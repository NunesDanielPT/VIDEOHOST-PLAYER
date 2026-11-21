#pragma once
// Saída de som (WASAPI, modo partilhado, float 48 kHz estéreo).
//
// Uma thread própria, de alta prioridade, mistura as fontes activas (a cue no ar
// e, durante um crossfade, a anterior). Cada fonte lê do seu AudioDecoder no
// tempo de media dado pelo MediaClock da cue, por isso o som segue a imagem
// mesmo com pausas, saltos e velocidade.
//
// Robustez: se a placa de som desaparecer (cabo USB, driver), a thread tenta
// reabrir de segundo a segundo; o vídeo nunca espera pelo som. Nada aqui lança
// para fora.

#include "audio_decoder.h"
#include "media_clock.h"
#include "vthost/core/protocol.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vthost {

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // "" = placa de som por omissão do Windows. Reabre se mudar.
    void setDevice(const std::string& deviceId);

    // Fontes (chamado no render loop). gain = linear, já com fades/mute.
    int addSource(std::shared_ptr<AudioDecoder> decoder);
    void updateSource(int handle, const MediaClock& clock, float gain);
    void removeSource(int handle);

    std::string deviceName() const;
    bool ok() const { return ok_.load(); }
    std::string error() const;
    float levelL() const { return levelL_.load(); }
    float levelR() const { return levelR_.load(); }

    static std::vector<protocol::AudioDeviceInfo> listDevices();

private:
    struct Source {
        int handle = 0;
        std::shared_ptr<AudioDecoder> decoder;
        MediaClock clock;
        float gain = 0.0f;
        float lastGain = 0.0f; // só a thread de áudio mexe (rampa sem cliques)
    };

    void run();
    void mix(float* out, std::size_t frames, double latencySeconds);
    void setError(const std::string& e);
    void collectGarbage();

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> ok_{false};
    std::atomic<bool> reopen_{false};
    std::atomic<float> levelL_{0.0f};
    std::atomic<float> levelR_{0.0f};

    mutable std::mutex mutex_;
    std::string deviceId_;
    std::string deviceName_;
    std::string error_;
    std::vector<Source> sources_;
    int nextHandle_ = 1;
    // Fontes retiradas: guardadas 1 s antes de destruir (a thread de áudio pode
    // ainda estar a ler) e depois entregues ao reaper.
    std::vector<std::pair<std::chrono::steady_clock::time_point, std::shared_ptr<AudioDecoder>>> graveyard_;

    std::vector<float> scratch_; // só a thread de áudio
};

} // namespace vthost
