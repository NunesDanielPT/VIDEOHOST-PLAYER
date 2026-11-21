#include "ndi_output.h"

#include "vthost/core/log.h"

#include <windows.h>

#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace vthost {
namespace {

// Declarações mínimas da API C do NDI (ABI estável desde o NDI 4).
// Só usamos o envio de vídeo; o runtime vem das NDI Tools instaladas pelo utilizador.
struct NdiSendCreate {
    const char* p_ndi_name;
    const char* p_groups;
    bool clock_video;
    bool clock_audio;
};

struct NdiVideoFrameV2 {
    int xres;
    int yres;
    int FourCC;
    int frame_rate_N;
    int frame_rate_D;
    float picture_aspect_ratio;
    int frame_format_type;
    std::int64_t timecode;
    std::uint8_t* p_data;
    int line_stride_in_bytes;
    const char* p_metadata;
    std::int64_t timestamp;
};

constexpr int fourcc(char a, char b, char c, char d) {
    return static_cast<int>(static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 8) |
                            (static_cast<std::uint32_t>(c) << 16) | (static_cast<std::uint32_t>(d) << 24));
}
constexpr int kFourCC_BGRA = fourcc('B', 'G', 'R', 'A');
constexpr int kFourCC_BGRX = fourcc('B', 'G', 'R', 'X');
constexpr int kFormatProgressive = 1;
constexpr std::int64_t kTimecodeSynthesize = INT64_MAX;

using FnInitialize = bool (*)();
using FnSendCreate = void* (*)(const NdiSendCreate*);
using FnSendDestroy = void (*)(void*);
using FnSendVideo = void (*)(void*, const NdiVideoFrameV2*);

struct Runtime {
    HMODULE dll = nullptr;
    FnInitialize initialize = nullptr;
    FnSendCreate sendCreate = nullptr;
    FnSendDestroy sendDestroy = nullptr;
    FnSendVideo sendVideo = nullptr;
    bool ready = false;
    std::string error;
};

std::wstring envDir(const wchar_t* name) {
    wchar_t* v = nullptr;
    std::size_t len = 0;
    std::wstring out;
    if (_wdupenv_s(&v, &len, name) == 0 && v) {
        out = v;
        std::free(v);
    }
    return out;
}

Runtime& runtime() {
    static Runtime rt = [] {
        Runtime r;
        // O instalador do NDI define NDI_RUNTIME_DIR_Vx; tenta do mais recente para o mais antigo.
        for (const wchar_t* var : {L"NDI_RUNTIME_DIR_V6", L"NDI_RUNTIME_DIR_V5", L"NDI_RUNTIME_DIR_V4"}) {
            const std::wstring dir = envDir(var);
            if (dir.empty()) continue;
            r.dll = LoadLibraryExW((dir + L"\\Processing.NDI.Lib.x64.dll").c_str(), nullptr,
                                   LOAD_WITH_ALTERED_SEARCH_PATH);
            if (r.dll) break;
        }
        if (!r.dll) r.dll = LoadLibraryW(L"Processing.NDI.Lib.x64.dll");
        if (!r.dll) {
            r.error = "runtime NDI não instalado (instale as NDI Tools gratuitas: ndi.video/tools)";
            return r;
        }
        r.initialize = reinterpret_cast<FnInitialize>(GetProcAddress(r.dll, "NDIlib_initialize"));
        r.sendCreate = reinterpret_cast<FnSendCreate>(GetProcAddress(r.dll, "NDIlib_send_create"));
        r.sendDestroy = reinterpret_cast<FnSendDestroy>(GetProcAddress(r.dll, "NDIlib_send_destroy"));
        r.sendVideo = reinterpret_cast<FnSendVideo>(GetProcAddress(r.dll, "NDIlib_send_send_video_v2"));
        if (!r.initialize || !r.sendCreate || !r.sendDestroy || !r.sendVideo) {
            r.error = "runtime NDI incompatível";
            return r;
        }
        if (!r.initialize()) {
            r.error = "o NDI não suporta este processador";
            return r;
        }
        r.ready = true;
        log::info("runtime NDI carregado");
        return r;
    }();
    return rt;
}

} // namespace

bool NdiOutput::runtimeAvailable() { return runtime().ready; }

NdiOutput::NdiOutput(std::string id, std::string sourceName, bool withAlpha)
    : id_(std::move(id)), sourceName_(std::move(sourceName)), withAlpha_(withAlpha) {
    thread_ = std::thread([this] { run(); });
}

NdiOutput::~NdiOutput() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string NdiOutput::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

std::string NdiOutput::detail() const { return "NDI: " + sourceName_ + (withAlpha_ ? " (alpha)" : ""); }

void NdiOutput::submit(const std::uint8_t* data, int rowPitch, int width, int height, double fps) {
    if (!ok_.load() || width <= 0 || height <= 0) return;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return; // a thread de envio está a mexer no buffer: salta este frame
    const std::size_t row = static_cast<std::size_t>(width) * 4;
    pending_.resize(row * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y)
        std::memcpy(pending_.data() + row * static_cast<std::size_t>(y), data + static_cast<std::size_t>(y) * rowPitch, row);
    pendingW_ = width;
    pendingH_ = height;
    pendingFps_ = fps > 1.0 ? fps : 60.0;
    hasPending_ = true;
    lock.unlock();
    cv_.notify_one();
}

void NdiOutput::run() {
    Runtime& rt = runtime();
    if (!rt.ready) {
        std::lock_guard lock(mutex_);
        error_ = rt.error;
        return;
    }
    NdiSendCreate desc{sourceName_.c_str(), nullptr, false, false};
    void* sender = rt.sendCreate(&desc);
    if (!sender) {
        std::lock_guard lock(mutex_);
        error_ = "não foi possível criar a fonte NDI";
        return;
    }
    log::info("NDI: fonte \"" + sourceName_ + "\" criada");
    ok_ = true;

    std::vector<std::uint8_t> sending;
    for (;;) {
        int w = 0, h = 0;
        double fps = 60.0;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return stop_ || hasPending_; });
            if (stop_) break;
            sending.swap(pending_); // troca de buffers: o render loop escreve no outro
            w = pendingW_;
            h = pendingH_;
            fps = pendingFps_;
            hasPending_ = false;
        }
        NdiVideoFrameV2 f{};
        f.xres = w;
        f.yres = h;
        f.FourCC = withAlpha_ ? kFourCC_BGRA : kFourCC_BGRX;
        f.frame_rate_N = static_cast<int>(std::lround(fps * 1000.0));
        f.frame_rate_D = 1000;
        f.picture_aspect_ratio = static_cast<float>(w) / static_cast<float>(h);
        f.frame_format_type = kFormatProgressive;
        f.timecode = kTimecodeSynthesize;
        f.p_data = sending.data();
        f.line_stride_in_bytes = w * 4;
        f.p_metadata = nullptr;
        f.timestamp = 0;
        try {
            rt.sendVideo(sender, &f);
        } catch (...) {
        }
    }
    ok_ = false;
    rt.sendDestroy(sender);
}

} // namespace vthost
