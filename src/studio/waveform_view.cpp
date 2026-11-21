#include "waveform_view.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <iterator>
#include <cmath>

namespace {
constexpr int kRuler = 20;
const QColor kGreen(98, 200, 92);
const QColor kBlue(42, 160, 255);

QString rulerLabel(double s) {
    const int t = static_cast<int>(std::lround(s));
    if (t >= 3600) return QStringLiteral("%1h%2").arg(t / 3600).arg((t / 60) % 60, 2, 10, QLatin1Char('0'));
    if (t >= 60 && t % 60 == 0) return QStringLiteral("%1m").arg(t / 60);
    if (t >= 60) return QStringLiteral("%1m%2").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1s").arg(t);
}
} // namespace

WaveformView::WaveformView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(90);
    setCursor(Qt::PointingHandCursor);
}

void WaveformView::setData(const QVector<float>& peaks, double blocksPerSecond, double fileDuration, bool analyzing,
                           bool hasAudio) {
    peaks_ = peaks;
    bps_ = blocksPerSecond;
    duration_ = std::isfinite(fileDuration) ? std::max(0.0, fileDuration) : 0.0;
    analyzing_ = analyzing;
    hasAudio_ = hasAudio;
    update();
}

void WaveformView::setInOut(double in, double out) {
    if (drag_ == Drag::In || drag_ == Drag::Out) return; // o utilizador está a arrastar
    in_ = in;
    out_ = out;
    update();
}

void WaveformView::setPlayhead(double fileTime) {
    if (drag_ == Drag::Seek) return;
    if (std::abs(fileTime - playhead_) < 1e-3) return;
    playhead_ = fileTime;
    update();
}

QRectF WaveformView::waveRect() const { return QRectF(8, kRuler + 4, width() - 16, height() - kRuler - 10); }

double WaveformView::timeAt(double x) const {
    const QRectF r = waveRect();
    if (duration_ <= 0.0 || r.width() <= 1) return 0.0;
    return std::clamp((x - r.left()) / r.width(), 0.0, 1.0) * duration_;
}

double WaveformView::xAt(double t) const {
    const QRectF r = waveRect();
    if (duration_ <= 0.0) return r.left();
    return r.left() + std::clamp(t / duration_, 0.0, 1.0) * r.width();
}

void WaveformView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));
    const QRectF r = waveRect();
    p.fillRect(r, QColor(20, 20, 20));
    if (duration_ <= 0.0) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(r, Qt::AlignCenter, tr("Select a cue to see its audio"));
        return;
    }

    // Régua: marcas com intervalo "redondo" conforme a largura.
    const double pxPerSec = r.width() / duration_;
    static const double steps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600};
    double step = steps[std::size(steps) - 1];
    for (double s : steps)
        if (s * pxPerSec >= 60.0) {
            step = s;
            break;
        }
    p.setPen(QColor(140, 140, 140));
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    for (double t = 0.0; t <= duration_ + 1e-6; t += step) {
        const double x = xAt(t);
        p.drawLine(QPointF(x, 2), QPointF(x, kRuler));
        p.drawText(QRectF(x + 3, 2, 60, kRuler - 4), Qt::AlignLeft | Qt::AlignVCenter, rulerLabel(t));
    }

    // Onda (L em cima, R em baixo).
    const double in = std::clamp(in_, 0.0, duration_);
    const double out = std::clamp(effectiveOut(), in, duration_);
    if (!peaks_.isEmpty() && bps_ > 0.0) {
        const int blocks = static_cast<int>(peaks_.size() / 2);
        const double halfH = r.height() / 4.0;
        const float gain = static_cast<float>(std::pow(10.0, std::clamp(gainDb_, -60.0, 40.0) / 20.0));
        const double midL = r.top() + halfH, midR = r.top() + 3 * halfH;
        for (int px = 0; px < static_cast<int>(r.width()); ++px) {
            const double t0 = static_cast<double>(px) / r.width() * duration_;
            const double t1 = static_cast<double>(px + 1) / r.width() * duration_;
            const int b0 = std::clamp(static_cast<int>(t0 * bps_), 0, blocks - 1);
            const int b1 = std::clamp(static_cast<int>(t1 * bps_), b0, blocks - 1);
            float l = 0.0f, rr = 0.0f;
            for (int b = b0; b <= b1; ++b) {
                l = std::max(l, peaks_[2 * b]);
                rr = std::max(rr, peaks_[2 * b + 1]);
            }
            const bool inside = t0 >= in && t0 <= out;
            l *= gain;
            rr *= gain;
            const double x = r.left() + px + 0.5;
            const bool clip = l > 1.0f || rr > 1.0f;
            p.setPen(!inside ? QColor(70, 100, 68) : clip ? QColor(220, 70, 60) : kGreen);
            l = std::min(l, 1.0f);
            rr = std::min(rr, 1.0f);
            p.drawLine(QPointF(x, midL - l * halfH), QPointF(x, midL + l * halfH));
            p.drawLine(QPointF(x, midR - rr * halfH), QPointF(x, midR + rr * halfH));
        }
        p.setPen(QColor(60, 60, 60));
        p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
        if (!badge_.isEmpty()) {
            QFont bf = font();
            bf.setPixelSize(11);
            p.setFont(bf);
            const int w = p.fontMetrics().horizontalAdvance(badge_) + 12;
            const QRectF br(r.right() - w - 4, r.top() + 4, w, 18);
            p.fillRect(br, QColor(0, 0, 0, 170));
            p.setPen(QColor(200, 200, 200));
            p.drawText(br, Qt::AlignCenter, badge_);
            p.setFont(f);
        }
    } else {
        p.setPen(QColor(120, 120, 120));
        p.drawText(r, Qt::AlignCenter,
                   analyzing_ ? tr("Analysing audio...") : (hasAudio_ ? QString() : tr("No audio in this file")));
    }

    // Fora do In..Out escurecido; pegas In/Out.
    p.fillRect(QRectF(r.left(), r.top(), xAt(in) - r.left(), r.height()), QColor(0, 0, 0, 140));
    p.fillRect(QRectF(xAt(out), r.top(), r.right() - xAt(out), r.height()), QColor(0, 0, 0, 140));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(240, 240, 240));
    const double xi = xAt(in), xo = xAt(out);
    p.drawPolygon(QPolygonF({QPointF(xi, r.top()), QPointF(xi + 9, r.top()), QPointF(xi, r.top() + 12)}));
    p.drawPolygon(QPolygonF({QPointF(xo, r.top()), QPointF(xo - 9, r.top()), QPointF(xo, r.top() + 12)}));
    p.setPen(QPen(QColor(240, 240, 240, 160), 1));
    p.drawLine(QPointF(xi, r.top()), QPointF(xi, r.bottom()));
    p.drawLine(QPointF(xo, r.top()), QPointF(xo, r.bottom()));

    // Cabeça de leitura (só se esta cue estiver no ar).
    if (playhead_ >= 0.0) {
        const double x = xAt(playhead_);
        p.setPen(QPen(kBlue, 2));
        p.drawLine(QPointF(x, kRuler - 2), QPointF(x, r.bottom()));
        p.setPen(Qt::NoPen);
        p.setBrush(kBlue);
        p.drawPolygon(QPolygonF({QPointF(x - 7, 0), QPointF(x + 7, 0), QPointF(x, kRuler - 2)}));
    }
}

void WaveformView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || duration_ <= 0.0) return;
    const double x = e->position().x();
    const QRectF r = waveRect();
    const bool nearTop = e->position().y() <= r.top() + 16;
    if (editable_ && nearTop && std::abs(x - xAt(in_)) <= 8) drag_ = Drag::In;
    else if (editable_ && nearTop && std::abs(x - xAt(effectiveOut())) <= 8) drag_ = Drag::Out;
    else if (playhead_ >= 0.0) drag_ = Drag::Seek;
    else return;
    mouseMoveEvent(e);
}

void WaveformView::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ == Drag::None) return;
    const double t = timeAt(e->position().x());
    if (drag_ == Drag::Seek) {
        playhead_ = t;
        update();
        emit seekRequested(t, false);
    } else if (drag_ == Drag::In) {
        in_ = std::clamp(t, 0.0, std::max(0.0, effectiveOut() - 0.1));
        update();
        emit inOutChanged(in_, out_, false);
    } else {
        double o = std::clamp(t, in_ + 0.1, duration_);
        if (o >= duration_ - 0.01) o = 0.0; // até ao fim
        out_ = o;
        update();
        emit inOutChanged(in_, out_, false);
    }
}

void WaveformView::mouseReleaseEvent(QMouseEvent* e) {
    if (drag_ == Drag::None) return;
    const Drag d = drag_;
    mouseMoveEvent(e);
    drag_ = Drag::None;
    if (d == Drag::Seek) emit seekRequested(playhead_, true);
    else emit inOutChanged(in_, out_, true);
}
