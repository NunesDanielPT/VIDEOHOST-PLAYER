#include "preview_widget.h"

#include "vthost/core/preview.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <atomic>
#include <cmath>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vthost;

namespace {
const QColor kOrange(255, 136, 0);
constexpr int kMargin = 28;
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

void PreviewWidget::setSelection(const std::optional<LayerProps>& props, int mediaW, int mediaH) {
    selection_ = props;
    selMediaW_ = mediaW;
    selMediaH_ = mediaH;
    update();
}

void PreviewWidget::setCanvasSize(int w, int h) {
    if (live_ || w <= 0 || h <= 0) return; // com sinal, manda o tamanho real do motor
    canvasW_ = w;
    canvasH_ = h;
    update();
}

QRectF PreviewWidget::canvasRect() const {
    const QRectF area = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin - 18);
    if (area.width() <= 0 || area.height() <= 0 || canvasW_ <= 0 || canvasH_ <= 0) return {};
    const double s = std::min(area.width() / canvasW_, area.height() / canvasH_);
    const double w = canvasW_ * s, h = canvasH_ * s;
    return {area.center().x() - w / 2, area.center().y() - h / 2, w, h};
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Fundo riscado (zona fora do canvas).
    p.fillRect(rect(), QColor(58, 58, 58));
    p.fillRect(rect(), QBrush(QColor(70, 70, 70), Qt::BDiagPattern));

    const QRectF cr = canvasRect();
    if (cr.isEmpty()) return;

    if (live_ && !frame_.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(cr, frame_);
    } else {
        p.fillRect(cr, Qt::black);
        p.setPen(QColor(160, 160, 160));
        p.drawText(cr, Qt::AlignCenter, tr("No signal from the engine"));
    }

    // Contorno e etiqueta "Canvas".
    p.setPen(QPen(kOrange, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(cr.adjusted(-0.5, -0.5, 0.5, 0.5));
    const QString tag = tr("Canvas");
    const QFontMetrics fm(font());
    const QRectF tagRect(cr.left(), cr.top() - fm.height() - 4, fm.horizontalAdvance(tag) + 10, fm.height() + 4);
    p.fillRect(tagRect, kOrange);
    p.setPen(Qt::black);
    p.drawText(tagRect, Qt::AlignCenter, tag);

    // Contorno da layer selecionada.
    if (selection_) {
        const LayerQuad q = computeLayerQuad(*selection_, selMediaW_, selMediaH_, canvasW_, canvasH_);
        const double s = cr.width() / canvasW_;
        const double c = std::cos(q.rotationRad), sn = std::sin(q.rotationRad);
        const double local[4][2] = {{-q.halfW, -q.halfH}, {q.halfW, -q.halfH}, {q.halfW, q.halfH}, {-q.halfW, q.halfH}};
        QPolygonF poly;
        for (const auto& l : local)
            poly << QPointF(cr.left() + (q.cx + l[0] * c - l[1] * sn) * s, cr.top() + (q.cy + l[0] * sn + l[1] * c) * s);
        p.setPen(QPen(kOrange, 1.5, Qt::DashLine));
        p.drawPolygon(poly);
        p.setBrush(kOrange);
        p.setPen(Qt::NoPen);
        for (const QPointF& pt : poly) p.drawRect(QRectF(pt.x() - 3, pt.y() - 3, 6, 6));
    }

    // Rodapé: resolução e fps.
    p.setPen(QColor(190, 190, 190));
    const QString info = QStringLiteral("%1 × %2").arg(canvasW_).arg(canvasH_) +
                         (live_ ? QStringLiteral("  ·  %1 fps").arg(fps_, 0, 'f', 1) : QString());
    p.drawText(QRectF(0, height() - 20, width() - 8, 18), Qt::AlignRight | Qt::AlignVCenter, info);
    if (live_) {
        p.setBrush(QColor(220, 40, 40));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(12, height() - 11), 4, 4);
        p.setPen(QColor(220, 220, 220));
        p.drawText(QRectF(20, height() - 20, 120, 18), Qt::AlignLeft | Qt::AlignVCenter, tr("ON AIR"));
    }
}

void PreviewWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && editable_ && selection_ && canvasRect().contains(e->position())) {
        dragging_ = true;
        dragStart_ = e->position();
    }
}

void PreviewWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    const QRectF cr = canvasRect();
    if (cr.width() <= 0) return;
    const double s = canvasW_ / cr.width();
    const QPointF d = (e->position() - dragStart_) * s;
    emit layerDragged(d.x(), d.y(), false);
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_) return;
    dragging_ = false;
    const QRectF cr = canvasRect();
    if (cr.width() <= 0) return;
    const double s = canvasW_ / cr.width();
    const QPointF d = (e->position() - dragStart_) * s;
    emit layerDragged(d.x(), d.y(), true);
}
