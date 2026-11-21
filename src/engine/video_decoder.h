#pragma once
// Descodificador de vídeo/imagem com FFmpeg, numa thread própria.
//
// Regras de robustez:
//  - Nada aqui lança para fora: erros passam a State::Error com mensagem.
//  - Ficheiros corrompidos/pacotes partidos são saltados; só muitos erros
//    seguidos dão erro definitivo.
//  - A fila de frames é limitada (memória constante).
//  - O destrutor pára a thread mesmo que o FFmpeg esteja bloqueado a ler
//    (interrupt callback).

#include "decode_range.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vthost {

struct VideoFrame {
    std::vector<std::uint8_t> bgra; // width*height*4, linhas contíguas
    int width = 0;
    int height = 0;
    double time = 0.0; // tempo de media acumulado (tempo no ficheiro + passagem * duração da passagem)
};

class VideoDecoder {
public:
    // NoVideo: ficheiro só de áudio (não é erro: a imagem fica preta).
    enum class State { Opening, Playing, Ended, Error, NoVideo };

    // Começa em `startTime` (tempo acumulado; recuperação/saltos) e toca a zona
    // In..Out `range.passes` vezes.
    VideoDecoder(std::string utf8Path, DecodeRange range, double startTime);
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Devolve o frame mais recente com time <= mediaTime (descarta os mais antigos).
    // nullptr se ainda não houver nenhum novo: quem chama mantém o anterior (frame hold).
    std::shared_ptr<VideoFrame> takeFrameFor(double mediaTime);
    // Devolve o frame mais antigo da fila (usado para arrancar o relógio).
    std::shared_ptr<VideoFrame> takeOldest();

    // Devolve um frame já usado para reaproveitar a memória.
    void recycle(std::shared_ptr<VideoFrame> frame);

    State state() const { return state_.load(); }
    // Acabou (todas as passagens) e já não há frames por mostrar.
    bool drained() const;
    std::string error() const;
    double duration() const { return duration_.load(); } // do ficheiro; 0 se desconhecida
    bool opened() const { return opened_.load(); }       // duração já conhecida (ou desistiu)
    bool isStill() const { return still_.load(); }

    // Destrói um descodificador numa thread à parte: parar uma thread a meio de
    // descodificar um frame 4K pode demorar milissegundos, e isso não pode
    // causar um soluço no output.
    static void retireAsync(std::unique_ptr<VideoDecoder> decoder);

private:
    void run();
    void fail(const std::string& message);
    std::shared_ptr<VideoFrame> allocFrame();

    const std::string path_;
    const DecodeRange range_;
    const double startTime_;

    std::atomic<bool> stop_{false};
    std::atomic<State> state_{State::Opening};
    std::atomic<double> duration_{0.0};
    std::atomic<bool> still_{false};
    std::atomic<bool> opened_{false};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<VideoFrame>> queue_;
    std::vector<std::shared_ptr<VideoFrame>> pool_;
    std::string error_;

    std::thread thread_;
};

} // namespace vthost
