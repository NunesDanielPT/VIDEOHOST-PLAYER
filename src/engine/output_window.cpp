#include "output_window.h"

#include <algorithm>
#include <cmath>

#include "displays.h"
#include "vthost/core/log.h"

#include <algorithm>
#include <vector>

namespace vthost {
namespace {

constexpr wchar_t kClassName[] = L"VTHOST_Output";

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void registerClassOnce() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcW; // substituído por instância em create()
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = nullptr;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    done = true;
}

} // namespace

OutputWindow::OutputWindow(Renderer& renderer, const Output& config) : renderer_(renderer), config_(config) {}

OutputWindow::~OutputWindow() {
    rtv_.Reset();
    swapChain_.Reset();
    if (hwnd_) DestroyWindow(hwnd_);
}

bool OutputWindow::create() {
    registerClassOnce();
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kClassName, widen("VTHOST - " + config_.name).c_str(), WS_POPUP,
                            0, 0, 640, 360, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        error_ = "não foi possível criar a janela";
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&OutputWindow::wndProc));
    placeOnMonitor();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    if (!createSwapChain()) return false;
    return true;
}

void OutputWindow::placeOnMonitor() {
    if (!hwnd_) return;
    const auto displays = enumerateDisplays();
    RECT primary{0, 0, 1920, 1080};
    for (const auto& d : displays)
        if (d.primary) primary = {d.x, d.y, d.x + d.width, d.y + d.height};

    if (config_.type == OutputType::Window) {
        // Janela normal: redimensionável, no ecrã principal.
        if (!onRightMonitor_) {
            RECT r{primary.left + 80, primary.top + 80, primary.left + 80 + config_.windowWidth,
                   primary.top + 80 + config_.windowHeight};
            SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
            SetWindowPos(hwnd_, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            onRightMonitor_ = true;
        }
        error_.clear();
        detail_ = "Window · " + std::to_string(config_.windowWidth) + "×" + std::to_string(config_.windowHeight);
        return;
    }

    protocol::DisplayInfo d;
    // Com um ecrã escolhido pelo nome, nunca salta para outro ecrã por engano.
    const bool found = findDisplay(displays, config_.displayId, config_.monitor, config_.displayId.empty(), d);
    RECT r{};
    if (found) {
        r = {d.x, d.y, d.x + d.width, d.y + d.height};
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        if (!onRightMonitor_) log::info("output " + config_.id + " em " + d.id + " (" + describeDisplay(d) + ")");
        onRightMonitor_ = true;
        error_.clear();
        detail_ = describeDisplay(d) + " · " + d.adapter;
        if (d.primary) detail_ += " · principal";
    } else {
        // Ecrã em falta: janela de pré-visualização para o operador ver que algo está errado.
        r = {primary.left + 40, primary.top + 40, primary.left + 40 + 640, primary.top + 40 + 360};
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPED | WS_CAPTION | WS_VISIBLE);
        const std::string which = config_.displayId.empty() ? "monitor " + std::to_string(config_.monitor) : config_.displayId;
        if (onRightMonitor_ || error_.empty()) log::warn("output " + config_.id + ": ecrã " + which + " não encontrado");
        onRightMonitor_ = false;
        error_ = "ecrã " + which + " não encontrado";
        detail_.clear();
    }
    SetWindowPos(hwnd_, found ? HWND_TOPMOST : HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

bool OutputWindow::createSwapChain() {
    RECT cr{};
    GetClientRect(hwnd_, &cr);
    width_ = std::max<LONG>(1, cr.right - cr.left);
    height_ = std::max<LONG>(1, cr.bottom - cr.top);

    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = static_cast<UINT>(width_);
    d.Height = static_cast<UINT>(height_);
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    HRESULT hr = renderer_.factory()->CreateSwapChainForHwnd(renderer_.device(), hwnd_, &d, nullptr,
                                                             nullptr, &swapChain_);
    if (FAILED(hr)) {
        // Fallback para o modelo antigo (alguns drivers/VMs).
        d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        d.BufferCount = 1;
        hr = renderer_.factory()->CreateSwapChainForHwnd(renderer_.device(), hwnd_, &d, nullptr, nullptr,
                                                         &swapChain_);
    }
    if (FAILED(hr)) {
        error_ = "falha a criar swap chain";
        ok_ = false;
        return false;
    }
    // Sem Alt+Enter: o modo de ecrã é controlado só pelo VTHOST.
    renderer_.factory()->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&back))) ||
        FAILED(renderer_.device()->CreateRenderTargetView(back.Get(), nullptr, &rtv_))) {
        error_ = "falha a obter back buffer";
        ok_ = false;
        return false;
    }
    ok_ = true;
    return true;
}

void OutputWindow::resize(int w, int h) {
    if (!swapChain_ || w <= 0 || h <= 0 || (w == width_ && h == height_)) return;
    rtv_.Reset();
    if (FAILED(swapChain_->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h),
                                         DXGI_FORMAT_UNKNOWN, 0))) {
        lost_ = true;
        return;
    }
    ComPtr<ID3D11Texture2D> back;
    if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&back))))
        renderer_.device()->CreateRenderTargetView(back.Get(), nullptr, &rtv_);
    width_ = w;
    height_ = h;
    updateLetterbox();
}

void OutputWindow::setCanvas(const std::string& canvasId, int canvasW, int canvasH) {
    if (canvasId == canvasId_ && canvasW == canvasW_ && canvasH == canvasH_ && mesh_.vb) return;
    canvasId_ = canvasId;
    canvasW_ = std::max(1, canvasW);
    canvasH_ = std::max(1, canvasH);
    updateLetterbox();
}

void OutputWindow::updateLetterbox() {
    // Ajusta o canvas à saída mantendo a proporção (barras pretas onde sobrar).
    OutputMapping m;
    if (width_ > 0 && height_ > 0) {
        const double ca = static_cast<double>(canvasW_) / canvasH_;
        const double oa = static_cast<double>(width_) / height_;
        double w = 1.0, h = 1.0;
        if (ca > oa) h = oa / ca;
        else w = ca / oa;
        const double x0 = (1.0 - w) / 2.0, y0 = (1.0 - h) / 2.0;
        if (std::abs(w - 1.0) > 1e-4 || std::abs(h - 1.0) > 1e-4) {
            m.mode = WarpMode::Perspective;
            m.corners = {{{x0, y0}, {x0 + w, y0}, {x0 + w, y0 + h}, {x0, y0 + h}}};
        }
    }
    mapping_ = m;
    WarpMesh mesh;
    if (renderer_.buildWarpMesh(mapping_, mesh)) mesh_ = mesh;
}

void OutputWindow::present(bool vsync, bool testCard) {
    if (!swapChain_ || !rtv_) return;
    if (!mesh_.vb) updateLetterbox();
    renderer_.drawOutput(rtv_.Get(), width_, height_, canvasId_, mapping_, mesh_, testCard);
    const HRESULT hr = swapChain_->Present(vsync ? 1 : 0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        lost_ = true;
        ok_ = false;
        error_ = "dispositivo gráfico perdido";
    } else {
        ok_ = SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
    }
}

LRESULT CALLBACK OutputWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<OutputWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CLOSE:
        return 0; // ignora Alt+F4 / fechar: o motor só pára por comando
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && self && self->config_.type == OutputType::Display) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_SYSCOMMAND: {
        const WPARAM cmd = wp & 0xFFF0;
        if (cmd == SC_SCREENSAVE || cmd == SC_MONITORPOWER || cmd == SC_KEYMENU) return 0;
        break;
    }
    case WM_SIZE:
        if (self) self->resize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_DISPLAYCHANGE:
        if (self) self->placeOnMonitor();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace vthost
