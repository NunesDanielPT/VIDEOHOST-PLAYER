#include "dsm.h"

#include "vthost/core/log.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vthost {

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

OutputMapping rectMapping(double x, double y, double w, double h) {
    OutputMapping m;
    m.mode = WarpMode::Perspective;
    m.corners = {{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}};
    return m;
}
} // namespace

bool DsmGeometry::update(Renderer& r, int ow, int oh, int cw, int ch) {
    if (ow == outW && oh == outH && cw == canvasW && ch == canvasH && videoMesh.vb && panelMesh.vb) return true;
    const DsmLayout l = computeDsmLayout(ow, oh, cw, ch);
    video = rectMapping(l.videoX, l.videoY, l.videoW, l.videoH);
    panel = rectMapping(l.panelX, l.panelY, l.panelW, l.panelH);
    WarpMesh vm, pm;
    if (!r.buildWarpMesh(video, vm) || !r.buildWarpMesh(panel, pm)) return false;
    videoMesh = vm;
    panelMesh = pm;
    outW = ow;
    outH = oh;
    canvasW = cw;
    canvasH = ch;
    panelW = std::clamp(static_cast<int>(std::lround(l.panelW * ow)), 16, 3840);
    panelH = std::clamp(static_cast<int>(std::lround(l.panelH * oh)), 16, 1080);
    return true;
}

void drawDsm(Renderer& r, ID3D11RenderTargetView* rtv, const DsmGeometry& g, const std::string& canvasId,
             const std::string& panelCanvasId, bool testCard) {
    r.drawOutput(rtv, g.outW, g.outH, canvasId, g.video, g.videoMesh, testCard, true);
    r.drawOutput(rtv, g.outW, g.outH, panelCanvasId, g.panel, g.panelMesh, false, false);
}

std::string dsmPanelCanvasId(const std::string& outputId) { return "dsm:" + outputId; }

// ------------------------------------------------------------------ texto (thread)

DsmPanelRenderer::DsmPanelRenderer() : thread_([this] { run(); }) {}

DsmPanelRenderer::~DsmPanelRenderer() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void DsmPanelRenderer::request(const std::string& outputId, const DsmPanelRequest& r) {
    std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
    if (!lk.owns_lock()) return;
    auto d = done_.find(outputId);
    if (d != done_.end() && d->second == r) return; // já desenhado assim
    auto p = pending_.find(outputId);
    if (p != pending_.end() && p->second == r) return;
    pending_[outputId] = r;
    lk.unlock();
    cv_.notify_one();
}

bool DsmPanelRenderer::take(const std::string& outputId, DsmPanelImage& out) {
    std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
    if (!lk.owns_lock()) return false;
    auto it = ready_.find(outputId);
    if (it == ready_.end()) return false;
    out = std::move(it->second);
    ready_.erase(it);
    return true;
}

void DsmPanelRenderer::forget() {
    std::lock_guard<std::mutex> lk(mutex_);
    done_.clear();
}

void DsmPanelRenderer::run() {
    for (;;) {
        std::string id;
        DsmPanelRequest req;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return stop_ || !pending_.empty(); });
            if (stop_) return;
            auto it = pending_.begin();
            id = it->first;
            req = it->second;
            pending_.erase(it);
        }
        DsmPanelImage img;
        try {
            const int w = std::clamp(req.width, 16, 3840), h = std::clamp(req.height, 16, 1080);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h; // de cima para baixo
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
            if (dc && bmp && bits) {
                HGDIOBJ oldBmp = SelectObject(dc, bmp);
                std::memset(bits, 0, static_cast<std::size_t>(w) * h * 4);
                SetBkMode(dc, TRANSPARENT);
                const int labelPx = std::max(8, static_cast<int>(h * 0.22));
                const int numberPx = std::max(10, static_cast<int>(h * 0.62));
                HFONT labelFont = CreateFontW(-labelPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                              DEFAULT_PITCH, L"Segoe UI");
                HFONT numberFont = CreateFontW(-numberPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                               DEFAULT_PITCH, L"Segoe UI");
                const int labelH = static_cast<int>(h * 0.30);
                const auto drawText = [&](HFONT f, COLORREF color, const std::string& text, RECT rc, UINT align) {
                    SelectObject(dc, f);
                    SetTextColor(dc, color);
                    std::wstring t = widen(text);
                    DrawTextW(dc, t.c_str(), static_cast<int>(t.size()), &rc, align | DT_SINGLELINE | DT_NOPREFIX);
                };
                const COLORREF labelColor = RGB(170, 170, 170);
                const COLORREF white = RGB(255, 255, 255);
                const COLORREF remColor =
                    RGB((req.remainingColor >> 16) & 0xFF, (req.remainingColor >> 8) & 0xFF, req.remainingColor & 0xFF);
                const int half = w / 2;
                drawText(labelFont, labelColor, req.labelLength, RECT{0, 0, half, labelH}, DT_LEFT | DT_BOTTOM);
                drawText(numberFont, white, req.length, RECT{0, labelH, half, h}, DT_LEFT | DT_VCENTER);
                drawText(labelFont, labelColor, req.labelRemaining, RECT{half, 0, w, labelH}, DT_RIGHT | DT_BOTTOM);
                drawText(numberFont, remColor, req.remaining, RECT{half, labelH, w, h}, DT_RIGHT | DT_VCENTER);
                GdiFlush();
                img.width = w;
                img.height = h;
                img.bgra.assign(static_cast<const std::uint8_t*>(bits),
                                static_cast<const std::uint8_t*>(bits) + static_cast<std::size_t>(w) * h * 4);
                for (std::size_t i = 3; i < img.bgra.size(); i += 4) img.bgra[i] = 255; // GDI deixa o alfa a 0
                SelectObject(dc, oldBmp);
                if (labelFont) DeleteObject(labelFont);
                if (numberFont) DeleteObject(numberFont);
            }
            if (bmp) DeleteObject(bmp);
            if (dc) DeleteDC(dc);
        } catch (...) {
            img = {};
        }
        std::lock_guard<std::mutex> lk(mutex_);
        if (img.width > 0) {
            ready_[id] = std::move(img);
            done_[id] = req;
        }
    }
}

} // namespace vthost
