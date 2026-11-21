#include "playlist_view.h"

#include "media_cache.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

#include <algorithm>
#include <cmath>

using namespace vthost;

namespace {
constexpr int kRowHeight = 96;
constexpr int kThumbX = 22, kThumbY = 28, kThumbW = 112, kThumbH = 63;
constexpr int kInfoX = kThumbX + kThumbW + 12;
const QColor kBlue(42, 160, 255);
const QColor kIconOff(110, 110, 110);

QColor cueColor(const std::string& c) {
    if (c == "red") return {232, 76, 61};
    if (c == "yellow") return {241, 196, 15};
    if (c == "green") return {46, 204, 113};
    if (c == "purple") return {155, 89, 182};
    if (c == "pink") return {232, 67, 147};
    return {};
}

QString qs(const std::string& s) { return QString::fromStdString(s); }
} // namespace

QString formatTimecode(double seconds, double fps) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    if (!(fps > 1.0) || fps > 240.0) fps = 25.0;
    const int nominal = static_cast<int>(std::lround(fps));
    long long totalFrames = static_cast<long long>(std::floor(seconds * fps + 1e-6));
    const long long frames = totalFrames % nominal;
    const long long total = static_cast<long long>(std::floor(seconds + 1e-9));
    return QStringLiteral("%1:%2:%3:%4")
        .arg(total / 3600, 2, 10, QLatin1Char('0'))
        .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'))
        .arg(frames, 2, 10, QLatin1Char('0'));
}

void paintCueIcon(QPainter* p, const QRectF& r, CueIcon icon, bool on) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QColor c = on ? kBlue : kIconOff;
    QPen pen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    const QRectF b = r.adjusted(4, 4, -4, -4);
    const double l = b.left(), t = b.top(), w = b.width(), h = b.height(), rr = b.right(), bt = b.bottom();
    switch (icon) {
    case CueIcon::Audio: {
        QPainterPath sp;
        sp.moveTo(l, t + h * 0.35);
        sp.lineTo(l + w * 0.25, t + h * 0.35);
        sp.lineTo(l + w * 0.5, t);
        sp.lineTo(l + w * 0.5, bt);
        sp.lineTo(l + w * 0.25, t + h * 0.65);
        sp.lineTo(l, t + h * 0.65);
        sp.closeSubpath();
        p->setBrush(c);
        p->drawPath(sp);
        p->setBrush(Qt::NoBrush);
        if (on) {
            p->drawArc(QRectF(l + w * 0.35, t + h * 0.2, w * 0.4, h * 0.6), -50 * 16, 100 * 16);
            p->drawArc(QRectF(l + w * 0.3, t, w * 0.7, h), -50 * 16, 100 * 16);
        } else {
            p->drawLine(QPointF(l + w * 0.65, t + h * 0.3), QPointF(rr, bt - h * 0.3));
            p->drawLine(QPointF(rr, t + h * 0.3), QPointF(l + w * 0.65, bt - h * 0.3));
        }
        break;
    }
    case CueIcon::PauseStart: {
        p->drawLine(QPointF(l + 1, t), QPointF(l + 1, bt));
        QPolygonF tri({QPointF(l + w * 0.35, t), QPointF(rr, t + h / 2), QPointF(l + w * 0.35, bt)});
        p->setBrush(c);
        p->drawPolygon(tri);
        break;
    }
    case CueIcon::PauseEnd: {
        p->drawLine(QPointF(rr - 1, t), QPointF(rr - 1, bt));
        QPolygonF tri({QPointF(l, t), QPointF(l + w * 0.65, t + h / 2), QPointF(l, bt)});
        p->setBrush(c);
        p->drawPolygon(tri);
        break;
    }
    case CueIcon::FadeIn:
        p->drawPolyline(QPolygonF({QPointF(l, bt), QPointF(l + w * 0.3, bt), QPointF(l + w * 0.7, t), QPointF(rr, t)}));
        break;
    case CueIcon::FadeOut:
        p->drawPolyline(QPolygonF({QPointF(l, t), QPointF(l + w * 0.3, t), QPointF(l + w * 0.7, bt), QPointF(rr, bt)}));
        break;
    case CueIcon::Loop: {
        p->drawArc(b.adjusted(1, 1, -1, -1), 40 * 16, 290 * 16);
        const QPointF tip(b.center().x() + w * 0.38, b.center().y() - h * 0.32);
        p->drawLine(tip, tip + QPointF(-w * 0.3, -h * 0.02));
        p->drawLine(tip, tip + QPointF(0, h * 0.3));
        break;
    }
    case CueIcon::Crossfade:
        p->drawLine(QPointF(l, bt), QPointF(rr, t));
        p->drawLine(QPointF(l, t), QPointF(rr, bt));
        p->drawLine(QPointF(rr - w * 0.3, t), QPointF(rr, t));
        p->drawLine(QPointF(rr - w * 0.3, bt), QPointF(rr, bt));
        break;
    case CueIcon::Clear: {
        QPolygonF tri({QPointF(l, t + h * 0.6), QPointF(l + w / 2, t), QPointF(rr, t + h * 0.6)});
        p->setBrush(c);
        p->drawPolygon(tri);
        p->drawLine(QPointF(l, bt), QPointF(rr, bt));
        break;
    }
    case CueIcon::Goto: {
        const double cx = l + w / 2;
        p->drawLine(QPointF(cx, bt), QPointF(cx, t + 1));
        p->drawLine(QPointF(cx, t), QPointF(l + 1, t + h * 0.45));
        p->drawLine(QPointF(cx, t), QPointF(rr - 1, t + h * 0.45));
        break;
    }
    case CueIcon::Count: break;
    }
    p->restore();
}

// ------------------------------------------------------------------ PlaylistView

PlaylistView::PlaylistView(MediaCache* media, QWidget* parent) : QListWidget(parent), media_(media) {
    setItemDelegate(new CueDelegate(this));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setUniformItemSizes(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setMouseTracking(true);
    setStyleSheet(QStringLiteral("QListWidget { background: #232323; border: none; }"));
}

void PlaylistView::setProject(const Project* project) {
    project_ = project;
    const int sel = selectedIndex();
    QString selId;
    if (sel >= 0 && currentItem()) selId = currentItem()->data(Qt::UserRole).toString();
    const QSignalBlocker block(this);
    clear();
    if (!project_) return;
    for (const auto& q : project_->cues) {
        auto* it = new QListWidgetItem(this);
        it->setData(Qt::UserRole, qs(q.id));
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        if (qs(q.id) == selId) setCurrentItem(it);
    }
}

void PlaylistView::refresh() { viewport()->update(); }

void PlaylistView::setPlaying(int index, double progress, const QString& state) {
    if (index == playing_ && std::abs(progress - progress_) < 0.001 && state == playingState_) return;
    playing_ = index;
    progress_ = progress;
    playingState_ = state;
    viewport()->update();
}

int PlaylistView::selectedIndex() const { return currentRow(); }

void PlaylistView::selectIndex(int index) {
    if (index < 0 || index >= count()) return;
    setCurrentRow(index);
    scrollToItem(item(index));
}

double PlaylistView::startOf(int index) const {
    if (!project_) return 0.0;
    double t = 0.0;
    for (int i = 0; i < index && i < static_cast<int>(project_->cues.size()); ++i) {
        const Cue& q = project_->cues[static_cast<std::size_t>(i)];
        t += q.totalSeconds(media_->info(qs(q.path)).duration);
    }
    return t;
}

void PlaylistView::dragEnterEvent(QDragEnterEvent* e) {
    if (!editable_) return e->ignore();
    if (e->mimeData()->hasUrls() || e->source() == this) e->acceptProposedAction();
    else e->ignore();
}

void PlaylistView::dragMoveEvent(QDragMoveEvent* e) {
    if (!editable_) return e->ignore();
    if (e->mimeData()->hasUrls()) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
        return;
    }
    if (e->source() == this) {
        QListWidget::dragMoveEvent(e); // indicador de onde vai cair
        e->setDropAction(Qt::MoveAction);
        e->accept();
        return;
    }
    e->ignore();
}

void PlaylistView::dropEvent(QDropEvent* e) {
    if (!editable_) return e->ignore();
    const QPoint pos = e->position().toPoint();
    int row = indexAt(pos).row();
    if (row >= 0 && pos.y() > visualItemRect(item(row)).center().y()) ++row; // metade de baixo: depois
    if (row < 0) row = count();
    if (e->mimeData()->hasUrls()) {
        QStringList files;
        for (const QUrl& u : e->mimeData()->urls())
            if (u.isLocalFile()) files << u.toLocalFile();
        e->acceptProposedAction();
        if (!files.isEmpty()) emit filesDropped(row, files);
        return;
    }
    if (e->source() == this) {
        const int from = currentRow();
        // Não deixa o QListWidget mexer nas linhas: o projecto é que manda.
        e->setDropAction(Qt::IgnoreAction);
        e->accept();
        if (from >= 0) {
            int to = row > from ? row - 1 : row;
            to = std::clamp(to, 0, count() - 1);
            if (to != from) emit cueMoved(from, to);
        }
        return;
    }
    e->ignore();
}

void PlaylistView::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && currentRow() >= 0) {
        emit playRequested(currentRow());
        return;
    }
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && currentRow() >= 0 && editable_) {
        emit deleteRequested(currentRow());
        return;
    }
    QListWidget::keyPressEvent(e);
}

void PlaylistView::mouseDoubleClickEvent(QMouseEvent* e) {
    const QModelIndex idx = indexAt(e->position().toPoint());
    if (!idx.isValid()) return;
    // Duplo clique num ícone não dispara a cue.
    const QRect row = visualRect(idx);
    for (int i = 0; i < static_cast<int>(CueIcon::Count); ++i)
        if (CueDelegate::iconRect(row, i).contains(e->position().toPoint())) return;
    emit playRequested(idx.row());
}

void PlaylistView::contextMenuEvent(QContextMenuEvent* e) {
    const QModelIndex idx = indexAt(e->pos());
    if (idx.isValid()) setCurrentRow(idx.row());
    emit contextMenuRequested(idx.isValid() ? idx.row() : -1, e->globalPos());
}

// ------------------------------------------------------------------ CueDelegate

CueDelegate::CueDelegate(PlaylistView* view) : QStyledItemDelegate(view), view_(view) {}

QSize CueDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const { return {300, kRowHeight}; }

QRect CueDelegate::iconRect(const QRect& row, int i) {
    return {row.left() + kInfoX + i * 25, row.top() + 62, 23, 22};
}

void CueDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    const Project* pr = view_->project_;
    const int i = idx.row();
    if (!pr || i < 0 || i >= static_cast<int>(pr->cues.size())) return;
    const Cue& q = pr->cues[static_cast<std::size_t>(i)];
    const QRect r = opt.rect;
    const bool playing = i == view_->playing_;
    const bool selected = opt.state & QStyle::State_Selected;
    const MediaInfo info = view_->media_->info(qs(q.path));
    const bool missing = info.ready && !info.ok && info.error == QStringLiteral("missing");

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QColor bg(38, 38, 38);
    if (!q.color.empty()) bg = QColor(56, 50, 30); // linha com cor de etiqueta fica ligeiramente tingida
    if (selected) bg = QColor(58, 58, 64);
    p->fillRect(r, bg);
    p->setPen(QColor(25, 25, 25));
    p->drawLine(r.bottomLeft(), r.bottomRight());
    if (playing) p->fillRect(QRect(r.left(), r.top(), 4, r.height()), kBlue);

    // Linha 1: número ou ID, cor, nome.
    QFont f = opt.font;
    f.setPixelSize(13);
    QFont bold = f;
    bold.setBold(true);
    p->setFont(bold);
    const QString number = q.label.empty() ? QString::number(i + 1) : qs(q.label);
    p->setPen(playing ? kBlue : QColor(200, 200, 200));
    p->drawText(QRect(r.left() + 8, r.top() + 5, 60, 18), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(bold).elidedText(number, Qt::ElideRight, 60));
    int x = r.left() + kInfoX;
    if (const QColor cc = cueColor(q.color); cc.isValid()) {
        p->fillRect(QRect(r.left() + kInfoX - 18, r.top() + 9, 11, 11), cc);
    }
    p->setPen(missing ? QColor(230, 90, 90) : (playing ? kBlue : QColor(235, 235, 235)));
    const QString name = qs(q.displayName());
    p->drawText(QRect(x, r.top() + 5, r.width() - kInfoX - 10, 18), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(bold).elidedText(missing ? name + QStringLiteral("  (?)") : name, Qt::ElideMiddle,
                                              r.width() - kInfoX - 14));

    // Miniatura.
    const QRect th(r.left() + kThumbX, r.top() + kThumbY, kThumbW, kThumbH);
    p->fillRect(th, Qt::black);
    if (!info.thumb.isNull()) {
        const QSize sz = info.thumb.size().scaled(th.size(), Qt::KeepAspectRatio);
        p->drawImage(QRect(th.center().x() - sz.width() / 2, th.center().y() - sz.height() / 2, sz.width(), sz.height()),
                     info.thumb);
    } else if (info.ready && info.ok && info.hasAudio) {
        p->setPen(QColor(120, 120, 120));
        paintCueIcon(p, QRectF(th.center().x() - 14, th.center().y() - 12, 28, 24), CueIcon::Audio, false);
    }
    if (playing) {
        p->fillRect(QRect(th.left(), th.bottom() - 3, th.width(), 4), QColor(0, 0, 0, 180));
        p->fillRect(QRect(th.left(), th.bottom() - 3, static_cast<int>(th.width() * std::clamp(view_->progress_, 0.0, 1.0)), 4),
                    kBlue);
    }

    // Linha 2: Start, TRT, repetições.
    p->setFont(f);
    const double fps = info.fps > 1.0 ? info.fps : 25.0;
    const double pass = q.passSeconds(info.duration);
    const double total = q.playCount > 0 ? pass * q.playCount : pass;
    QString line = QStringLiteral("Start: %1    TRT: %2")
                       .arg(formatTimecode(view_->startOf(i), fps), formatTimecode(total, fps));
    if (q.playCount != 1) line += QStringLiteral("    ") + PlaylistView::tr("Plays: %1").arg(q.playCount == 0 ? QStringLiteral("∞") : QString::number(q.playCount));
    if (q.speed != 1.0) line += QStringLiteral("    %1%").arg(q.speed * 100.0, 0, 'f', 0);
    p->setPen(QColor(150, 150, 150));
    p->drawText(QRect(x, r.top() + 30, r.width() - kInfoX - 10, 18), Qt::AlignLeft | Qt::AlignVCenter, line);
    if (playing && !view_->playingState_.isEmpty() && view_->playingState_ != QStringLiteral("playing")) {
        p->setPen(kBlue);
        p->drawText(QRect(x, r.top() + 44, r.width() - kInfoX - 10, 16), Qt::AlignLeft | Qt::AlignVCenter,
                    view_->playingState_ == QStringLiteral("paused")  ? PlaylistView::tr("Paused")
                    : view_->playingState_ == QStringLiteral("ended") ? PlaylistView::tr("Holding last frame")
                    : view_->playingState_ == QStringLiteral("loading") ? PlaylistView::tr("Loading...")
                                                                        : view_->playingState_);
    }

    // Linha 3: ícones (fundo escuro como no Mitti).
    const QRect bar(iconRect(r, 0).left() - 3, iconRect(r, 0).top() - 1, 25 * static_cast<int>(CueIcon::Count) + 4, 24);
    p->setPen(QColor(70, 70, 70));
    p->setBrush(QColor(28, 28, 28));
    p->drawRoundedRect(bar, 4, 4);
    const bool states[] = {!q.muted,          q.pauseAtStart, q.fadeIn > 0.0,   q.playCount != 1, q.crossfade > 0.0,
                           q.fadeOut > 0.0,   q.pauseAtEnd,   q.clearAtEnd,     !q.gotoCue.empty()};
    for (int k = 0; k < static_cast<int>(CueIcon::Count); ++k)
        paintCueIcon(p, iconRect(r, k), static_cast<CueIcon>(k), states[k]);
    if (const int g = pr->cueIndex(q.gotoCue); g >= 0) {
        const Cue& tq = pr->cues[static_cast<std::size_t>(g)];
        const QString target = tq.label.empty() ? QString::number(g + 1) : qs(tq.label);
        const QRect tag(bar.right() + 6, bar.top() + 3, QFontMetrics(bold).horizontalAdvance(target) + 10, 18);
        p->setPen(Qt::NoPen);
        p->setBrush(kBlue);
        p->drawRoundedRect(tag, 3, 3);
        p->setFont(bold);
        p->setPen(Qt::black);
        p->drawText(tag, Qt::AlignCenter, target);
    }
    p->restore();
}

bool CueDelegate::editorEvent(QEvent* e, QAbstractItemModel*, const QStyleOptionViewItem& opt, const QModelIndex& idx) {
    if (e->type() != QEvent::MouseButtonRelease) return false;
    auto* me = static_cast<QMouseEvent*>(e);
    if (me->button() != Qt::LeftButton) return false;
    for (int k = 0; k < static_cast<int>(CueIcon::Count); ++k)
        if (iconRect(opt.rect, k).contains(me->position().toPoint())) {
            if (!view_->editable_) return true;
            emit view_->iconClicked(idx.row(), static_cast<CueIcon>(k), me->globalPosition().toPoint());
            return true;
        }
    return false;
}
