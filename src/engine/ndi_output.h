#pragma once
// Saída NDI. O runtime NDI (Processing.NDI.Lib.x64.dll, instalado com as NDI
// Tools gratuitas) é carregado em tempo de execução: se não existir, a saída
// fica em erro com uma mensagem clara e o resto do motor continua normal.
//
// O envio corre numa thread própria com uma "caixa de correio" de 1 frame: o
// render loop copia o frame e segue; se o envio atrasar, frames são saltados,
// nunca bloqueia o output principal.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vthost {

class NdiOutput {
public:
    NdiOutput(std::string id, std::string sourceName, bool withAlpha);
    ~NdiOutput();
    NdiOutput(const NdiOutput&) = delete;
    NdiOutput& operator=(const NdiOutput&) = delete;

    // Chamado no render loop: copia o frame (BGRA) e devolve logo.
    void submit(const std::uint8_t* data, int rowPitch, int width, int height, double fps);

    const std::string& id() const { return id_; }
    bool ok() const { return ok_.load(); }
    std::string error() const;
    std::string detail() const;

    static bool runtimeAvailable();

private:
    void run();

    std::string id_;
    std::string sourceName_;
    bool withAlpha_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::uint8_t> pending_;
    int pendingW_ = 0, pendingH_ = 0;
    double pendingFps_ = 60.0;
    bool hasPending_ = false;
    bool stop_ = false;
    std::atomic<bool> ok_{false};
    std::string error_;
    std::thread thread_;
};

} // namespace vthost
