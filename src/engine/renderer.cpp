#include "renderer.h"

#include "video_decoder.h"
#include "vthost/core/log.h"
#include "vthost/core/preview.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vthost {
namespace {

constexpr char kShaderSource[] = R"(
cbuffer Params : register(b0) {
    float4 c01;      // cantos 0 e 1 em NDC (sup-esq, sup-dir)
    float4 c23;      // cantos 2 e 3 em NDC (inf-esq, inf-dir)
    float opacity;
    uint mode;       // 0 normal, 1 add, 2 multiply, 3 screen, 4 cópia opaca
    float2 pad;
};
Texture2D tex : register(t0);
SamplerState smp : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
    float2 uv = float2(id & 1, id >> 1);
    float2 p = id == 0 ? c01.xy : (id == 1 ? c01.zw : (id == 2 ? c23.xy : c23.zw));
    VSOut o;
    o.pos = float4(p, 0.0, 1.0);
    o.uv = uv;
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    float4 c = tex.Sample(smp, i.uv);
    float a = saturate(c.a * opacity);
    if (mode == 2) return float4(lerp(float3(1, 1, 1), c.rgb, a), 1.0);
    if (mode == 4) return float4(c.rgb, 1.0);
    return float4(c.rgb * a, a); // alfa pré-multiplicado
}
)";

struct alignas(16) Params {
    float corners[4][2];
    float opacity;
    unsigned mode;
    float pad[2];
};

bool compile(const char* entry, const char* target, ComPtr<ID3DBlob>& out, std::string* error) {
    ComPtr<ID3DBlob> err;
    const HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "vthost", nullptr, nullptr,
                                  entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &err);
    if (FAILED(hr)) {
        if (error)
            *error = std::string("shader: ") +
                     (err ? static_cast<const char*>(err->GetBufferPointer()) : "erro de compilação");
        return false;
    }
    return true;
}

// Cantos (NDC) de um retângulo alinhado em 0..1 do alvo.
void rectCorners(const float r[4], float out[4][2]) {
    const float x0 = r[0] * 2.f - 1.f, x1 = (r[0] + r[2]) * 2.f - 1.f;
    const float y0 = 1.f - r[1] * 2.f, y1 = 1.f - (r[1] + r[3]) * 2.f;
    out[0][0] = x0; out[0][1] = y0;
    out[1][0] = x1; out[1][1] = y0;
    out[2][0] = x0; out[2][1] = y1;
    out[3][0] = x1; out[3][1] = y1;
}

// Retângulo que encaixa src em dst mantendo a proporção (letterbox), em 0..1.
void fitRect(int srcW, int srcH, int dstW, int dstH, float out[4]) {
    out[0] = 0.f;
    out[1] = 0.f;
    out[2] = 1.f;
    out[3] = 1.f;
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
    const double s = std::min(static_cast<double>(dstW) / srcW, static_cast<double>(dstH) / srcH);
    const double w = srcW * s / dstW;
    const double h = srcH * s / dstH;
    out[0] = static_cast<float>((1.0 - w) / 2.0);
    out[1] = static_cast<float>((1.0 - h) / 2.0);
    out[2] = static_cast<float>(w);
    out[3] = static_cast<float>(h);
}

ComPtr<ID3D11BlendState> makeBlend(ID3D11Device* dev, bool enable, D3D11_BLEND src, D3D11_BLEND dst,
                                   D3D11_BLEND srcA, D3D11_BLEND dstA) {
    D3D11_BLEND_DESC d{};
    auto& rt = d.RenderTarget[0];
    rt.BlendEnable = enable ? TRUE : FALSE;
    rt.SrcBlend = src;
    rt.DestBlend = dst;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = srcA;
    rt.DestBlendAlpha = dstA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> s;
    dev->CreateBlendState(&d, &s);
    return s;
}

} // namespace

bool Renderer::init(std::string* error) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device_,
                                   nullptr, &ctx_);
    if (FAILED(hr)) {
        log::warn("GPU indisponível (hr=" + std::to_string(hr) + "), a usar WARP (software)");
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device_,
                               nullptr, &ctx_);
        warp_ = true;
    }
    if (FAILED(hr)) {
        if (error) *error = "não foi possível criar o dispositivo Direct3D 11";
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(device_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory_)))) {
        if (error) *error = "DXGI indisponível";
        return false;
    }
    DXGI_ADAPTER_DESC ad{};
    if (SUCCEEDED(adapter->GetDesc(&ad))) {
        char name[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
        log::info(std::string("GPU: ") + name);
    }

    ComPtr<ID3DBlob> vsb, psb;
    if (!compile("VSMain", "vs_4_0", vsb, error) || !compile("PSMain", "ps_4_0", psb, error)) return false;
    if (FAILED(device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) ||
        FAILED(device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_))) {
        if (error) *error = "falha a criar shaders";
        return false;
    }

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(Params);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&cbd, nullptr, &cbuffer_))) {
        if (error) *error = "falha a criar constant buffer";
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&sd, &sampler_);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    device_->CreateRasterizerState(&rd, &raster_);

    blendNormal_ = makeBlend(device_.Get(), true, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA,
                             D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA);
    blendAdd_ = makeBlend(device_.Get(), true, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_ONE,
                          D3D11_BLEND_ONE);
    blendMultiply_ = makeBlend(device_.Get(), true, D3D11_BLEND_ZERO, D3D11_BLEND_SRC_COLOR,
                               D3D11_BLEND_ZERO, D3D11_BLEND_ONE);
    blendScreen_ = makeBlend(device_.Get(), true, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_COLOR,
                             D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA);
    blendOpaque_ = makeBlend(device_.Get(), false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE,
                             D3D11_BLEND_ZERO);
    return true;
}

bool Renderer::resizeCanvas(int width, int height) {
    width = std::clamp(width, 16, kMaxCanvasSize);
    height = std::clamp(height, 16, kMaxCanvasSize);
    if (canvas_ && width == canvasW_ && height == canvasH_) return true;

    canvas_.Reset();
    canvasRtv_.Reset();
    canvasSrv_.Reset();

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &canvas_)) ||
        FAILED(device_->CreateRenderTargetView(canvas_.Get(), nullptr, &canvasRtv_)) ||
        FAILED(device_->CreateShaderResourceView(canvas_.Get(), nullptr, &canvasSrv_))) {
        log::error("falha a criar canvas " + std::to_string(width) + "x" + std::to_string(height));
        canvas_.Reset();
        return false;
    }
    canvasW_ = width;
    canvasH_ = height;
    log::info("canvas " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

void Renderer::beginCanvas() {
    if (!canvasRtv_) return;
    const float black[4] = {0.f, 0.f, 0.f, 0.f};
    ctx_->ClearRenderTargetView(canvasRtv_.Get(), black);
    ID3D11RenderTargetView* rtv = canvasRtv_.Get();
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0.f, 0.f, static_cast<float>(canvasW_), static_cast<float>(canvasH_), 0.f, 1.f};
    ctx_->RSSetViewports(1, &vp);
}

void Renderer::setBlend(BlendMode blend, bool opaqueCopy) {
    ID3D11BlendState* s = blendNormal_.Get();
    if (opaqueCopy) s = blendOpaque_.Get();
    else if (blend == BlendMode::Add) s = blendAdd_.Get();
    else if (blend == BlendMode::Multiply) s = blendMultiply_.Get();
    else if (blend == BlendMode::Screen) s = blendScreen_.Get();
    const float factor[4] = {0, 0, 0, 0};
    ctx_->OMSetBlendState(s, factor, 0xffffffff);
}

void Renderer::drawLayer(const GpuTexture& tex, const LayerProps& props, float extraOpacity) {
    const float opacity = static_cast<float>(props.opacity) * extraOpacity;
    if (!tex.valid() || !canvasRtv_ || opacity <= 0.0f) return;
    const LayerQuad q = computeLayerQuad(props, tex.width, tex.height, canvasW_, canvasH_);
    const double c = std::cos(q.rotationRad), s = std::sin(q.rotationRad);
    const double local[4][2] = {{-q.halfW, -q.halfH}, {q.halfW, -q.halfH}, {-q.halfW, q.halfH}, {q.halfW, q.halfH}};
    float corners[4][2];
    for (int i = 0; i < 4; ++i) {
        const double x = q.cx + local[i][0] * c - local[i][1] * s;
        const double y = q.cy + local[i][0] * s + local[i][1] * c;
        corners[i][0] = static_cast<float>(x / canvasW_ * 2.0 - 1.0);
        corners[i][1] = static_cast<float>(1.0 - y / canvasH_ * 2.0);
    }
    setBlend(props.blend, false);
    drawQuad(tex.srv.Get(), corners, std::clamp(opacity, 0.0f, 1.0f), static_cast<unsigned>(props.blend));
}

void Renderer::drawCanvasTo(ID3D11RenderTargetView* rtv, int width, int height) {
    if (!rtv) return;
    const float black[4] = {0.f, 0.f, 0.f, 1.f};
    ctx_->ClearRenderTargetView(rtv, black);
    if (!canvasSrv_) return;
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
    ctx_->RSSetViewports(1, &vp);
    float rect[4];
    fitRect(canvasW_, canvasH_, width, height, rect);
    float corners[4][2];
    rectCorners(rect, corners);
    setBlend(BlendMode::Normal, true);
    drawQuad(canvasSrv_.Get(), corners, 1.0f, 4);
    // Desliga o canvas como SRV antes de voltar a ser render target no frame seguinte.
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(0, 1, &none);
}

bool Renderer::ensurePreviewTargets(int w, int h) {
    if (previewRt_ && w == previewW_ && h == previewH_) return true;
    previewRt_.Reset();
    previewRtv_.Reset();
    for (int i = 0; i < kPreviewRing; ++i) {
        previewStaging_[i].Reset();
        previewPending_[i] = false;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &previewRt_)) ||
        FAILED(device_->CreateRenderTargetView(previewRt_.Get(), nullptr, &previewRtv_)))
        return false;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int i = 0; i < kPreviewRing; ++i)
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &previewStaging_[i]))) {
            previewRt_.Reset();
            return false;
        }
    previewW_ = w;
    previewH_ = h;
    return true;
}

void Renderer::capturePreview(const PreviewSink& sink) {
    if (!canvasSrv_) return;
    int w = 0, h = 0;
    previewSizeFor(canvasW_, canvasH_, w, h);
    if (!ensurePreviewTargets(w, h)) return;

    // 1) Reduz o canvas para o alvo do preview e copia para a próxima staging.
    ID3D11RenderTargetView* rtv = previewRtv_.Get();
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f};
    ctx_->RSSetViewports(1, &vp);
    const float full[4] = {0.f, 0.f, 1.f, 1.f};
    float corners[4][2];
    rectCorners(full, corners);
    setBlend(BlendMode::Normal, true);
    drawQuad(canvasSrv_.Get(), corners, 1.0f, 4);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(0, 1, &none);

    const int write = previewIndex_;
    ctx_->CopyResource(previewStaging_[write].Get(), previewRt_.Get());
    previewPending_[write] = true;
    previewIndex_ = (previewIndex_ + 1) % kPreviewRing;

    // 2) Lê a staging mais antiga, se a GPU já a tiver acabado (sem esperar).
    const int read = previewIndex_; // a próxima a ser escrita = a mais antiga
    if (!previewPending_[read]) return;
    D3D11_MAPPED_SUBRESOURCE m{};
    const HRESULT hr = ctx_->Map(previewStaging_[read].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING || FAILED(hr)) return;
    try {
        sink(static_cast<const std::uint8_t*>(m.pData), static_cast<int>(m.RowPitch), w, h);
    } catch (...) {
    }
    ctx_->Unmap(previewStaging_[read].Get(), 0);
    previewPending_[read] = false;
}

void Renderer::drawQuad(ID3D11ShaderResourceView* srv, const float corners[4][2], float opacity, unsigned mode) {
    Params p{};
    std::memcpy(p.corners, corners, sizeof(p.corners));
    p.opacity = opacity;
    p.mode = mode;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx_->Map(cbuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    std::memcpy(m.pData, &p, sizeof(p));
    ctx_->Unmap(cbuffer_.Get(), 0);

    ID3D11Buffer* cb = cbuffer_.Get();
    ID3D11SamplerState* smp = sampler_.Get();
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetShader(ps_.Get(), nullptr, 0);
    ctx_->PSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetShaderResources(0, 1, &srv);
    ctx_->PSSetSamplers(0, 1, &smp);
    ctx_->RSSetState(raster_.Get());
    ctx_->Draw(4, 0);
}

bool Renderer::upload(GpuTexture& tex, const VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.bgra.size() < static_cast<std::size_t>(frame.width) * frame.height * 4)
        return false;

    if (!tex.texture || tex.width != frame.width || tex.height != frame.height) {
        tex = {};
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(frame.width);
        td.Height = static_cast<UINT>(frame.height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &tex.texture)) ||
            FAILED(device_->CreateShaderResourceView(tex.texture.Get(), nullptr, &tex.srv))) {
            tex = {};
            return false;
        }
        tex.width = frame.width;
        tex.height = frame.height;
    }

    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx_->Map(tex.texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    const std::size_t rowBytes = static_cast<std::size_t>(frame.width) * 4;
    auto* dst = static_cast<std::uint8_t*>(m.pData);
    const std::uint8_t* src = frame.bgra.data();
    for (int y = 0; y < frame.height; ++y) std::memcpy(dst + y * m.RowPitch, src + y * rowBytes, rowBytes);
    ctx_->Unmap(tex.texture.Get(), 0);
    return true;
}

bool Renderer::deviceLost() const {
    return device_ && device_->GetDeviceRemovedReason() != S_OK;
}

} // namespace vthost
