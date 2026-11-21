#include "audio_output.h"

#include "reaper.h"
#include "vthost/core/log.h"

#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propsys.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vthost {

using Microsoft::WRL::ComPtr;

namespace {

constexpr int kRate = AudioDecoder::kRate;
constexpr REFERENCE_TIME kBufferHns = 300000; // 30 ms de buffer no Windows

// Constantes definidas aqui para não depender de INITGUID / bibliotecas extra.
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::string utf8(const wchar_t* w) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<std::size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string hrText(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::string friendlyName(IMMDevice* dev) {
    ComPtr<IPropertyStore> props;
    if (!dev || FAILED(dev->OpenPropertyStore(STGM_READ, &props))) return {};
    PROPVARIANT v;
    PropVariantInit(&v);
    std::string name;
    if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR) name = utf8(v.pwszVal);
    PropVariantClear(&v);
    return name;
}

std::string deviceIdOf(IMMDevice* dev) {
    LPWSTR id = nullptr;
    if (!dev || FAILED(dev->GetId(&id)) || !id) return {};
    std::string s = utf8(id);
    CoTaskMemFree(id);
    return s;
}

// COM na thread actual, só enquanto o objeto existir.
struct ComScope {
    bool owns = false;
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owns = SUCCEEDED(hr); // S_OK ou S_FALSE; RPC_E_CHANGED_MODE = já inicializado noutro modo
    }
    ~ComScope() {
        if (owns) CoUninitialize();
    }
};

} // namespace

AudioOutput::AudioOutput() {
    thread_ = std::thread([this] { run(); });
}

AudioOutput::~AudioOutput() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    for (auto& s : sources_) retireLater(std::move(s.decoder));
    for (auto& g : graveyard_) retireLater(std::move(g.second));
    sources_.clear();
    graveyard_.clear();
}

void AudioOutput::setDevice(const std::string& deviceId) {
    std::lock_guard lock(mutex_);
    if (deviceId == deviceId_) return;
    deviceId_ = deviceId;
    reopen_ = true;
}

int AudioOutput::addSource(std::shared_ptr<AudioDecoder> decoder) {
    std::lock_guard lock(mutex_);
    collectGarbage();
    Source s;
    s.handle = nextHandle_++;
    if (nextHandle_ > 1000000000) nextHandle_ = 1;
    s.decoder = std::move(decoder);
    sources_.push_back(std::move(s));
    return sources_.back().handle;
}

void AudioOutput::updateSource(int handle, const MediaClock& clock, float gain) {
    std::lock_guard lock(mutex_);
    for (auto& s : sources_)
        if (s.handle == handle) {
            s.clock = clock;
            s.gain = std::isfinite(gain) ? std::clamp(gain, 0.0f, 16.0f) : 0.0f;
            return;
        }
}

void AudioOutput::removeSource(int handle) {
    std::lock_guard lock(mutex_);
    for (auto it = sources_.begin(); it != sources_.end(); ++it)
        if (it->handle == handle) {
            graveyard_.emplace_back(std::chrono::steady_clock::now(), std::move(it->decoder));
            sources_.erase(it);
            break;
        }
    collectGarbage();
}

void AudioOutput::collectGarbage() {
    // (mutex_ já trancado)
    const auto now = std::chrono::steady_clock::now();
    for (auto it = graveyard_.begin(); it != graveyard_.end();) {
        if (now - it->first > std::chrono::seconds(1)) {
            retireLater(std::move(it->second));
            it = graveyard_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string AudioOutput::deviceName() const {
    std::lock_guard lock(mutex_);
    return deviceName_;
}

std::string AudioOutput::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

void AudioOutput::setError(const std::string& e) {
    bool changed = false;
    {
        std::lock_guard lock(mutex_);
        changed = e != error_;
        error_ = e;
    }
    if (changed && !e.empty()) log::error("áudio: " + e);
}

std::vector<protocol::AudioDeviceInfo> AudioOutput::listDevices() {
    std::vector<protocol::AudioDeviceInfo> out;
    try {
        ComScope com;
        ComPtr<IMMDeviceEnumerator> en;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return out;
        std::string defId;
        ComPtr<IMMDevice> def;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def))) defId = deviceIdOf(def.Get());
        ComPtr<IMMDeviceCollection> col;
        if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) return out;
        UINT n = 0;
        col->GetCount(&n);
        for (UINT i = 0; i < n && i < 64; ++i) {
            ComPtr<IMMDevice> d;
            if (FAILED(col->Item(i, &d))) continue;
            protocol::AudioDeviceInfo info;
            info.id = deviceIdOf(d.Get());
            info.name = friendlyName(d.Get());
            info.isDefault = !defId.empty() && info.id == defId;
            if (!info.id.empty()) out.push_back(std::move(info));
        }
    } catch (...) {
    }
    return out;
}

void AudioOutput::mix(float* out, std::size_t frames, double latencySeconds) {
    std::memset(out, 0, frames * 2 * sizeof(float));
    struct Snap {
        int handle;
        std::shared_ptr<AudioDecoder> decoder;
        MediaClock clock;
        float gain;
    };
    std::vector<Snap> snap;
    {
        std::lock_guard lock(mutex_);
        snap.reserve(sources_.size());
        for (const auto& s : sources_) snap.push_back({s.handle, s.decoder, s.clock, s.gain});
    }
    // Ganhos anteriores (rampa por período: sem cliques ao mudar volume/fades).
    static thread_local std::vector<std::pair<int, float>> lastGains;
    std::vector<std::pair<int, float>> nextGains;
    auto previousGain = [&](int h) {
        for (const auto& [k, g] : lastGains)
            if (k == h) return g;
        return 0.0f; // fonte nova: começa em silêncio e sobe neste período
    };

    const auto playAt = MediaClock::Clock::now() +
                        std::chrono::duration_cast<MediaClock::Clock::duration>(std::chrono::duration<double>(latencySeconds));
    scratch_.resize(frames * 2);
    for (const auto& s : snap) {
        if (!s.decoder) continue;
        const float g0 = previousGain(s.handle);
        const float g1 = s.gain;
        nextGains.emplace_back(s.handle, g1);
        if (!s.clock.started || s.clock.paused) continue;
        std::fill(scratch_.begin(), scratch_.end(), 0.0f);
        const std::size_t n = s.decoder->read(s.clock.at(playAt), scratch_.data(), frames);
        if (n == 0 || (g0 <= 0.0f && g1 <= 0.0f)) continue;
        const float inv = 1.0f / static_cast<float>(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            const float g = g0 + (g1 - g0) * (static_cast<float>(i) * inv);
            out[2 * i] += scratch_[2 * i] * g;
            out[2 * i + 1] += scratch_[2 * i + 1] * g;
        }
    }
    lastGains.swap(nextGains);

    float pl = 0.0f, pr = 0.0f;
    for (std::size_t i = 0; i < frames; ++i) {
        float& l = out[2 * i];
        float& r = out[2 * i + 1];
        if (!std::isfinite(l)) l = 0.0f;
        if (!std::isfinite(r)) r = 0.0f;
        l = std::clamp(l, -1.0f, 1.0f);
        r = std::clamp(r, -1.0f, 1.0f);
        pl = std::max(pl, std::abs(l));
        pr = std::max(pr, std::abs(r));
    }
    // Medidores com queda suave.
    levelL_ = std::max(pl, levelL_.load() * 0.85f);
    levelR_ = std::max(pr, levelR_.load() * 0.85f);
}

void AudioOutput::run() {
    ComScope com;
    DWORD taskIndex = 0;
    HANDLE avrt = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    auto sleepInterruptible = [&](int ms) {
        for (int t = 0; t < ms && !stop_ && !reopen_; t += 50) Sleep(50);
    };

    while (!stop_) {
        try {
            reopen_ = false;
            std::string wantedId;
            {
                std::lock_guard lock(mutex_);
                wantedId = deviceId_;
            }
            ComPtr<IMMDeviceEnumerator> en;
            HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
            if (FAILED(hr)) {
                setError("serviço de áudio do Windows indisponível (" + hrText(hr) + ")");
                sleepInterruptible(1000);
                continue;
            }
            ComPtr<IMMDevice> dev;
            std::string note;
            if (!wantedId.empty() && FAILED(en->GetDevice(wide(wantedId).c_str(), &dev))) {
                dev.Reset();
                note = "placa de som escolhida não encontrada: a usar a do Windows";
            }
            if (!dev && FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) {
                setError("sem placa de som");
                sleepInterruptible(1000);
                continue;
            }
            const std::string name = friendlyName(dev.Get());

            ComPtr<IAudioClient> client;
            hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
            if (FAILED(hr)) {
                setError("não foi possível abrir " + name + " (" + hrText(hr) + ")");
                sleepInterruptible(1000);
                continue;
            }
            WAVEFORMATEXTENSIBLE wf{};
            wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            wf.Format.nChannels = 2;
            wf.Format.nSamplesPerSec = kRate;
            wf.Format.wBitsPerSample = 32;
            wf.Format.nBlockAlign = 8;
            wf.Format.nAvgBytesPerSec = kRate * 8;
            wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            wf.Samples.wValidBitsPerSample = 32;
            wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
            wf.SubFormat = kSubtypeFloat;
            // AUTOCONVERTPCM: o Windows converte para o formato da placa (frequência/canais).
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    kBufferHns, 0, &wf.Format, nullptr);
            UINT32 bufferFrames = 0;
            ComPtr<IAudioRenderClient> render;
            if (SUCCEEDED(hr)) hr = client->SetEventHandle(event);
            if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufferFrames);
            if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&render));
            if (SUCCEEDED(hr) && bufferFrames > 0) {
                // Silêncio inicial (evita um clique ao arrancar).
                BYTE* data = nullptr;
                if (SUCCEEDED(render->GetBuffer(bufferFrames, &data))) render->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
                hr = client->Start();
            }
            if (FAILED(hr) || bufferFrames == 0) {
                setError("não foi possível iniciar o som em " + name + " (" + hrText(hr) + ")");
                sleepInterruptible(1000);
                continue;
            }
            {
                std::lock_guard lock(mutex_);
                deviceName_ = name;
            }
            setError(note);
            ok_ = true;
            log::info("áudio: " + name + " (48 kHz, buffer " + std::to_string(bufferFrames) + " frames)");

            while (!stop_ && !reopen_) {
                WaitForSingleObject(event, 200);
                UINT32 padding = 0;
                hr = client->GetCurrentPadding(&padding);
                if (FAILED(hr)) break;
                const UINT32 avail = bufferFrames > padding ? bufferFrames - padding : 0;
                if (avail == 0) continue;
                BYTE* data = nullptr;
                hr = render->GetBuffer(avail, &data);
                if (FAILED(hr)) break;
                try {
                    mix(reinterpret_cast<float*>(data), avail, static_cast<double>(padding) / kRate);
                    hr = render->ReleaseBuffer(avail, 0);
                } catch (...) {
                    hr = render->ReleaseBuffer(avail, AUDCLNT_BUFFERFLAGS_SILENT);
                }
                if (FAILED(hr)) break;
            }
            client->Stop();
            ok_ = false;
            levelL_ = 0.0f;
            levelR_ = 0.0f;
            if (!stop_ && !reopen_) {
                // Placa desligada / driver reiniciado: tenta de novo.
                setError("placa de som perdida (" + hrText(hr) + "): a tentar de novo");
                sleepInterruptible(1000);
            }
        } catch (...) {
            ok_ = false;
            setError("erro inesperado no áudio: a tentar de novo");
            sleepInterruptible(1000);
        }
    }
    if (event) CloseHandle(event);
    if (avrt) AvRevertMmThreadCharacteristics(avrt);
}

} // namespace vthost
