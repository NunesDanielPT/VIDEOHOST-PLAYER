#pragma once
// Renderer Direct3D 11.
//  - Vários canvas (um render target por canvas), onde se compõem as layers.
//  - Saídas: desenham uma região de um canvas com warp (malha de triângulos),
//    soft-edge, brilho e, opcionalmente, o test card.
//  - Cópias assíncronas para a CPU (preview do Studio e NDI), sem esperar pela GPU.

#include "vthost/core/mapping.h"
#include "vthost/core/project.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
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

// Malha de uma saída (warp) já na GPU.
struct WarpMesh {
    ComPtr<ID3D11Buffer> vb;
    UINT vertexCount = 0;
};

class Renderer {
public:
    // Tenta a GPU; se falhar, usa WARP (software) para o show continuar.
    bool init(std::string* error);
    bool usingSoftwareFallback() const { return warp_; }

    ID3D11Device* device() const { return device_.Get(); }
    IDXGIFactory2* factory() const { return factory_.Get(); }
    // Memória de vídeo usada pelo motor e a que o Windows lhe reserva (MB). false = sem dados.
    bool videoMemory(double& usedMb, double& budgetMb) const;

    // Canvas
    bool ensureCanvas(const std::string& id, int width, int height);
    void keepOnlyCanvases(const std::set<std::string>& ids);
    bool hasCanvas(const std::string& id) const { return canvases_.count(id) > 0; }
    void canvasSize(const std::string& id, int& w, int& h) const;
    void beginCanvas(const std::string& id); // limpa e passa a desenhar neste canvas

    // Desenha a textura no canvas atual com as propriedades da layer.
    // `extraOpacity` multiplica a opacidade (fades).
    void drawLayer(const GpuTexture& tex, const LayerProps& props, float extraOpacity);

    // Saídas
    bool buildWarpMesh(const OutputMapping& mapping, WarpMesh& out);
    void drawOutput(ID3D11RenderTargetView* rtv, int width, int height, const std::string& canvasId,
                    const OutputMapping& mapping, const WarpMesh& mesh, bool testCard);

    // Cópias para a CPU. `sink` só é chamado quando há dados prontos.
    using PixelSink = std::function<void(const std::uint8_t* data, int rowPitch, int width, int height)>;
    void capturePreview(const std::string& canvasId, const PixelSink& sink); // reduzido (<= 960x540)
    void captureCanvas(const std::string& canvasId, const PixelSink& sink);  // resolução total (NDI)

    // Copiar o frame descodificado para a textura (recria-a se o tamanho mudou).
    bool upload(GpuTexture& tex, const VideoFrame& frame);

    // true se o dispositivo foi perdido (driver reiniciado, GPU removida...).
    bool deviceLost() const;

private:
    struct Readback {
        static constexpr int kRing = 3;
        ComPtr<ID3D11Texture2D> staging[kRing];
        bool pending[kRing] = {};
        int index = 0;
        int w = 0;
        int h = 0;
    };
    struct CanvasTarget {
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11ShaderResourceView> srv;
        int w = 0;
        int h = 0;
        Readback full; // NDI
    };

    void setBlend(BlendMode blend, bool opaqueCopy);
    // corners: 4 cantos em NDC (sup-esq, sup-dir, inf-esq, inf-dir).
    void drawQuad(ID3D11ShaderResourceView* srv, const float corners[4][2], float opacity, unsigned mode);
    bool ensureReadback(Readback& rb, int w, int h);
    void readback(Readback& rb, ID3D11Texture2D* source, const PixelSink& sink);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<IDXGIAdapter3> adapter3_; // para a VRAM (Windows 10+)
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11VertexShader> layerVs_;   // layers: malha (corner pin) + efeitos + máscara
    ComPtr<ID3D11PixelShader> layerPs_;
    ComPtr<ID3D11Buffer> layerCbuffer_;
    ComPtr<ID3D11Buffer> layerVb_;         // dinâmico
    ComPtr<ID3D11VertexShader> outVs_;
    ComPtr<ID3D11PixelShader> outPs_;
    ComPtr<ID3D11InputLayout> outLayout_;
    ComPtr<ID3D11Buffer> cbuffer_;
    ComPtr<ID3D11Buffer> outCbuffer_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11BlendState> blendNormal_, blendAdd_, blendMultiply_, blendScreen_, blendOpaque_;
    ComPtr<ID3D11RasterizerState> raster_;

    std::map<std::string, CanvasTarget> canvases_;
    CanvasTarget* current_ = nullptr;

    ComPtr<ID3D11Texture2D> previewRt_;
    ComPtr<ID3D11RenderTargetView> previewRtv_;
    Readback preview_;
    bool warp_ = false;
};

} // namespace vthost
