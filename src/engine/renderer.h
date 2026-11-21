#pragma once
// Renderer Direct3D 11: compõe as layers num canvas e desenha o canvas nos outputs.

#include "vthost/core/project.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <string>

namespace vthost {

using Microsoft::WRL::ComPtr;

struct VideoFrame;

// Textura de uma media (actualizada a partir dos frames descodificados).
struct GpuTexture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    int width = 0;
    int height = 0;
    bool valid() const { return srv != nullptr; }
};

class Renderer {
public:
    // Tenta a GPU; se falhar, usa WARP (software) para o show continuar.
    bool init(std::string* error);
    bool usingSoftwareFallback() const { return warp_; }

    ID3D11Device* device() const { return device_.Get(); }
    IDXGIFactory2* factory() const { return factory_.Get(); }

    bool resizeCanvas(int width, int height);
    void beginCanvas();
    // Desenha a textura no canvas com as propriedades da layer (posição, escala,
    // rotação, opacidade, blend). `extraOpacity` multiplica a opacidade (fades).
    void drawLayer(const GpuTexture& tex, const LayerProps& props, float extraOpacity);
    ID3D11ShaderResourceView* canvasSrv() const { return canvasSrv_.Get(); }

    // Desenha o canvas no back buffer de um output (escala para caber).
    void drawCanvasTo(ID3D11RenderTargetView* rtv, int width, int height);

    // Copia o frame para a textura (recria-a se o tamanho mudou).
    bool upload(GpuTexture& tex, const VideoFrame& frame);

    // Preview para o Studio: reduz o canvas e copia-o para a CPU de forma
    // assíncrona (lê a cópia de 2 frames atrás, nunca espera pela GPU).
    // `sink` só é chamado quando há dados prontos.
    using PreviewSink = std::function<void(const std::uint8_t* data, int rowPitch, int width, int height)>;
    void capturePreview(const PreviewSink& sink);
    // Canvas em resolução total (para NDI), também assíncrono.
    void captureCanvas(const PreviewSink& sink);

    int canvasWidth() const { return canvasW_; }
    int canvasHeight() const { return canvasH_; }

    // true se o dispositivo foi perdido (driver reiniciado, GPU removida...).
    bool deviceLost() const;

private:
    void setBlend(BlendMode blend, bool opaqueCopy);
    // corners: 4 cantos em NDC (sup-esq, sup-dir, inf-esq, inf-dir).
    void drawQuad(ID3D11ShaderResourceView* srv, const float corners[4][2], float opacity, unsigned mode);
    bool ensurePreviewTargets(int w, int h);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11Buffer> cbuffer_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11BlendState> blendNormal_, blendAdd_, blendMultiply_, blendScreen_, blendOpaque_;
    ComPtr<ID3D11RasterizerState> raster_;

    ComPtr<ID3D11Texture2D> canvas_;
    ComPtr<ID3D11RenderTargetView> canvasRtv_;
    ComPtr<ID3D11ShaderResourceView> canvasSrv_;
    int canvasW_ = 0;
    int canvasH_ = 0;
    bool warp_ = false;

    static constexpr int kPreviewRing = 3;
    ComPtr<ID3D11Texture2D> previewRt_;
    ComPtr<ID3D11RenderTargetView> previewRtv_;
    ComPtr<ID3D11Texture2D> previewStaging_[kPreviewRing];
    bool previewPending_[kPreviewRing] = {};
    int previewIndex_ = 0;
    int previewW_ = 0;
    int previewH_ = 0;

    ComPtr<ID3D11Texture2D> fullStaging_[kPreviewRing];
    bool fullPending_[kPreviewRing] = {};
    int fullIndex_ = 0;
    int fullW_ = 0;
    int fullH_ = 0;
};

} // namespace vthost
