#include "output_window.h"

#include "vthost/core/log.h"

#include <algorithm>
#include <vector>

namespace vthost {
namespace {

constexpr wchar_t kClassName[] = L"VTHOST_Output";

std::vector<RECT> monitorRects() {
    struct Ctx {
        std::vector<RECT> rects;
        RECT primary{};
        bool hasPrimary = false;
    } ctx;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(mon, &mi)) {
                if (mi.dwFlags & MONITORINFOF_PRIMARY) {
                    c->primary = mi.rcMonitor;
                    c->hasPrimary = true;
                } else {
                    c->rects.push_back(mi.rcMonitor);
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    // Ordem estável: principal = 0, os outros da esquerda para a direita.
    std::sort(ctx.rects.begin(), ctx.rects.end(), [](const RECT& a, const RECT& b) {
        return a.left != b.left ? a.left < b.left : a.top < b.top;
    });
    if (ctx.hasPrimary) ctx.rects.insert(ctx.rects.begin(), ctx.primary);
    return ctx.rects;
}

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

OutputWindow::OutputWindow(Renderer& renderer, std::string id, std::string name, int monitor)
    : renderer_(renderer), id_(std::move(id)), name_(std::move(name)), monitor_(monitor) {}

OutputWindow::~OutputWindow() {
    rtv_.Reset();
    swapChain_.Reset();
    if (hwnd_) DestroyWindow(hwnd_);
}

bool OutputWindow::create() {
    registerClassOnce();
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kClassName, widen("VTHOST - " + name_).c_str(), WS_POPUP,
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
    const auto rects = monitorRects();
    RECT r{};
    const bool exists = monitor_ >= 0 && monitor_ < static_cast<int>(rects.size());
    if (exists) {
        r = rects[static_cast<std::size_t>(monitor_)];
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        if (!onRightMonitor_) log::info("output " + id_ + " no monitor " + std::to_string(monitor_));
        onRightMonitor_ = true;
        error_.clear();
    } else {
        // Monitor em falta: janela de pré-visualização para o operador ver que algo está errado.
        RECT p = rects.empty() ? RECT{0, 0, 1920, 1080} : rects[0];
        r = {p.left + 40, p.top + 40, p.left + 40 + 640, p.top + 40 + 360};
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPED | WS_CAPTION | WS_VISIBLE);
        if (onRightMonitor_ || error_.empty())
            log::warn("output " + id_ + ": monitor " + std::to_string(monitor_) + " não encontrado");
        onRightMonitor_ = false;
        error_ = "monitor " + std::to_string(monitor_) + " não encontrado";
    }
    SetWindowPos(hwnd_, exists ? HWND_TOPMOST : HWND_NOTOPMOST, r.left, r.top, r.right - r.left,
                 r.bottom - r.top, SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
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
}

void OutputWindow::present(bool vsync) {
    if (!swapChain_ || !rtv_) return;
    renderer_.drawCanvasTo(rtv_.Get(), width_, height_);
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
        if (LOWORD(lp) == HTCLIENT) {
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
