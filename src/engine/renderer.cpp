#include "renderer.h"

#include "gpus.h"
#include "vthost/core/engine_config.h"
#include "vthost/core/paths.h"

#include "video_decoder.h"
#include "vthost/core/log.h"
#include "vthost/core/preview.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace vthost {
namespace {

// ---- Composição das layers no canvas
constexpr char kLayerShader[] = R"(
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

// ---- Layers no canvas: malha (cantos/corner pin), efeitos de cor, máscara e blend
constexpr char kLayerMeshShader[] = R"(
cbuffer LayerFx : register(b0) {
    float opacity;
    uint mode;          // 0 normal, 1 add, 2 multiply, 3 screen
    float2 flip;        // 1 = espelhar
    float4 crop;        // esquerda, direita, cima, baixo (0..1)
    float feather;
    float brightness;
    float contrast;
    float saturation;
    float gammaV;
    float hue;          // radianos
    float invert;
    float pad;
};
Texture2D tex : register(t0);
SamplerState smp : register(s0);

struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(VSIn v) {
    VSOut o;
    o.pos = float4(v.pos.x * 2.0 - 1.0, 1.0 - v.pos.y * 2.0, 0.0, 1.0);
    o.uv = v.uv;
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    float2 uv = i.uv;
    if (flip.x > 0.5) uv.x = 1.0 - uv.x;
    if (flip.y > 0.5) uv.y = 1.0 - uv.y;
    float4 c = tex.Sample(smp, uv);
    float3 rgb = c.rgb;
    if (invert > 0.5) rgb = 1.0 - rgb;
    rgb = (rgb - 0.5) * contrast + 0.5 + brightness;
    float luma = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    rgb = lerp(float3(luma, luma, luma), rgb, saturation);
    if (abs(hue) > 0.0001) {
        float3 k = float3(0.57735, 0.57735, 0.57735);
        float ca = cos(hue), sa = sin(hue);
        rgb = rgb * ca + cross(k, rgb) * sa + k * dot(k, rgb) * (1.0 - ca);
    }
    rgb = pow(saturate(rgb), 1.0 / gammaV);
    // Máscara de recorte com contorno suave.
    float f = max(feather, 0.0001);
    float m = smoothstep(crop.x, crop.x + f, i.uv.x) * smoothstep(crop.y, crop.y + f, 1.0 - i.uv.x) *
              smoothstep(crop.z, crop.z + f, i.uv.y) * smoothstep(crop.w, crop.w + f, 1.0 - i.uv.y);
    float a = saturate(c.a * opacity * m);
    if (mode == 2) return float4(lerp(float3(1, 1, 1), rgb, a), 1.0);
    return float4(rgb * a, a); // alfa pré-multiplicado
}
)";

struct alignas(16) LayerFx {
    float opacity;
    unsigned mode;
    float flip[2];
    float crop[4];
    float feather;
    float brightness;
    float contrast;
    float saturation;
    float gamma;
    float hue;
    float invert;
    float pad;
};

constexpr int kLayerGrid = 16;
constexpr UINT kLayerMaxVerts = kLayerGrid * kLayerGrid * 6;

// ---- Saídas: região do canvas + warp (malha) + soft-edge + brilho + test card
constexpr char kOutputShader[] = R"(
cbuffer OutParams : register(b0) {
    float4 region;      // x, y, w, h da região no canvas (0..1)
    float4 blend;       // esquerda, direita, cima, baixo (fração da saída)
    float smoothness;
    float gamma;
    float brightness;
    float testCard;
    float2 canvasSize;  // píxeis
    float2 pad;
};
Texture2D tex : register(t0);
SamplerState smp : register(s0);

struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(VSIn v) {
    VSOut o;
    o.pos = float4(v.pos.x * 2.0 - 1.0, 1.0 - v.pos.y * 2.0, 0.0, 1.0);
    o.uv = v.uv;
    return o;
}

float edge(float x) {
    x = saturate(x);
    float c = x < 0.5 ? 0.5 * pow(2.0 * x, smoothness) : 1.0 - 0.5 * pow(2.0 * (1.0 - x), smoothness);
    return pow(max(c, 0.0), 1.0 / gamma);
}

float lineMask(float d, float w) { return 1.0 - smoothstep(w * 0.5, w * 0.5 + 1.0, abs(d)); }

// Test card contínuo no canvas: o mesmo desenho atravessa todas as saídas,
// por isso as linhas e círculos têm de coincidir nas zonas de sobreposição.
float3 testPattern(float2 cuv) {
    float2 p = cuv * canvasSize;
    float2 cell = floor(p / 120.0);
    float checker = fmod(cell.x + cell.y, 2.0);
    float3 col = lerp(float3(0.16, 0.16, 0.16), float3(0.22, 0.22, 0.22), checker);
    // Grelha de 8 x 8 divisões.
    float2 step8 = canvasSize / 8.0;
    float2 g = p - round(p / step8) * step8;
    col = lerp(col, float3(0.85, 0.85, 0.85), max(lineMask(g.x, 2.0), lineMask(g.y, 2.0)));
    // Diagonais.
    float2 n = cuv;
    float diag = min(abs(n.x - n.y), abs(n.x - (1.0 - n.y))) * min(canvasSize.x, canvasSize.y);
    col = lerp(col, float3(0.2, 0.75, 0.9), lineMask(diag, 2.0));
    // Círculo central e cruz.
    float2 c = canvasSize * 0.5;
    float r = 0.45 * min(canvasSize.x, canvasSize.y);
    col = lerp(col, float3(1.0, 0.53, 0.0), lineMask(length(p - c) - r, 4.0));
    col = lerp(col, float3(1.0, 0.53, 0.0), max(lineMask(p.x - c.x, 4.0), lineMask(p.y - c.y, 4.0)));
    // Círculos em cada um dos 4 quadrantes.
    for (int qx = 0; qx < 2; ++qx)
        for (int qy = 0; qy < 2; ++qy) {
            float2 qc = canvasSize * float2(0.25 + 0.5 * qx, 0.25 + 0.5 * qy);
            col = lerp(col, float3(1, 1, 1), lineMask(length(p - qc) - r * 0.45, 3.0));
        }
    // Moldura do canvas.
    float border = min(min(p.x, p.y), min(canvasSize.x - p.x, canvasSize.y - p.y));
    col = lerp(col, float3(1.0, 0.53, 0.0), 1.0 - smoothstep(6.0, 7.0, border));
    return col;
}

float4 PSMain(VSOut i) : SV_Target {
    float2 cuv = region.xy + i.uv * region.zw;
    float3 rgb = testCard > 0.5 ? testPattern(cuv) : tex.Sample(smp, cuv).rgb;
    float f = 1.0;
    if (blend.x > 0) f *= edge(i.uv.x / blend.x);
    if (blend.y > 0) f *= edge((1.0 - i.uv.x) / blend.y);
    if (blend.z > 0) f *= edge(i.uv.y / blend.z);
    if (blend.w > 0) f *= edge((1.0 - i.uv.y) / blend.w);
    return float4(saturate(rgb * brightness * f), 1.0);
}
)";

struct alignas(16) Params {
    float corners[4][2];
    float opacity;
    unsigned mode;
    float pad[2];
};

struct alignas(16) OutParams {
    float region[4];
    float blend[4];
    float smoothness;
    float gamma;
    float brightness;
    float testCard;
    float canvasSize[2];
    float pad[2];
};

bool compile(const char* src, std::size_t len, const char* entry, const char* target, ComPtr<ID3DBlob>& out,
             std::string* error) {
    ComPtr<ID3DBlob> err;
    const HRESULT hr = D3DCompile(src, len, "vthost", nullptr, nullptr, entry, target,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &err);
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
    out[0][0] = x0;
    out[0][1] = y0;
    out[1][0] = x1;
    out[1][1] = y0;
    out[2][0] = x0;
    out[2][1] = y1;
    out[3][0] = x1;
    out[3][1] = y1;
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

bool makeCBuffer(ID3D11Device* dev, UINT size, ComPtr<ID3D11Buffer>& out) {
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = size;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(dev->CreateBuffer(&cbd, nullptr, &out));
}

} // namespace

bool Renderer::init(std::string* error) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    // Placa gráfica escolhida nas Definições (ou a de alto desempenho = normalmente a dedicada).
    HRESULT hr = E_FAIL;
    {
        const EngineConfig cfg = loadEngineConfig(paths::engineConfigFile());
        bool fellBack = false;
        std::string chosen;
        ComPtr<IDXGIAdapter1> adapter = pickGpu(cfg.gpu, chosen, fellBack);
        if (fellBack) gpuWarning_ = "placa gráfica \"" + cfg.gpu + "\" não encontrada: a usar " + chosen;
        if (adapter) {
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels,
                                   static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device_, nullptr, &ctx_);
            if (FAILED(hr)) log::warn("não foi possível usar " + chosen + " (hr=" + std::to_string(hr) + ")");
        }
    }
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device_, nullptr, &ctx_);
    if (FAILED(hr)) {
        log::warn("GPU indisponível (hr=" + std::to_string(hr) + "), a usar WARP (software)");
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device_, nullptr, &ctx_);
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
    adapter.As(&adapter3_); // pode falhar (WARP antigo): a VRAM fica por mostrar
    DXGI_ADAPTER_DESC ad{};
    if (SUCCEEDED(adapter->GetDesc(&ad))) {
        char name[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
        gpuName_ = warp_ ? std::string("WARP (software)") : std::string(name);
        log::info(std::string("GPU: ") + name);
        if (!gpuWarning_.empty()) log::warn(gpuWarning_);
    }

    ComPtr<ID3DBlob> vsb, psb, ovsb, opsb, lvsb, lpsb;
    if (!compile(kLayerMeshShader, sizeof(kLayerMeshShader) - 1, "VSMain", "vs_4_0", lvsb, error) ||
        !compile(kLayerMeshShader, sizeof(kLayerMeshShader) - 1, "PSMain", "ps_4_0", lpsb, error) ||
        !compile(kLayerShader, sizeof(kLayerShader) - 1, "VSMain", "vs_4_0", vsb, error) ||
        !compile(kLayerShader, sizeof(kLayerShader) - 1, "PSMain", "ps_4_0", psb, error) ||
        !compile(kOutputShader, sizeof(kOutputShader) - 1, "VSMain", "vs_4_0", ovsb, error) ||
        !compile(kOutputShader, sizeof(kOutputShader) - 1, "PSMain", "ps_4_0", opsb, error))
        return false;
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) ||
        FAILED(device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_)) ||
        FAILED(device_->CreateVertexShader(ovsb->GetBufferPointer(), ovsb->GetBufferSize(), nullptr, &outVs_)) ||
        FAILED(device_->CreatePixelShader(opsb->GetBufferPointer(), opsb->GetBufferSize(), nullptr, &outPs_)) ||
        FAILED(device_->CreateVertexShader(lvsb->GetBufferPointer(), lvsb->GetBufferSize(), nullptr, &layerVs_)) ||
        FAILED(device_->CreatePixelShader(lpsb->GetBufferPointer(), lpsb->GetBufferSize(), nullptr, &layerPs_)) ||
        FAILED(device_->CreateInputLayout(layout, 2, ovsb->GetBufferPointer(), ovsb->GetBufferSize(), &outLayout_))) {
        if (error) *error = "falha a criar shaders";
        return false;
    }
    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth = kLayerMaxVerts * sizeof(WarpVertex);
    vbd.Usage = D3D11_USAGE_DYNAMIC;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (!makeCBuffer(device_.Get(), sizeof(Params), cbuffer_) ||
        !makeCBuffer(device_.Get(), sizeof(OutParams), outCbuffer_) ||
        !makeCBuffer(device_.Get(), sizeof(LayerFx), layerCbuffer_) ||
        FAILED(device_->CreateBuffer(&vbd, nullptr, &layerVb_))) {
        if (error) *error = "falha a criar constant buffers";
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

    blendNormal_ = makeBlend(device_.Get(), true, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE,
                             D3D11_BLEND_INV_SRC_ALPHA);
    blendAdd_ = makeBlend(device_.Get(), true, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_ONE);
    blendMultiply_ = makeBlend(device_.Get(), true, D3D11_BLEND_ZERO, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_ZERO,
                               D3D11_BLEND_ONE);
    blendScreen_ = makeBlend(device_.Get(), true, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_COLOR, D3D11_BLEND_ONE,
                             D3D11_BLEND_INV_SRC_ALPHA);
    blendOpaque_ =
        makeBlend(device_.Get(), false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_ZERO);
    return true;
}

// ------------------------------------------------------------------ canvas

bool Renderer::ensureCanvas(const std::string& id, int width, int height) {
    width = std::clamp(width, 16, kMaxCanvasSize);
    height = std::clamp(height, 16, kMaxCanvasSize);
    auto it = canvases_.find(id);
    if (it != canvases_.end() && it->second.tex && it->second.w == width && it->second.h == height) return true;

    CanvasTarget t;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &t.tex)) ||
        FAILED(device_->CreateRenderTargetView(t.tex.Get(), nullptr, &t.rtv)) ||
        FAILED(device_->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv))) {
        log::error("falha a criar canvas " + id + " " + std::to_string(width) + "x" + std::to_string(height));
        return false;
    }
    t.w = width;
    t.h = height;
    current_ = nullptr;
    canvases_[id] = std::move(t);
    log::info("canvas " + id + " " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

void Renderer::keepOnlyCanvases(const std::set<std::string>& ids) {
    current_ = nullptr;
    for (auto it = canvases_.begin(); it != canvases_.end();) {
        if (!ids.count(it->first)) it = canvases_.erase(it);
        else ++it;
    }
}

void Renderer::canvasSize(const std::string& id, int& w, int& h) const {
    auto it = canvases_.find(id);
    w = it != canvases_.end() ? it->second.w : 0;
    h = it != canvases_.end() ? it->second.h : 0;
}

void Renderer::beginCanvas(const std::string& id) {
    auto it = canvases_.find(id);
    current_ = it != canvases_.end() ? &it->second : nullptr;
    if (!current_) return;
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(0, 1, &none);
    const float black[4] = {0.f, 0.f, 0.f, 0.f};
    ctx_->ClearRenderTargetView(current_->rtv.Get(), black);
    ID3D11RenderTargetView* rtv = current_->rtv.Get();
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0.f, 0.f, static_cast<float>(current_->w), static_cast<float>(current_->h), 0.f, 1.f};
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
    if (!tex.valid() || !current_ || opacity <= 0.0f) return;
    const int cw = current_->w, ch = current_->h;

    // Cantos finais (ajuste, escala, posição, rotação, corner pin) -> malha por
    // homografia, para a imagem não "partir" na diagonal com o corner pin.
    const auto corners = computeLayerCorners(props, tex.width, tex.height, cw, ch);
    OutputMapping m;
    m.mode = WarpMode::Perspective;
    for (std::size_t i = 0; i < 4; ++i) m.corners[i] = {corners[i].x / cw, corners[i].y / ch};
    bool pinned = false;
    for (const auto& c : props.cornerPin) pinned = pinned || c.x != 0.0 || c.y != 0.0;
    const std::vector<WarpVertex> verts = buildWarpGrid(m, pinned ? kLayerGrid : 1);
    if (verts.empty() || verts.size() > kLayerMaxVerts) return;

    D3D11_MAPPED_SUBRESOURCE vm{};
    if (FAILED(ctx_->Map(layerVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &vm))) return;
    std::memcpy(vm.pData, verts.data(), verts.size() * sizeof(WarpVertex));
    ctx_->Unmap(layerVb_.Get(), 0);

    LayerFx fx{};
    fx.opacity = std::clamp(opacity, 0.0f, 1.0f);
    fx.mode = static_cast<unsigned>(props.blend);
    fx.flip[0] = props.flipH ? 1.f : 0.f;
    fx.flip[1] = props.flipV ? 1.f : 0.f;
    fx.crop[0] = static_cast<float>(props.cropLeft);
    fx.crop[1] = static_cast<float>(props.cropRight);
    fx.crop[2] = static_cast<float>(props.cropTop);
    fx.crop[3] = static_cast<float>(props.cropBottom);
    fx.feather = static_cast<float>(props.feather);
    fx.brightness = static_cast<float>(props.brightness);
    fx.contrast = static_cast<float>(props.contrast);
    fx.saturation = static_cast<float>(props.saturation);
    fx.gamma = static_cast<float>(props.gamma);
    fx.hue = static_cast<float>(props.hue * 3.14159265358979323846 / 180.0);
    fx.invert = props.invert ? 1.f : 0.f;
    D3D11_MAPPED_SUBRESOURCE cm{};
    if (FAILED(ctx_->Map(layerCbuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cm))) return;
    std::memcpy(cm.pData, &fx, sizeof(fx));
    ctx_->Unmap(layerCbuffer_.Get(), 0);

    setBlend(props.blend, false);
    const UINT stride = sizeof(WarpVertex), offset = 0;
    ID3D11Buffer* vb = layerVb_.Get();
    ID3D11Buffer* cb = layerCbuffer_.Get();
    ID3D11ShaderResourceView* srv = tex.srv.Get();
    ID3D11SamplerState* smp = sampler_.Get();
    ctx_->IASetInputLayout(outLayout_.Get()); // mesmo formato de vértice das saídas
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx_->VSSetShader(layerVs_.Get(), nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetShader(layerPs_.Get(), nullptr, 0);
    ctx_->PSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetShaderResources(0, 1, &srv);
    ctx_->PSSetSamplers(0, 1, &smp);
    ctx_->RSSetState(raster_.Get());
    ctx_->Draw(static_cast<UINT>(verts.size()), 0);
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

// ------------------------------------------------------------------ saídas

bool Renderer::buildWarpMesh(const OutputMapping& mapping, WarpMesh& out) {
    // Perspetiva pura é exata com poucos triângulos? Não (interpolação afim por
    // triângulo), por isso usa-se sempre uma grelha densa.
    const int div = mapping.mode == WarpMode::None ? 1 : 48;
    const std::vector<WarpVertex> verts = buildWarpGrid(mapping, div);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = static_cast<UINT>(verts.size() * sizeof(WarpVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{verts.data(), 0, 0};
    ComPtr<ID3D11Buffer> vb;
    if (verts.empty() || FAILED(device_->CreateBuffer(&bd, &init, &vb))) return false;
    out.vb = vb;
    out.vertexCount = static_cast<UINT>(verts.size());
    return true;
}

void Renderer::drawOutput(ID3D11RenderTargetView* rtv, int width, int height, const std::string& canvasId,
                          const OutputMapping& mapping, const WarpMesh& mesh, bool testCard, bool clear) {
    if (!rtv) return;
    const float black[4] = {0.f, 0.f, 0.f, 1.f};
    if (clear) ctx_->ClearRenderTargetView(rtv, black);
    auto it = canvases_.find(canvasId);
    if (it == canvases_.end() || !mesh.vb || width <= 0 || height <= 0) return;
    const CanvasTarget& cv = it->second;

    OutParams p{};
    const double sw = mapping.srcW > 0 ? mapping.srcW : cv.w;
    const double sh = mapping.srcH > 0 ? mapping.srcH : cv.h;
    p.region[0] = static_cast<float>(mapping.srcX / cv.w);
    p.region[1] = static_cast<float>(mapping.srcY / cv.h);
    p.region[2] = static_cast<float>(sw / cv.w);
    p.region[3] = static_cast<float>(sh / cv.h);
    p.blend[0] = static_cast<float>(mapping.blendLeft / width);
    p.blend[1] = static_cast<float>(mapping.blendRight / width);
    p.blend[2] = static_cast<float>(mapping.blendTop / height);
    p.blend[3] = static_cast<float>(mapping.blendBottom / height);
    p.smoothness = static_cast<float>(mapping.blendSmoothness);
    p.gamma = static_cast<float>(mapping.blendGamma);
    p.brightness = static_cast<float>(mapping.brightness);
    p.testCard = testCard ? 1.0f : 0.0f;
    p.canvasSize[0] = static_cast<float>(cv.w);
    p.canvasSize[1] = static_cast<float>(cv.h);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx_->Map(outCbuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    std::memcpy(m.pData, &p, sizeof(p));
    ctx_->Unmap(outCbuffer_.Get(), 0);

    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
    ctx_->RSSetViewports(1, &vp);
    setBlend(BlendMode::Normal, true);
    const UINT stride = sizeof(WarpVertex), offset = 0;
    ID3D11Buffer* vb = mesh.vb.Get();
    ID3D11Buffer* cb = outCbuffer_.Get();
    ID3D11ShaderResourceView* srv = cv.srv.Get();
    ID3D11SamplerState* smp = sampler_.Get();
    ctx_->IASetInputLayout(outLayout_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx_->VSSetShader(outVs_.Get(), nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetShader(outPs_.Get(), nullptr, 0);
    ctx_->PSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetShaderResources(0, 1, &srv);
    ctx_->PSSetSamplers(0, 1, &smp);
    ctx_->RSSetState(raster_.Get());
    ctx_->Draw(mesh.vertexCount, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(0, 1, &none);
}

// ------------------------------------------------------------------ cópias para a CPU

bool Renderer::ensureReadback(Readback& rb, int w, int h) {
    if (rb.staging[0] && rb.w == w && rb.h == h) return true;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int i = 0; i < Readback::kRing; ++i) {
        rb.staging[i].Reset();
        rb.pending[i] = false;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &rb.staging[i]))) {
            rb.staging[0].Reset();
            return false;
        }
    }
    rb.w = w;
    rb.h = h;
    rb.index = 0;
    return true;
}

void Renderer::readback(Readback& rb, ID3D11Texture2D* source, const PixelSink& sink) {
    const int write = rb.index;
    ctx_->CopyResource(rb.staging[write].Get(), source);
    rb.pending[write] = true;
    rb.index = (rb.index + 1) % Readback::kRing;
    // Lê a cópia mais antiga, se a GPU já a tiver acabado (nunca espera).
    const int read = rb.index;
    if (!rb.pending[read]) return;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx_->Map(rb.staging[read].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) return;
    try {
        sink(static_cast<const std::uint8_t*>(m.pData), static_cast<int>(m.RowPitch), rb.w, rb.h);
    } catch (...) {
    }
    ctx_->Unmap(rb.staging[read].Get(), 0);
    rb.pending[read] = false;
}

void Renderer::capturePreview(const std::string& canvasId, const PixelSink& sink) {
    auto it = canvases_.find(canvasId);
    if (it == canvases_.end()) return;
    const CanvasTarget& cv = it->second;
    int w = 0, h = 0;
    previewSizeFor(cv.w, cv.h, w, h);
    if (!previewRt_ || preview_.w != w || preview_.h != h) {
        previewRt_.Reset();
        previewRtv_.Reset();
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
            return;
    }
    if (!ensureReadback(preview_, w, h)) return;

    ID3D11RenderTargetView* rtv = previewRtv_.Get();
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f};
    ctx_->RSSetViewports(1, &vp);
    const float full[4] = {0.f, 0.f, 1.f, 1.f};
    float corners[4][2];
    rectCorners(full, corners);
    setBlend(BlendMode::Normal, true);
    drawQuad(cv.srv.Get(), corners, 1.0f, 4);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(0, 1, &none);
    readback(preview_, previewRt_.Get(), sink);
}

bool Renderer::videoMemory(double& usedMb, double& budgetMb) const {
    if (!adapter3_) return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter3_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return false;
    usedMb = static_cast<double>(info.CurrentUsage) / (1024.0 * 1024.0);
    budgetMb = static_cast<double>(info.Budget) / (1024.0 * 1024.0);
    return true;
}

ID3D11RenderTargetView* Renderer::canvasRtv(const std::string& id) const {
    auto it = canvases_.find(id);
    return it == canvases_.end() ? nullptr : it->second.rtv.Get();
}

void Renderer::clearCanvas(const std::string& id) {
    auto it = canvases_.find(id);
    if (it == canvases_.end()) return;
    const float black[4] = {0.f, 0.f, 0.f, 1.f};
    ctx_->ClearRenderTargetView(it->second.rtv.Get(), black);
}

bool Renderer::uploadCanvas(const std::string& id, const std::uint8_t* bgra, int w, int h) {
    auto it = canvases_.find(id);
    if (it == canvases_.end() || !bgra || it->second.w != w || it->second.h != h) return false;
    ctx_->UpdateSubresource(it->second.tex.Get(), 0, nullptr, bgra, static_cast<UINT>(w) * 4, 0);
    return true;
}

void Renderer::captureCanvas(const std::string& canvasId, const PixelSink& sink) {
    auto it = canvases_.find(canvasId);
    if (it == canvases_.end()) return;
    CanvasTarget& cv = it->second;
    if (!ensureReadback(cv.full, cv.w, cv.h)) return;
    readback(cv.full, cv.tex.Get(), sink);
}

// ------------------------------------------------------------------ texturas de media

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

bool Renderer::deviceLost() const { return device_ && device_->GetDeviceRemovedReason() != S_OK; }

} // namespace vthost
