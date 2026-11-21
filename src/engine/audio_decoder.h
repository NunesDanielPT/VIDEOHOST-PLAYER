#pragma once
// Descodificador de áudio com FFmpeg, numa thread própria (abre o ficheiro à
// parte do vídeo: um problema no som nunca pára a imagem).
//
// Saída: float estéreo intercalado a 48 kHz, já com a velocidade aplicada
// (reamostragem: a 2x o som fica mais agudo, como num leitor de cassetes).
// Cada bloco leva o tempo de media do primeiro sample, com a mesma regra de
// tempo acumulado da imagem (DecodeRange), por isso ficam alinhados.
//
// A fila é limitada (~1 s); a thread de mistura lê com read(), que alinha o som
// com o relógio (salta ou insere silêncio se houver desvio).

#include "decode_range.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vthost {

class AudioDecoder {
public:
    static constexpr int kRate = 48000;
    static constexpr int kChannels = 2;

    enum class State { Opening, Playing, Ended, Error, NoAudio };

    AudioDecoder(std::string utf8Path, DecodeRange range, double startTime, double speed);
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    // Thread de mistura: escreve `frames` frames estéreo em `out` para o tempo de
    // media `mediaTime` (tempo do primeiro frame). O que faltar fica a zero.
    // Devolve quantos frames tinham som.
    std::size_t read(double mediaTime, float* out, std::size_t frames);

    State state() const { return state_.load(); }
    bool finished() const; // acabou e já não há nada na fila
    bool hasData() const;  // já há som pronto (para arrancar o relógio)
    double speed() const { return speed_; }
    std::string error() const;

private:
    struct Chunk {
        double time = 0.0;        // tempo de media do primeiro frame
        std::vector<float> data;  // estéreo intercalado
        std::size_t pos = 0;      // frames já consumidos
    };

    void run();
    void fail(const std::string& message);
    bool push(Chunk&& c); // espera por espaço; false se parar

    const std::string path_;
    const DecodeRange range_;
    const double startTime_;
    const double speed_;
    const double step_; // tempo de media por frame de saída

    std::atomic<bool> stop_{false};
    std::atomic<State> state_{State::Opening};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Chunk> queue_;
    std::size_t queuedFrames_ = 0;
    std::string error_;

    std::thread thread_;
};

} // namespace vthost
