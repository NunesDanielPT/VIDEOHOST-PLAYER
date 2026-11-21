#include "timeline_widget.h"

#include "library_panel.h"
#include "media_cache.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace vthost;

namespace {
constexpr double kHeaderW = 130.0;
constexpr double kRulerH = 26.0;
constexpr double kSegH = 24.0;
constexpr double kTrackH = 54.0;
constexpr double kEdgePx = 6.0;
constexpr double kSnapPx = 8.0;

const QColor kBg(38, 38, 38);
const QColor kTrackA(46, 46, 46);
const QColor kTrackB(52, 52, 52);
const QColor kClip(214, 214, 74);
const QColor kSegment(110, 110, 110);
const QColor kSegmentLoop(230, 130, 30);
const QColor kPlayhead(235, 40, 40);

QString formatTime(double t) {
    if (t < 0) t = 0;
    const int total = static_cast<int>(t);
    return QStringLiteral("%1:%2").arg(total / 60, 2, 10, QLatin1Char('0')).arg(total % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

TimelineWidget::TimelineWidget(MediaCache* cache, QWidget* parent) : QWidget(parent), cache_(cache) {
    setAcceptDrops(true);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(static_cast<int>(kRulerH + kSegH + kTrackH * 2));
    repaint_.setInterval(33);
    connect(&repaint_, &QTimer::timeout, this, [this] {
        // Acompanha a cabeça de leitura se sair do ecrã.
        const double t = playhead();
        if (t > scroll_ + visibleSeconds() * 0.95 || t < scroll_) setScrollSeconds(std::max(0.0, t - visibleSeconds() * 0.1));
        update();
    });
    connect(cache_, &MediaCache::updated, this, [this](const QString&) { update(); });
}

Timeline* TimelineWidget::timeline() const {
    if (!project_) return nullptr;
    for (auto& t : project_->timelines)
        if (QString::fromStdString(t.id) == timelineId_) return &t;
    return nullptr;
}

void TimelineWidget::setProject(Project* project, const QString& timelineId) {
    project_ = project;
    timelineId_ = timelineId;
    if (Timeline* t = timeline(); !t || !t->findClip(selectedClipId_.toStdString())) selectedClipId_.clear();
    const int rows = project_ ? static_cast<int>(project_->layers.size()) : 0;
    setMinimumHeight(static_cast<int>(kRulerH + kSegH + kTrackH * std::max(rows, 1)));
    emit viewChanged();
    update();
}

void TimelineWidget::setPlayhead(double seconds, bool playing) {
    if (drag_ == Drag::Scrub) return; // o utilizador está a posicionar: o estado do motor vem atrasado
    playheadBase_ = std::max(0.0, seconds);
    playing_ = playing;
    playClock_.start();
    if (playing_) repaint_.start();
    else repaint_.stop();
    update();
}

double TimelineWidget::playhead() const {
    if (!playing_ || !playClock_.isValid()) return playheadBase_;
    return playheadBase_ + playClock_.elapsed() / 1000.0;
}

void TimelineWidget::setZoom(double pxPerSec) {
    pxPerSec_ = std::clamp(pxPerSec, 2.0, 600.0);
    emit viewChanged();
    update();
}

void TimelineWidget::setScrollSeconds(double s) {
    scroll_ = std::clamp(s, 0.0, std::max(0.0, contentSeconds() - visibleSeconds() * 0.5));
    emit viewChanged();
    update();
}

double TimelineWidget::visibleSeconds() const { return std::max(1.0, (width() - kHeaderW) / pxPerSec_); }

double TimelineWidget::contentSeconds() const {
    const Timeline* t = timeline();
    return (t ? t->duration() : 60.0) + 10.0;
}

double TimelineWidget::timeToX(double t) const { return kHeaderW + (t - scroll_) * pxPerSec_; }
double TimelineWidget::xToTime(double x) const { return std::max(0.0, scroll_ + (x - kHeaderW) / pxPerSec_); }
double TimelineWidget::rowTop(int row) const { return kRulerH + kSegH + row * kTrackH; }

int TimelineWidget::rowAt(double y) const {
    if (!project_ || y < kRulerH + kSegH) return -1;
    const int r = static_cast<int>((y - kRulerH - kSegH) / kTrackH);
    return r < static_cast<int>(project_->layers.size()) ? r : -1;
}

int TimelineWidget::clipAt(const QPointF& pos, bool* onRightEdge) const {
    const Timeline* t = timeline();
    const int row = rowAt(pos.y());
    if (!t || row < 0 || pos.x() < kHeaderW) return -1;
    const std::string& layerId = project_->layers[static_cast<std::size_t>(row)].id;
    for (int i = static_cast<int>(t->clips.size()) - 1; i >= 0; --i) {
        const Clip& c = t->clips[static_cast<std::size_t>(i)];
        if (c.layerId != layerId) continue;
        const double x0 = timeToX(c.start), x1 = timeToX(c.end());
        if (pos.x() >= x0 && pos.x() <= x1 + 2) {
            if (onRightEdge) *onRightEdge = pos.x() >= x1 - kEdgePx;
            return i;
        }
    }
    return -1;
}

int TimelineWidget::segmentAt(double x) const {
    const Timeline* t = timeline();
    if (!t) return -1;
    const double time = xToTime(x);
    for (int i = static_cast<int>(t->segments.size()) - 1; i >= 0; --i) {
        const Segment& s = t->segments[static_cast<std::size_t>(i)];
        if (time >= s.start && time < s.end) return i;
    }
    return -1;
}

double TimelineWidget::snap(double t, int ignoreClip) const {
    const Timeline* tl = timeline();
    const double tol = kSnapPx / pxPerSec_;
    double best = std::round(t * 100.0) / 100.0;
    double bestDist = tol;
    auto consider = [&](double cand) {
        const double d = std::abs(cand - t);
        if (d < bestDist) {
            bestDist = d;
            best = cand;
        }
    };
    consider(0.0);
    consider(playhead());
    if (tl) {
        for (int i = 0; i < static_cast<int>(tl->clips.size()); ++i) {
            if (i == ignoreClip) continue;
            consider(tl->clips[static_cast<std::size_t>(i)].start);
            consider(tl->clips[static_cast<std::size_t>(i)].end());
        }
        for (const auto& s : tl->segments) {
            consider(s.start);
            consider(s.end);
        }
    }
    return std::max(0.0, best);
}

QString TimelineWidget::newClipId() const {
    const Timeline* t = timeline();
    for (int i = 1;; ++i) {
        const std::string id = "c" + std::to_string(i);
        if (!t || !t->findClip(id)) return QString::fromStdString(id);
    }
}

void TimelineWidget::emitSeek(double t, bool final) {
    // O motor recomeça os descodificadores a cada seek: limita a ~8 por segundo.
    if (!final && seekThrottle_.isValid() && seekThrottle_.elapsed() < 120) return;
    seekThrottle_.start();
    emit seekRequested(t, final);
}

// ------------------------------------------------------------------ pintura

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kBg);
    const Timeline* tl = timeline();
    const int rows = project_ ? static_cast<int>(project_->layers.size()) : 0;
    const double w = width();

    // Faixas
    for (int r = 0; r < rows; ++r) {
        const QRectF track(kHeaderW, rowTop(r), w - kHeaderW, kTrackH);
        p.fillRect(track, r % 2 ? kTrackB : kTrackA);
        p.setPen(QColor(30, 30, 30));
        p.drawLine(track.bottomLeft(), track.bottomRight());
    }

    // Grelha e régua
    p.fillRect(QRectF(kHeaderW, 0, w - kHeaderW, kRulerH), QColor(28, 28, 28));
    static const double steps[] = {0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
    double step = 600;
    for (double s : steps)
        if (s * pxPerSec_ >= 70) {
            step = s;
            break;
        }
    const double t0 = std::floor(scroll_ / step) * step;
    const double tEnd = scroll_ + visibleSeconds();
    p.setFont(QFont(font().family(), 8));
    for (double t = t0; t <= tEnd + step; t += step) {
        const double x = timeToX(t);
        if (x < kHeaderW) continue;
        p.setPen(QColor(150, 150, 150));
        p.drawLine(QPointF(x, kRulerH - 10), QPointF(x, kRulerH));
        p.drawText(QPointF(x + 3, kRulerH - 12), formatTime(t));
        p.setPen(QColor(60, 60, 60));
        p.drawLine(QPointF(x, kRulerH + kSegH), QPointF(x, rowTop(rows)));
        for (int k = 1; k < 5; ++k) {
            const double xm = timeToX(t + step * k / 5.0);
            p.setPen(QColor(90, 90, 90));
            p.drawLine(QPointF(xm, kRulerH - 4), QPointF(xm, kRulerH));
        }
    }

    // Segmentos
    p.fillRect(QRectF(kHeaderW, kRulerH, w - kHeaderW, kSegH), QColor(33, 33, 33));
    if (tl) {
        for (const auto& s : tl->segments) {
            const QRectF r(timeToX(s.start) + 1, kRulerH + 3, (s.end - s.start) * pxPerSec_ - 2, kSegH - 6);
            if (r.right() < kHeaderW || r.left() > w) continue;
            QPainterPath path;
            path.addRoundedRect(r, 3, 3);
            p.fillPath(path, s.loop ? kSegmentLoop : kSegment);
            p.setPen(Qt::black);
            const QString label = (s.loop ? QStringLiteral("⟲ ") : QString()) + QString::fromStdString(s.name);
            p.drawText(r.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
        }
    }

    // Clips
    if (tl && project_) {
        p.setFont(font());
        for (const auto& c : tl->clips) {
            int row = -1;
            for (int r = 0; r < rows; ++r)
                if (project_->layers[static_cast<std::size_t>(r)].id == c.layerId) row = r;
            if (row < 0) continue;
            const QRectF r(timeToX(c.start), rowTop(row) + 3, c.duration * pxPerSec_, kTrackH - 6);
            if (r.right() < kHeaderW || r.left() > w) continue;
            p.save();
            p.setClipRect(QRectF(kHeaderW, 0, w - kHeaderW, height()));
            QPainterPath path;
            path.addRoundedRect(r, 4, 4);
            p.fillPath(path, kClip);
            const MediaItem* m = project_->findMedia(c.mediaId);
            const QString mpath = m ? QString::fromStdString(m->path) : QString();
            double textX = r.left() + 6;
            if (m) {
                const MediaInfo info = cache_->info(mpath);
                if (info.ok && !info.thumb.isNull() && r.width() > 50) {
                    const double th = r.height() - 8;
                    const double tw = th * info.thumb.width() / std::max(1, info.thumb.height());
                    p.drawImage(QRectF(r.left() + 4, r.top() + 4, tw, th), info.thumb);
                    textX += tw + 2;
                }
            }
            p.setPen(Qt::black);
            p.drawText(QRectF(textX, r.top(), r.right() - textX - 4, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                       m ? QFileInfo(mpath).fileName() : QStringLiteral("?"));
            if (QString::fromStdString(c.id) == selectedClipId_) {
                p.setPen(QPen(Qt::white, 2));
                p.drawPath(path);
            }
            p.restore();
        }
    }

    // Pré-visualização de onde vai cair a media arrastada
    if (dropPreviewRow_ >= 0 && dropPreviewTime_ >= 0) {
        const double x = timeToX(dropPreviewTime_);
        p.setPen(QPen(kClip, 2, Qt::DashLine));
        p.drawLine(QPointF(x, rowTop(dropPreviewRow_)), QPointF(x, rowTop(dropPreviewRow_) + kTrackH));
    }

    // Cabeçalhos das layers (fixos à esquerda)
    p.fillRect(QRectF(0, 0, kHeaderW, height()), QColor(42, 42, 42));
    p.setPen(QColor(200, 200, 200));
    p.drawText(QRectF(8, kRulerH, kHeaderW - 10, kSegH), Qt::AlignVCenter | Qt::AlignLeft, tr("Segments"));
    for (int r = 0; r < rows; ++r) {
        const QRectF hr(0, rowTop(r), kHeaderW, kTrackH);
        p.setPen(QColor(30, 30, 30));
        p.drawLine(hr.bottomLeft(), hr.bottomRight());
        p.setPen(QColor(220, 220, 220));
        p.drawText(hr.adjusted(10, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromStdString(project_->layers[static_cast<std::size_t>(r)].name));
    }
    p.setPen(QColor(70, 70, 70));
    p.drawLine(QPointF(kHeaderW, 0), QPointF(kHeaderW, height()));

    // Cabeça de leitura
    const double px = timeToX(playhead());
    if (px >= kHeaderW && px <= w) {
        p.setPen(QPen(kPlayhead, 2));
        p.drawLine(QPointF(px, kRulerH), QPointF(px, height()));
        QPolygonF tri;
        tri << QPointF(px - 8, 2) << QPointF(px + 8, 2) << QPointF(px, kRulerH);
        p.setBrush(kPlayhead);
        p.setPen(Qt::NoPen);
        p.drawPolygon(tri);
    }
}

// ------------------------------------------------------------------ rato

void TimelineWidget::mousePressEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (e->button() != Qt::LeftButton) return;

    if (pos.x() < kHeaderW) {
        const int row = rowAt(pos.y());
        if (row >= 0) emit layerClicked(row);
        return;
    }
    if (pos.y() < kRulerH + kSegH) {
        drag_ = Drag::Scrub;
        const double t = xToTime(pos.x());
        playheadBase_ = t;
        playClock_.start();
        emitSeek(t, false);
        update();
        return;
    }
    const int row = rowAt(pos.y());
    if (row >= 0) emit layerClicked(row);
    bool edge = false;
    const int ci = clipAt(pos, &edge);
    Timeline* tl = timeline();
    if (ci < 0 || !tl) {
        selectedClipId_.clear();
        update();
        return;
    }
    const Clip& c = tl->clips[static_cast<std::size_t>(ci)];
    selectedClipId_ = QString::fromStdString(c.id);
    if (editable_) {
        drag_ = edge ? Drag::ResizeClip : Drag::MoveClip;
        dragClip_ = ci;
        dragOriginal_ = c;
        dragGrabOffset_ = xToTime(pos.x()) - c.start;
    }
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    Timeline* tl = timeline();
    if (drag_ == Drag::None) {
        bool edge = false;
        const bool over = editable_ && clipAt(pos, &edge) >= 0;
        setCursor(over ? (edge ? Qt::SizeHorCursor : Qt::OpenHandCursor) : Qt::ArrowCursor);
        return;
    }
    if (drag_ == Drag::Scrub) {
        const double t = xToTime(pos.x());
        playheadBase_ = t;
        playClock_.start();
        emitSeek(t, false);
        update();
        return;
    }
    if (!tl || dragClip_ < 0 || dragClip_ >= static_cast<int>(tl->clips.size())) return;
    Clip& c = tl->clips[static_cast<std::size_t>(dragClip_)];
    if (drag_ == Drag::MoveClip) {
        c.start = snap(xToTime(pos.x()) - dragGrabOffset_, dragClip_);
        // Também encaixa o fim do clip.
        const double endSnap = snap(c.start + c.duration, dragClip_);
        if (std::abs(endSnap - (c.start + c.duration)) * pxPerSec_ < kSnapPx && endSnap != c.start + c.duration)
            c.start = std::max(0.0, endSnap - c.duration);
        const int row = rowAt(pos.y());
        if (row >= 0) c.layerId = project_->layers[static_cast<std::size_t>(row)].id;
        setCursor(Qt::ClosedHandCursor);
    } else {
        const double end = snap(xToTime(pos.x()), dragClip_);
        c.duration = std::max(0.1, end - c.start);
    }
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (drag_ == Drag::Scrub) {
        emitSeek(xToTime(e->position().x()), true);
    } else if (drag_ == Drag::MoveClip || drag_ == Drag::ResizeClip) {
        Timeline* tl = timeline();
        if (tl && dragClip_ >= 0 && dragClip_ < static_cast<int>(tl->clips.size())) {
            const Clip& c = tl->clips[static_cast<std::size_t>(dragClip_)];
            if (c.start != dragOriginal_.start || c.duration != dragOriginal_.duration || c.layerId != dragOriginal_.layerId)
                emit edited();
        }
    }
    drag_ = Drag::None;
    dragClip_ = -1;
    setCursor(Qt::ArrowCursor);
    emit viewChanged();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    Timeline* tl = timeline();
    const int ci = clipAt(e->position(), nullptr);
    if (tl && ci >= 0) emitSeek(tl->clips[static_cast<std::size_t>(ci)].start, true);
}

void TimelineWidget::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.0;
    if (e->modifiers() & Qt::ControlModifier) {
        // Zoom à volta do ponteiro.
        const double tAtMouse = xToTime(e->position().x());
        setZoom(pxPerSec_ * std::pow(1.2, steps));
        setScrollSeconds(tAtMouse - (e->position().x() - kHeaderW) / pxPerSec_);
    } else {
        const double dx = e->angleDelta().x() != 0 ? e->angleDelta().x() / 120.0 : steps;
        setScrollSeconds(scroll_ - dx * visibleSeconds() * 0.1);
    }
    e->accept();
}

void TimelineWidget::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && editable_ && !selectedClipId_.isEmpty()) {
        if (Timeline* tl = timeline()) {
            const std::string id = selectedClipId_.toStdString();
            std::erase_if(tl->clips, [&](const Clip& c) { return c.id == id; });
            selectedClipId_.clear();
            emit edited();
            update();
        }
        return;
    }
    QWidget::keyPressEvent(e);
}

// ------------------------------------------------------------------ segmentos

void TimelineWidget::addSegmentAt(double t) {
    Timeline* tl = timeline();
    if (!tl || !editable_) return;
    Segment s;
    s.name = tr("segment %1").arg(tl->segments.size() + 1).toStdString();
    s.start = snap(t, -1);
    s.end = s.start + 10.0;
    tl->segments.push_back(s);
    emit edited();
    update();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* e) {
    Timeline* tl = timeline();
    if (!tl || !editable_) return;
    const QPointF pos = e->pos();
    QMenu menu(this);
    if (pos.y() >= kRulerH && pos.y() < kRulerH + kSegH && pos.x() >= kHeaderW) {
        const double t = xToTime(pos.x());
        const int si = segmentAt(pos.x());
        if (si >= 0) {
            Segment& s = tl->segments[static_cast<std::size_t>(si)];
            QAction* loop = menu.addAction(tr("Loop"));
            loop->setCheckable(true);
            loop->setChecked(s.loop);
            connect(loop, &QAction::toggled, this, [this, si](bool on) {
                if (Timeline* t2 = timeline(); t2 && si < static_cast<int>(t2->segments.size())) {
                    t2->segments[static_cast<std::size_t>(si)].loop = on;
                    emit edited();
                    update();
                }
            });
            menu.addAction(tr("Edit segment..."), this, [this, si] {
                Timeline* t2 = timeline();
                if (!t2 || si >= static_cast<int>(t2->segments.size())) return;
                Segment& sg = t2->segments[static_cast<std::size_t>(si)];
                bool ok = false;
                const QString name = QInputDialog::getText(this, tr("Segment"), tr("Name:"), QLineEdit::Normal,
                                                           QString::fromStdString(sg.name), &ok);
                if (!ok) return;
                const double start = QInputDialog::getDouble(this, tr("Segment"), tr("Start (seconds):"), sg.start, 0.0,
                                                             kMaxTimelineSeconds, 2, &ok);
                if (!ok) return;
                const double end = QInputDialog::getDouble(this, tr("Segment"), tr("End (seconds):"),
                                                           std::max(sg.end, start + 0.1), start + 0.1, kMaxTimelineSeconds, 2, &ok);
                if (!ok) return;
                sg.name = name.toStdString();
                sg.start = start;
                sg.end = end;
                emit edited();
                update();
            });
            menu.addAction(tr("Delete segment"), this, [this, si] {
                Timeline* t2 = timeline();
                if (!t2 || si >= static_cast<int>(t2->segments.size())) return;
                t2->segments.erase(t2->segments.begin() + si);
                emit edited();
                update();
            });
        } else {
            menu.addAction(tr("Add segment here"), this, [this, t] { addSegmentAt(t); });
        }
    } else {
        const int ci = clipAt(pos, nullptr);
        if (ci < 0) return;
        selectedClipId_ = QString::fromStdString(tl->clips[static_cast<std::size_t>(ci)].id);
        update();
        menu.addAction(tr("Delete clip"), this, [this] {
            if (Timeline* t2 = timeline()) {
                const std::string id = selectedClipId_.toStdString();
                std::erase_if(t2->clips, [&](const Clip& c) { return c.id == id; });
                selectedClipId_.clear();
                emit edited();
                update();
            }
        });
    }
    if (!menu.isEmpty()) menu.exec(e->globalPos());
}

// ------------------------------------------------------------------ drag & drop

void TimelineWidget::dragEnterEvent(QDragEnterEvent* e) {
    if (editable_ && e->mimeData()->hasFormat(QString::fromLatin1(kMediaMime))) e->acceptProposedAction();
    else e->ignore();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* e) {
    const QPointF pos = e->position();
    const int row = rowAt(pos.y());
    if (!editable_ || row < 0 || pos.x() < kHeaderW) {
        dropPreviewRow_ = -1;
        update();
        e->ignore();
        return;
    }
    dropPreviewRow_ = row;
    dropPreviewTime_ = snap(xToTime(pos.x()), -1);
    update();
    e->acceptProposedAction();
}

void TimelineWidget::dropEvent(QDropEvent* e) {
    dropPreviewRow_ = -1;
    Timeline* tl = timeline();
    const QPointF pos = e->position();
    const int row = rowAt(pos.y());
    if (!tl || !editable_ || row < 0 || pos.x() < kHeaderW) {
        e->ignore();
        update();
        return;
    }
    const std::string mediaId = QString::fromUtf8(e->mimeData()->data(QString::fromLatin1(kMediaMime))).toStdString();
    const MediaItem* m = project_->findMedia(mediaId);
    if (!m) {
        e->ignore();
        return;
    }
    Clip c;
    c.id = newClipId().toStdString();
    c.layerId = project_->layers[static_cast<std::size_t>(row)].id;
    c.mediaId = mediaId;
    c.start = snap(xToTime(pos.x()), -1);
    c.duration = cache_->suggestedClipDuration(QString::fromStdString(m->path));
    tl->clips.push_back(c);
    selectedClipId_ = QString::fromStdString(c.id);
    e->acceptProposedAction();
    emit edited();
    update();
}
