#pragma once
// Renderer Direct3D 11: compõe as layers num canvas e desenha o canvas nos outputs.

#include "vthost/core/project.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

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
    void drawLayer(const GpuTexture& tex, float opacity, BlendMode blend);
    ID3D11ShaderResourceView* canvasSrv() const { return canvasSrv_.Get(); }

    // Desenha o canvas no back buffer de um output (escala para caber).
    void drawCanvasTo(ID3D11RenderTargetView* rtv, int width, int height);

    // Copia o frame para a textura (recria-a se o tamanho mudou).
    bool upload(GpuTexture& tex, const VideoFrame& frame);

    // true se o dispositivo foi perdido (driver reiniciado, GPU removida...).
    bool deviceLost() const;

private:
    void setBlend(BlendMode blend, bool opaqueCopy);
    void drawQuad(ID3D11ShaderResourceView* srv, float opacity, int mode);

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
};

} // namespace vthost
