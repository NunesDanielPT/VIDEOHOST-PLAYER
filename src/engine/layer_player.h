#pragma once
// Reprodução de uma layer: media actual + media anterior durante o fade.

#include "renderer.h"
#include "video_decoder.h"

#include <chrono>
#include <memory>
#include <string>

namespace vthost {

class LayerPlayer {
public:
    using Clock = std::chrono::steady_clock;

    explicit LayerPlayer(std::string layerId) : layerId_(std::move(layerId)) {}
    ~LayerPlayer();
    LayerPlayer(const LayerPlayer&) = delete;
    LayerPlayer& operator=(const LayerPlayer&) = delete;

    // Começa uma media nova com fade (fadeSeconds=0 => corte seco).
    // startSeconds > 0 serve para retomar após crash.
    // `tag` identifica quem pôs a media a tocar ("" = dashboard, id do clip = timeline).
    void play(const std::string& mediaId, const std::string& utf8Path, bool loop, double fadeSeconds,
              double startSeconds = 0.0, const std::string& tag = {});
    // Fade para vazio.
    void stop(double fadeSeconds);

    // Pausa: o relógio da media congela e o frame actual fica no ecrã.
    void setPaused(bool paused);
    bool paused() const { return paused_; }

    void update(Renderer& renderer); // puxa frames novos e envia para a GPU
    void draw(Renderer& renderer, const LayerProps& props, float extraOpacity = 1.0f);

    const std::string& layerId() const { return layerId_; }
    const std::string& mediaId() const { return current_.mediaId; }
    const std::string& tag() const { return current_.tag; }
    void clearTag() { current_.tag = "\x01"; } // força a próxima comparação a falhar
    double position() const; // posição na media actual (segundos)
    bool ended() const;      // media actual chegou ao fim (sem loop) e não é imagem fixa
    std::string stateName() const;
    std::string error() const;

private:
    struct Slot {
        std::string mediaId;
        std::string tag;
        std::unique_ptr<VideoDecoder> decoder;
        GpuTexture texture;
        bool clockStarted = false;
        double clockBase = 0.0; // tempo de media do primeiro frame
        Clock::time_point clockStart{};
        double shownTime = 0.0;
        bool hasFrame = false;
        double frozenTime = 0.0; // tempo de media no momento da pausa
        double mediaTime(bool paused) const;
    };

    void updateSlot(Slot& s, Renderer& renderer);
    float fadeProgress() const;

    std::string layerId_;
    Slot current_;
    Slot previous_;
    Clock::time_point fadeStart_{};
    double fadeSeconds_ = 0.0;
    bool paused_ = false;
};

} // namespace vthost
