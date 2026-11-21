#include "slice_canvas.h"

#include "vthost/core/mapping.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

using namespace vthost;

namespace {
constexpr double kHandle = 7.0; // raio das pegas (px do widget)
const QColor kCurrent(108, 245, 210);
const QColor kOther(150, 150, 150);

QString qs(const std::string& s) { return QString::fromStdString(s); }

double dist(const QPointF& a, const QPointF& b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }
} // namespace

SliceCanvas::SliceCanvas(Mode mode, QWidget* parent) : QWidget(parent), mode_(mode) {
    setMinimumSize(360, 220);
    setMouseTracking(true);
}

void SliceCanvas::setSourceInfo(SourceSize size, SourceName name) {
    sourceSize_ = std::move(size);
    sourceName_ = std::move(name);
    update();
}

void SliceCanvas::setOutput(Output* output, int currentSlice) {
    output_ = output;
    current_ = currentSlice;
    if (drag_ != Drag::None) drag_ = Drag::None;
    update();
}

Slice* SliceCanvas::current() const {
    if (!output_ || current_ < 0 || current_ >= static_cast<int>(output_->slices.size())) return nullptr;
    return &output_->slices[static_cast<std::size_t>(current_)];
}

QSize SliceCanvas::sourceSize(const Slice& s) const {
    QSize z = sourceSize_ ? sourceSize_(s) : QSize(1920, 1080);
    if (z.width() <= 0 || z.height() <= 0) z = QSize(1920, 1080);
    return z;
}

QSizeF SliceCanvas::spaceSize() const {
    if (mode_ == Mode::Output) return output_ ? QSizeF(std::max(1, output_->width), std::max(1, output_->height)) : QSizeF(1920, 1080);
    const Slice* s = current();
    return s ? QSizeF(sourceSize(*s)) : QSizeF(1920, 1080);
}

QRectF SliceCanvas::area() const {
    const QSizeF sz = spaceSize();
    const QRectF r = QRectF(rect()).adjusted(16, 16, -16, -16);
    const double k = std::min(r.width() / sz.width(), r.height() / sz.height());
    const QSizeF a(sz.width() * k, sz.height() * k);
    return {r.center().x() - a.width() / 2, r.center().y() - a.height() / 2, a.width(), a.height()};
}

QPointF SliceCanvas::toWidget(double x, double y) const {
    const QRectF a = area();
    const QSizeF sz = spaceSize();
    return {a.left() + x / sz.width() * a.width(), a.top() + y / sz.height() * a.height()};
}

QPointF SliceCanvas::toSpace(const QPointF& p) const {
    const QRectF a = area();
    const QSizeF sz = spaceSize();
    return {(p.x() - a.left()) / a.width() * sz.width(), (p.y() - a.top()) / a.height() * sz.height()};
}

std::array<QPointF, 4> SliceCanvas::sliceCorners(const Slice& s) const {
    const QSize src = sourceSize(s);
    const QSizeF sp(output_->width, output_->height);
    const OutputMapping m = sliceToMapping(s, output_->width, output_->height, src.width(), src.height());
    std::array<QPointF, 4> c;
    for (int i = 0; i < 4; ++i)
        c[static_cast<std::size_t>(i)] = {m.corners[static_cast<std::size_t>(i)].x * sp.width(),
                                          m.corners[static_cast<std::size_t>(i)].y * sp.height()};
    return c;
}

std::array<QPointF, 4> SliceCanvas::transformCorners(const Slice& s) const {
    // Igual a sliceToMapping (antes do espelho): pega i = canto i do retângulo.
    Slice t = s;
    t.flipH = t.flipV = false;
    return sliceCorners(t);
}

QRectF SliceCanvas::inputRect(const Slice& s) const {
    const QSize src = sourceSize(s);
    if (s.inW <= 0.0 || s.inH <= 0.0) return {0, 0, static_cast<double>(src.width()), static_cast<double>(src.height())};
    QRectF r(s.inX, s.inY, s.inW, s.inH);
    return r.intersected(QRectF(0, 0, src.width(), src.height()));
}

std::vector<QPointF> SliceCanvas::meshPoints(const Slice& s) const {
    std::vector<QPointF> pts;
    if (s.meshCols <= 0 || s.meshRows <= 0) return pts;
    const QSize src = sourceSize(s);
    OutputMapping m = sliceToMapping(s, output_->width, output_->height, src.width(), src.height());
    m.mode = WarpMode::Perspective;
    for (const auto& p : s.mesh) {
        const WarpPoint w = warpPoint(m, p.x, p.y);
        pts.emplace_back(w.x * output_->width, w.y * output_->height);
    }
    return pts;
}

void SliceCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(22, 22, 22));
    if (!output_) {
        p.setPen(kOther);
        p.drawText(rect(), Qt::AlignCenter, tr("Select a screen or a slice"));
        return;
    }
    const QRectF a = area();
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);

    if (mode_ == Mode::Output) {
        p.fillRect(a, QColor(10, 10, 10));
        p.setPen(QPen(QColor(90, 90, 90), 1));
        p.drawRect(a);
        p.setPen(QColor(110, 110, 110));
        p.drawText(a.adjusted(6, 4, -6, -4), Qt::AlignRight | Qt::AlignBottom,
                   QStringLiteral("%1 × %2").arg(output_->width).arg(output_->height));
        for (int i = 0; i < static_cast<int>(output_->slices.size()); ++i) {
            const Slice& s = output_->slices[static_cast<std::size_t>(i)];
            const bool cur = i == current_;
            const auto c = sliceCorners(s);
            QPolygonF poly;
            for (const auto& q : c) poly << toWidget(q.x(), q.y());
            QColor fill = cur ? kCurrent : kOther;
            fill.setAlpha(s.enabled ? (cur ? 70 : 40) : 15);
            p.setBrush(fill);
            p.setPen(QPen(cur ? kCurrent : kOther, cur ? 2 : 1, s.enabled ? Qt::SolidLine : Qt::DashLine));
            p.drawPolygon(poly);
            // Seta no lado de cima (mostra rotação e espelho).
            const QPointF top = (poly[0] + poly[1]) / 2;
            p.drawLine(top, (poly[0] + poly[1] + poly[2] + poly[3]) / 4);
            p.setPen(cur ? Qt::white : QColor(200, 200, 200));
            const QString label = qs(s.name) + QStringLiteral("\n") + (sourceName_ ? sourceName_(s) : QString());
            p.drawText(poly.boundingRect(), Qt::AlignCenter, label);
        }
        const Slice* s = current();
        if (s && editable_) {
            p.setBrush(kCurrent);
            p.setPen(Qt::black);
            if (tool_ == Tool::Points && s->meshCols > 0) {
                const auto pts = meshPoints(*s);
                p.setPen(QPen(kCurrent, 1));
                const int cols = s->meshCols + 1;
                for (int r = 0; r <= s->meshRows; ++r)
                    for (int c = 0; c < cols; ++c) {
                        const auto idx = static_cast<std::size_t>(r * cols + c);
                        if (idx >= pts.size()) continue;
                        const QPointF w = toWidget(pts[idx].x(), pts[idx].y());
                        if (c + 1 < cols && idx + 1 < pts.size()) p.drawLine(w, toWidget(pts[idx + 1].x(), pts[idx + 1].y()));
                        if (idx + static_cast<std::size_t>(cols) < pts.size()) {
                            const auto& d = pts[idx + static_cast<std::size_t>(cols)];
                            p.drawLine(w, toWidget(d.x(), d.y()));
                        }
                    }
                p.setPen(Qt::black);
                for (const auto& q : pts) p.drawEllipse(toWidget(q.x(), q.y()), kHandle - 2, kHandle - 2);
            } else {
                const auto c = transformCorners(*s);
                for (const auto& q : c) {
                    const QPointF w = toWidget(q.x(), q.y());
                    if (tool_ == Tool::Corners) p.drawEllipse(w, kHandle, kHandle);
                    else p.drawRect(QRectF(w.x() - kHandle + 1, w.y() - kHandle + 1, 2 * kHandle - 2, 2 * kHandle - 2));
                }
            }
        }
        return;
    }

    // Input Selection: a fonte inteira e o recorte de cada slice com a mesma fonte.
    const Slice* cur = current();
    if (!cur) {
        p.setPen(kOther);
        p.drawText(rect(), Qt::AlignCenter, tr("Select a slice"));
        return;
    }
    p.fillRect(a, QColor(40, 40, 46));
    p.setPen(QPen(QColor(90, 90, 90), 1));
    p.drawRect(a);
    const QSize src = sourceSize(*cur);
    p.setPen(QColor(150, 150, 150));
    p.drawText(a.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop,
               (sourceName_ ? sourceName_(*cur) : QString()) + QStringLiteral("  ·  %1 × %2").arg(src.width()).arg(src.height()));
    for (int i = 0; i < static_cast<int>(output_->slices.size()); ++i) {
        const Slice& s = output_->slices[static_cast<std::size_t>(i)];
        if (i != current_ && (s.source != cur->source || s.compositionId != cur->compositionId || s.layerId != cur->layerId)) continue;
        const QRectF r = inputRect(s);
        const QRectF w(toWidget(r.left(), r.top()), toWidget(r.right(), r.bottom()));
        const bool isCur = i == current_;
        QColor fill = isCur ? kCurrent : kOther;
        fill.setAlpha(isCur ? 60 : 30);
        p.setBrush(fill);
        p.setPen(QPen(isCur ? kCurrent : kOther, isCur ? 2 : 1));
        p.drawRect(w);
        p.setPen(Qt::white);
        p.drawText(w, Qt::AlignCenter, qs(s.name));
        if (isCur && editable_) {
            p.setBrush(kCurrent);
            p.setPen(Qt::black);
            const QPointF hs[8] = {w.topLeft(), w.topRight(), w.bottomRight(), w.bottomLeft(),
                                   {w.center().x(), w.top()}, {w.right(), w.center().y()},
                                   {w.center().x(), w.bottom()}, {w.left(), w.center().y()}};
            for (const auto& h : hs) p.drawRect(QRectF(h.x() - kHandle + 1, h.y() - kHandle + 1, 2 * kHandle - 2, 2 * kHandle - 2));
        }
    }
}

void SliceCanvas::mousePressEvent(QMouseEvent* e) {
    if (!output_ || e->button() != Qt::LeftButton) return;
    const QPointF pos = e->position();
    drag_ = Drag::None;

    if (mode_ == Mode::Output) {
        // Pegas do slice actual primeiro; depois escolher outro slice clicando nele.
        Slice* s = current();
        if (s && editable_) {
            if (tool_ == Tool::Points && s->meshCols > 0) {
                const auto pts = meshPoints(*s);
                for (int i = 0; i < static_cast<int>(pts.size()); ++i)
                    if (dist(toWidget(pts[static_cast<std::size_t>(i)].x(), pts[static_cast<std::size_t>(i)].y()), pos) <= kHandle + 2) {
                        drag_ = Drag::MeshPoint;
                        dragIndex_ = i;
                    }
            } else {
                const auto c = transformCorners(*s);
                for (int i = 0; i < 4; ++i)
                    if (dist(toWidget(c[static_cast<std::size_t>(i)].x(), c[static_cast<std::size_t>(i)].y()), pos) <= kHandle + 2) {
                        drag_ = Drag::Corner;
                        dragIndex_ = i;
                    }
            }
        }
        if (drag_ == Drag::None) {
            // De cima para baixo (o último desenhado fica por cima).
            int hit = -1;
            for (int i = static_cast<int>(output_->slices.size()) - 1; i >= 0 && hit < 0; --i) {
                const auto c = sliceCorners(output_->slices[static_cast<std::size_t>(i)]);
                QPolygonF poly;
                for (const auto& q : c) poly << toWidget(q.x(), q.y());
                if (poly.containsPoint(pos, Qt::OddEvenFill)) hit = i;
            }
            if (current() && hit >= 0) {
                // O slice actual tem prioridade se o clique cair nele.
                const auto c = sliceCorners(*current());
                QPolygonF poly;
                for (const auto& q : c) poly << toWidget(q.x(), q.y());
                if (poly.containsPoint(pos, Qt::OddEvenFill)) hit = current_;
            }
            if (hit != current_ && hit >= 0) {
                emit sliceClicked(hit);
                return;
            }
            if (hit < 0) return;
            if (editable_ && tool_ != Tool::Points) drag_ = Drag::Move;
        }
    } else {
        Slice* s = current();
        if (!s || !editable_) return;
        const QRectF r = inputRect(*s);
        const QRectF w(toWidget(r.left(), r.top()), toWidget(r.right(), r.bottom()));
        const QPointF hs[8] = {w.topLeft(), w.topRight(), w.bottomRight(), w.bottomLeft(),
                               {w.center().x(), w.top()}, {w.right(), w.center().y()},
                               {w.center().x(), w.bottom()}, {w.left(), w.center().y()}};
        for (int i = 0; i < 8; ++i)
            if (dist(hs[i], pos) <= kHandle + 2) {
                drag_ = Drag::Edge;
                dragIndex_ = i;
            }
        if (drag_ == Drag::None && w.contains(pos)) drag_ = Drag::Move;
        if (drag_ == Drag::None) return;
    }
    if (Slice* s = current()) dragOrig_ = *s;
    dragStart_ = toSpace(pos);
}

void SliceCanvas::mouseMoveEvent(QMouseEvent* e) {
    Slice* s = current();
    if (drag_ == Drag::None || !s || !(e->buttons() & Qt::LeftButton)) return;
    const QPointF now = toSpace(e->position());
    QPointF d = now - dragStart_;
    const bool fine = e->modifiers() & Qt::AltModifier; // Alt: movimento fino
    if (fine) d /= 10.0;
    const bool snap = e->modifiers() & Qt::ShiftModifier;
    auto snapv = [snap](double v) { return snap ? std::round(v / 10.0) * 10.0 : std::round(v); };

    if (mode_ == Mode::Output) {
        if (drag_ == Drag::Move) {
            s->x = snapv(dragOrig_.x + d.x());
            s->y = snapv(dragOrig_.y + d.y());
        } else if (drag_ == Drag::Corner && tool_ == Tool::Corners) {
            auto& o = s->cornerOffsets[static_cast<std::size_t>(dragIndex_)];
            const auto& oo = dragOrig_.cornerOffsets[static_cast<std::size_t>(dragIndex_)];
            o.x = oo.x + d.x();
            o.y = oo.y + d.y();
        } else if (drag_ == Drag::Corner) {
            // Redimensiona no referencial do slice (rodado), canto oposto fixo.
            const double rad = dragOrig_.rotation * 3.14159265358979323846 / 180.0;
            const double cs = std::cos(rad), sn = std::sin(rad);
            const QPointF target = QPointF(dragStart_ + d);
            const double lx = (target.x() - dragOrig_.x) * cs + (target.y() - dragOrig_.y) * sn;
            const double ly = -(target.x() - dragOrig_.x) * sn + (target.y() - dragOrig_.y) * cs;
            static const int sx[4] = {-1, 1, 1, -1};
            static const int sy[4] = {-1, -1, 1, 1};
            const double ox = -sx[dragIndex_] * dragOrig_.width / 2, oy = -sy[dragIndex_] * dragOrig_.height / 2;
            double w = std::max(4.0, std::abs(lx - ox));
            double h = std::max(4.0, std::abs(ly - oy));
            if (snap) {
                w = std::max(10.0, std::round(w / 10.0) * 10.0);
                h = std::max(10.0, std::round(h / 10.0) * 10.0);
            }
            const double ncx = ox + sx[dragIndex_] * w / 2, ncy = oy + sy[dragIndex_] * h / 2;
            s->width = std::round(w);
            s->height = std::round(h);
            s->x = dragOrig_.x + ncx * cs - ncy * sn;
            s->y = dragOrig_.y + ncx * sn + ncy * cs;
        } else if (drag_ == Drag::MeshPoint) {
            const QSize src = sourceSize(*s);
            OutputMapping m = sliceToMapping(*s, output_->width, output_->height, src.width(), src.height());
            m.mode = WarpMode::Perspective;
            WarpPoint ms;
            const QPointF target = dragStart_ + d;
            if (unwarpPerspective(m, {target.x() / output_->width, target.y() / output_->height}, ms) &&
                dragIndex_ < static_cast<int>(s->mesh.size())) {
                auto& pt = s->mesh[static_cast<std::size_t>(dragIndex_)];
                pt.x = std::clamp(ms.x, -4.0, 5.0);
                pt.y = std::clamp(ms.y, -4.0, 5.0);
            }
        }
    } else {
        const QSize src = sourceSize(*s);
        const QRectF r0 = inputRect(dragOrig_);
        double l = r0.left(), t = r0.top(), r = r0.right(), b = r0.bottom();
        if (drag_ == Drag::Move) {
            const double dx = std::clamp(d.x(), -l, src.width() - r);
            const double dy = std::clamp(d.y(), -t, src.height() - b);
            l += dx, r += dx, t += dy, b += dy;
        } else {
            const int i = dragIndex_;
            if (i == 0 || i == 3 || i == 7) l = std::clamp(r0.left() + d.x(), 0.0, r - 4);
            if (i == 1 || i == 2 || i == 5) r = std::clamp(r0.right() + d.x(), l + 4, static_cast<double>(src.width()));
            if (i == 0 || i == 1 || i == 4) t = std::clamp(r0.top() + d.y(), 0.0, b - 4);
            if (i == 2 || i == 3 || i == 6) b = std::clamp(r0.bottom() + d.y(), t + 4, static_cast<double>(src.height()));
        }
        s->inX = snapv(l);
        s->inY = snapv(t);
        s->inW = std::max(1.0, snapv(r) - s->inX);
        s->inH = std::max(1.0, snapv(b) - s->inY);
    }
    update();
    emit editing();
}

void SliceCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || drag_ == Drag::None) return;
    drag_ = Drag::None;
    emit edited();
}
