#include "preview_widget.h"

#include "vthost/core/preview.h"

#include <QPainter>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vthost;

namespace {
constexpr int kMargin = 0;
} // namespace

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 200);
    setMouseTracking(false);
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, &PreviewWidget::poll);
    timer_.start();
}

PreviewWidget::~PreviewWidget() {
#ifdef Q_OS_WIN
    if (block_) UnmapViewOfFile(block_);
    if (mapping_) CloseHandle(static_cast<HANDLE>(mapping_));
#endif
}

bool PreviewWidget::openMapping() {
#ifdef Q_OS_WIN
    if (block_) return true;
    if (lastOpenAttempt_.isValid() && lastOpenAttempt_.elapsed() < 1000) return false;
    lastOpenAttempt_.start();
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, kPreviewName);
    if (!h) return false;
    const void* view = MapViewOfFile(h, FILE_MAP_READ, 0, 0, kPreviewBlockBytes);
    if (!view) {
        CloseHandle(h);
        return false;
    }
    mapping_ = h;
    block_ = static_cast<const unsigned char*>(view);
    return true;
#else
    return false;
#endif
}

void PreviewWidget::poll() {
    const bool wasLive = live_;
    if (openMapping()) {
        const auto* hdr = reinterpret_cast<const PreviewHeader*>(block_);
        const auto* seqPtr = reinterpret_cast<const volatile std::uint64_t*>(&hdr->sequence);
        // Seqlock: só aceita a cópia se ninguém escreveu durante a leitura.
        for (int attempt = 0; attempt < 3; ++attempt) {
            const std::uint64_t s1 = *seqPtr;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (s1 & 1u || hdr->magic != kPreviewMagic) continue;
            const std::uint64_t frameNo = hdr->frameNumber;
            if (frameNo == lastFrameNumber_) break; // nada de novo
            const int w = static_cast<int>(hdr->width);
            const int h = static_cast<int>(hdr->height);
            if (w <= 0 || h <= 0 || w > kPreviewMaxWidth || h > kPreviewMaxHeight) break;
            const int cw = static_cast<int>(hdr->canvasWidth);
            const int ch = static_cast<int>(hdr->canvasHeight);
            QImage img(w, h, QImage::Format_RGB32);
            const unsigned char* src = block_ + sizeof(PreviewHeader);
            const int row = w * 4;
            for (int y = 0; y < h; ++y) std::memcpy(img.scanLine(y), src + static_cast<std::size_t>(y) * row, static_cast<std::size_t>(row));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (*seqPtr != s1) continue; // escrito a meio: tenta outra vez
            frame_ = std::move(img);
            lastFrameNumber_ = frameNo;
            if (cw > 0 && ch > 0) {
                canvasW_ = cw;
                canvasH_ = ch;
            }
            lastFrame_.start();
            break;
        }
    }
    live_ = lastFrame_.isValid() && lastFrame_.elapsed() < 1500;
    if (live_ || wasLive != live_) update();
}

void PreviewWidget::setTimes(const QString& elapsed, const QString& remaining, const QColor& remainingColor) {
    if (elapsed == elapsed_ && remaining == remaining_ && remainingColor == remainingColor_) return;
    elapsed_ = elapsed;
    remaining_ = remaining;
    remainingColor_ = remainingColor;
    update();
}

void PreviewWidget::setCanvasSize(int w, int h) {
    if (live_ || w <= 0 || h <= 0) return; // com sinal, manda o tamanho real do motor
    canvasW_ = w;
    canvasH_ = h;
    update();
}

QRectF PreviewWidget::canvasRect() const {
    const QRectF area = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin);
    if (area.width() <= 0 || area.height() <= 0 || canvasW_ <= 0 || canvasH_ <= 0) return {};
    const double s = std::min(area.width() / canvasW_, area.height() / canvasH_);
    const double w = canvasW_ * s, h = canvasH_ * s;
    return {area.center().x() - w / 2, area.center().y() - h / 2, w, h};
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(16, 16, 16));
    const QRectF cr = canvasRect();
    if (cr.isEmpty()) return;
    if (live_ && !frame_.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(cr, frame_);
    } else {
        p.fillRect(cr, Qt::black);
        p.setPen(QColor(120, 120, 120));
        p.drawText(cr, Qt::AlignCenter, tr("No signal from the engine"));
    }

    // Tempos nos cantos (como no Mitti): caixa preta, letra branca monoespaçada.
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setBold(true);
    f.setPixelSize(std::clamp(static_cast<int>(height() / 13), 12, 40));
    p.setFont(f);
    const QFontMetrics fm(f);
    const int pad = std::max(4, fm.height() / 5);
    auto box = [&](const QString& text, bool right, const QColor& color) {
        if (text.isEmpty()) return;
        const int w = fm.horizontalAdvance(text) + pad * 2;
        const int h = fm.height() + pad;
        const QRect r(right ? width() - w - 10 : 10, 10, w, h);
        p.fillRect(r, QColor(0, 0, 0, 200));
        p.setPen(color);
        p.drawText(r, Qt::AlignCenter, text);
    };
    box(elapsed_, false, Qt::white);
    box(remaining_, true, remainingColor_);

    // Rodapé discreto: resolução e fps.
    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    p.setPen(QColor(150, 150, 150));
    QString info = QStringLiteral("%1 × %2").arg(canvasW_).arg(canvasH_);
    if (live_) info += QStringLiteral("  ·  ") + tr("output %1 fps").arg(fps_, 0, 'f', 1);
    if (live_ && mediaFps_ > 0.0) info += QStringLiteral("  ·  ") + tr("video %1 fps").arg(mediaFps_, 0, 'f', mediaFps_ == std::floor(mediaFps_) ? 0 : 2);
    p.drawText(QRectF(0, height() - 18, width() - 8, 16), Qt::AlignRight | Qt::AlignVCenter, info);
}
