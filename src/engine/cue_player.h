#pragma once
// Reprodução da playlist: a cue no ar e, durante um crossfade ou um fade para
// preto, a anterior. Imagem (VideoDecoder -> textura) e som (AudioDecoder ->
// AudioOutput) partilham o MediaClock de cada cue.
//
// Tudo corre no render loop e nunca bloqueia: abrir/fechar ficheiros acontece
// nas threads dos descodificadores e no reaper.

#include "audio_decoder.h"
#include "audio_output.h"
#include "decode_range.h"
#include "media_clock.h"
#include "renderer.h"
#include "video_decoder.h"
#include "vthost/core/project.h"
#include "vthost/core/protocol.h"

#include <chrono>
#include <memory>
#include <string>

namespace vthost {

class CuePlayer {
public:
    using Clock = std::chrono::steady_clock;

    struct StartOptions {
        double startTime = -1.0; // tempo de cue acumulado (<0 = ponto In)
        bool paused = false;     // fica pronta no 1.º frame
        bool cut = false;        // sem crossfade nem fade in
    };

    CuePlayer(Renderer& renderer, AudioOutput& audio) : renderer_(renderer), audio_(audio) {}
    ~CuePlayer();
    CuePlayer(const CuePlayer&) = delete;
    CuePlayer& operator=(const CuePlayer&) = delete;

    void start(const Cue& cue, int index, const StartOptions& opts);
    void stop(double fadeSeconds); // para preto (e silêncio)
    void setPaused(bool paused);
    void holdAtEnd();              // "pausa no fim": fica no último frame
    void seekCueTime(double cueTime);
    void seekInPass(double secondsFromIn); // salto dentro da passagem actual (waveform)
    void seekRemaining(double seconds);    // GOTO 30/20/10

    // Parâmetros ao vivo (volume, fades, geometria, comportamento no fim...).
    // Trim, velocidade e repetições só mudam na próxima vez que a cue começar.
    void updateCue(const Cue& cue, int index);

    // Por frame: frames novos, áudio, transições. Ganho do som = cue + master (dB).
    void update(double normalizeTargetLufs, double masterDb);
    // No canvas actual. Sem `props`: geometria da cue (MEDIAPLAYER); com `props`:
    // propriedades da layer (CUEPLAYER). `opacity` multiplica tudo.
    void draw(const LayerProps* props = nullptr, float opacity = 1.0f);

    bool active() const { return cur_.active; }
    int index() const { return cur_.index; }
    const std::string& cueId() const { return cur_.cue.id; }
    const Cue& cue() const { return cur_.cue; }
    bool paused() const { return cur_.active && (cur_.clock.paused || cur_.pendingPause); }
    bool holding() const { return cur_.active && cur_.holding; }
    bool reachedEnd() const;
    bool endHandled() const { return cur_.endHandled; }
    void setEndHandled() { cur_.endHandled = true; }
    double secondsSinceStart() const;
    double cueTime() const; // para o estado do show (recuperação)

    void fillStatus(protocol::EngineStatus& s) const;

private:
    enum class Trans { None, In, Out };
    struct Slot {
        bool active = false;
        Cue cue;
        int index = -1;
        DecodeRange range;
        double startTime = 0.0;
        std::unique_ptr<VideoDecoder> video;
        std::shared_ptr<AudioDecoder> audio;
        int audioHandle = 0;
        GpuTexture tex;
        bool hasFrame = false;
        MediaClock clock;
        bool pendingPause = false;
        bool fadeInEnabled = true;
        bool holding = false;
        bool endHandled = false;
        Trans trans = Trans::None;
        Clock::time_point transStart{};
        double transSeconds = 0.0;
        Clock::time_point startedAt{};
    };

    void openDecoders(Slot& s, double startTime);
    void retire(Slot& s);
    void updateSlot(Slot& s, double normalizeTargetLufs, double masterDb);
    double passLength(const Slot& s) const;  // tempo de media de uma passagem (0 = desconhecido / infinito)
    double endTime(const Slot& s) const;     // tempo de cue em que acaba (0 = nunca)
    float opacity(const Slot& s) const;

    Renderer& renderer_;
    AudioOutput& audio_;
    Slot cur_;
    Slot prev_;
};

} // namespace vthost
