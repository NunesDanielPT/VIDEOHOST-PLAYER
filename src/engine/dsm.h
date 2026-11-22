#pragma once
// Modo DSM de uma saída: o canvas mais pequeno em cima e uma faixa com a duração do
// vídeo e o tempo que falta (texto). O texto é desenhado com GDI numa thread própria
// (DsmPanelRenderer) e o render loop só copia a imagem pronta para a GPU.

#include "renderer.h"
#include "vthost/core/project.h"

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vthost {

// Geometria (vídeo + faixa) de uma saída em modo DSM. update() só refaz as malhas
// quando os tamanhos mudam.
struct DsmGeometry {
    OutputMapping video, panel;
    WarpMesh videoMesh, panelMesh;
    int outW = 0, outH = 0, canvasW = 0, canvasH = 0;
    int panelW = 0, panelH = 0; // tamanho em px da faixa (para a imagem do texto)
    bool update(Renderer& r, int outW, int outH, int canvasW, int canvasH);
};

// Desenha a saída DSM inteira em `rtv` (limpa, vídeo, faixa).
void drawDsm(Renderer& r, ID3D11RenderTargetView* rtv, const DsmGeometry& g, const std::string& canvasId,
             const std::string& panelCanvasId, bool testCard);

std::string dsmPanelCanvasId(const std::string& outputId);

struct DsmPanelRequest {
    int width = 0, height = 0;
    std::string labelLength, labelRemaining; // UTF-8
    std::string length, remaining;            // ex. "02:41", "-01:12"
    std::uint32_t remainingColor = 0xFFFFFF;  // 0xRRGGBB
    bool operator==(const DsmPanelRequest&) const = default;
};

struct DsmPanelImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bgra;
};

class DsmPanelRenderer {
public:
    DsmPanelRenderer();
    ~DsmPanelRenderer();
    DsmPanelRenderer(const DsmPanelRenderer&) = delete;
    DsmPanelRenderer& operator=(const DsmPanelRenderer&) = delete;

    // Nunca bloqueiam o render loop (try_lock: se a thread estiver ocupada, tenta no próximo frame).
    void request(const std::string& outputId, const DsmPanelRequest& r);
    bool take(const std::string& outputId, DsmPanelImage& out);
    void forget(); // volta a desenhar tudo (ex. os canvases foram recriados)

private:
    void run();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::string, DsmPanelRequest> pending_;
    std::map<std::string, DsmPanelRequest> done_;
    std::map<std::string, DsmPanelImage> ready_;
    bool stop_ = false;
    std::thread thread_;
};

} // namespace vthost
