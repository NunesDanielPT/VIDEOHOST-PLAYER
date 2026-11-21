#pragma once
// Servidor de comandos num named pipe local (\\.\pipe\vthost-engine).
// Corre numa thread própria; os comandos são executados no render loop
// (thread principal) através de uma fila, por isso nenhum estado do motor é
// partilhado entre threads.

#include <windows.h>

#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>

namespace vthost {

class IpcServer {
public:
    struct Request {
        std::string line;
        std::promise<std::string> reply;
    };

    IpcServer();
    ~IpcServer();

    bool start(const char* pipeName);
    void stop();

    // Chamado na thread principal: executa todos os pedidos pendentes.
    void drain(const std::function<std::string(const std::string&)>& handler);

private:
    void run();
    void serveClient(HANDLE pipe);
    bool writeAll(HANDLE pipe, const std::string& data);

    std::wstring pipePath_;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
    std::mutex mutex_;
    std::deque<Request> queue_;
};

} // namespace vthost
