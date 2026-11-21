#include "ipc_server.h"

#include "vthost/core/log.h"
#include "vthost/core/protocol.h"

#include <algorithm>
#include <chrono>

namespace vthost {
namespace {

struct EventHandle {
    HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~EventHandle() { if (h) CloseHandle(h); }
};

// Espera pela operação overlapped ou pelo pedido de paragem.
// true = operação terminou; false = paragem/erro (a operação foi cancelada).
bool waitIo(HANDLE pipe, OVERLAPPED& ov, HANDLE stopEvent, DWORD& bytes) {
    HANDLE handles[2] = {ov.hEvent, stopEvent};
    const DWORD w = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (w != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &ov);
        GetOverlappedResult(pipe, &ov, &bytes, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe, &ov, &bytes, FALSE) != FALSE;
}

} // namespace

IpcServer::IpcServer() { stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); }

IpcServer::~IpcServer() {
    stop();
    if (stopEvent_) CloseHandle(stopEvent_);
}

bool IpcServer::start(const char* pipeName) {
    std::string p = std::string("\\\\.\\pipe\\") + pipeName;
    pipePath_.assign(p.begin(), p.end());
    thread_ = std::thread([this] { run(); });
    return true;
}

void IpcServer::stop() {
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
}

void IpcServer::run() {
    bool first = true;
    while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0) {
        DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
        // Na primeira instância: garante que nenhum outro processo "sequestrou" o nome do pipe.
        if (first) openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        HANDLE pipe = CreateNamedPipeW(pipePath_.c_str(), openMode,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                           PIPE_REJECT_REMOTE_CLIENTS,
                                       1, 64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            log::error("IPC: CreateNamedPipe falhou (" + std::to_string(GetLastError()) + ")");
            WaitForSingleObject(stopEvent_, 1000);
            continue;
        }
        first = false;

        EventHandle ev;
        OVERLAPPED ov{};
        ov.hEvent = ev.h;
        bool connected = false;
        if (ConnectNamedPipe(pipe, &ov)) {
            connected = true;
        } else {
            const DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (err == ERROR_IO_PENDING) {
                DWORD bytes = 0;
                connected = waitIo(pipe, ov, stopEvent_, bytes);
            }
        }

        if (connected) {
            log::info("IPC: cliente ligado");
            serveClient(pipe);
            log::info("IPC: cliente desligado");
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

bool IpcServer::writeAll(HANDLE pipe, const std::string& data) {
    EventHandle ev;
    std::size_t off = 0;
    while (off < data.size()) {
        OVERLAPPED ov{};
        ov.hEvent = ev.h;
        ResetEvent(ev.h);
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - off, 64 * 1024));
        if (!WriteFile(pipe, data.data() + off, chunk, &written, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (!waitIo(pipe, ov, stopEvent_, written)) return false;
        } else {
            GetOverlappedResult(pipe, &ov, &written, FALSE);
        }
        if (written == 0) return false;
        off += written;
    }
    return true;
}

void IpcServer::serveClient(HANDLE pipe) {
    EventHandle ev;
    std::string pending;
    char buf[8192];
    for (;;) {
        OVERLAPPED ov{};
        ov.hEvent = ev.h;
        ResetEvent(ev.h);
        DWORD got = 0;
        if (!ReadFile(pipe, buf, sizeof(buf), &got, &ov)) {
            const DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) return; // cliente saiu
            if (!waitIo(pipe, ov, stopEvent_, got)) return;
        } else {
            GetOverlappedResult(pipe, &ov, &got, FALSE);
        }
        if (got == 0) continue;
        pending.append(buf, got);

        std::size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            std::future<std::string> fut;
            {
                Request req;
                req.line = std::move(line);
                fut = req.reply.get_future();
                std::lock_guard lock(mutex_);
                queue_.push_back(std::move(req));
            }
            std::string reply;
            if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
                reply = fut.get();
            } else {
                reply = protocol::errorResponse("motor ocupado (timeout)");
                log::warn("IPC: pedido sem resposta em 5 s");
            }
            reply += '\n';
            if (!writeAll(pipe, reply)) return;
        }
        if (pending.size() > protocol::kMaxLineBytes) {
            writeAll(pipe, protocol::errorResponse("mensagem demasiado grande") + "\n");
            return; // cliente com comportamento anómalo: desliga
        }
    }
}

void IpcServer::drain(const std::function<std::string(const std::string&)>& handler) {
    std::deque<Request> work;
    {
        std::lock_guard lock(mutex_);
        work.swap(queue_);
    }
    for (auto& r : work) {
        try {
            r.reply.set_value(handler(r.line));
        } catch (const std::exception& e) {
            try { r.reply.set_value(protocol::errorResponse(std::string("erro interno: ") + e.what())); } catch (...) {}
        } catch (...) {
            try { r.reply.set_value(protocol::errorResponse("erro interno")); } catch (...) {}
        }
    }
}

} // namespace vthost
