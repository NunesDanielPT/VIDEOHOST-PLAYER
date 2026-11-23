#include "playlist_view.h"

#include <set>

#include <QHelpEvent>
#include <QToolTip>

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
constexpr int kBlockHeight = 34;
constexpr int kKindRole = Qt::UserRole + 2;   // 0 cue, 1 bloco
constexpr int kIndexRole = Qt::UserRole + 3;  // índice da cue / do bloco
constexpr int kNumberRole = Qt::UserRole + 4; // número do bloco (#1, #2...)
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
    setUniformItemSizes(false); // cabeçalhos de bloco são mais baixos
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setMouseTracking(true);
    setStyleSheet(QStringLiteral("QListWidget { background: #232323; border: none; }"));
}

void PlaylistView::setProject(const Project* project) {
    project_ = project;
    const int sel = selectedIndex();
    QString selId;
    if (sel >= 0 && currentItem()) selId = currentItem()->data(Qt::UserRole).toString();
    const QString selBlock = sel < 0 ? selectedBlock() : QString();
    const QSignalBlocker block(this);
    clear();
    if (!project_) return;
    int blockNumber = 0;
    std::set<std::string> shown;
    auto addHeader = [&](const vthost::Block& b) {
        auto* it = new QListWidgetItem(this);
        it->setData(Qt::UserRole, qs(b.id));
        it->setData(kKindRole, 1);
        it->setData(kIndexRole, static_cast<int>(&b - project_->blocks.data()));
        it->setData(kNumberRole, ++blockNumber);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (qs(b.id) == selBlock) setCurrentItem(it);
        shown.insert(b.id);
    };
    std::string current;
    for (std::size_t i = 0; i < project_->cues.size(); ++i) {
        const auto& q = project_->cues[i];
        const vthost::Block* b = project_->findBlock(q.block);
        if (b && q.block != current) addHeader(*b);
        current = b ? q.block : std::string();
        auto* it = new QListWidgetItem(this);
        it->setData(Qt::UserRole, qs(q.id));
        it->setData(kKindRole, 0);
        it->setData(kIndexRole, static_cast<int>(i));
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        if (b && b->collapsed) it->setHidden(true);
        if (qs(q.id) == selId) setCurrentItem(it);
    }
    // Blocos ainda sem cues: no fim (larga ficheiros no cabeçalho para os encher).
    for (const auto& b : project_->blocks)
        if (!shown.count(b.id)) addHeader(b);
}

int PlaylistView::cueAt(int row) const {
    const QListWidgetItem* it = row >= 0 && row < count() ? item(row) : nullptr;
    return it && it->data(kKindRole).toInt() == 0 ? it->data(kIndexRole).toInt() : -1;
}

QString PlaylistView::blockAt(int row) const {
    const QListWidgetItem* it = row >= 0 && row < count() ? item(row) : nullptr;
    return it && it->data(kKindRole).toInt() == 1 ? it->data(Qt::UserRole).toString() : QString();
}

int PlaylistView::rowOfCue(int cue) const {
    for (int r = 0; r < count(); ++r)
        if (cueAt(r) == cue) return r;
    return -1;
}

QString PlaylistView::selectedBlock() const { return blockAt(currentRow()); }

void PlaylistView::selectBlock(const QString& blockId) {
    for (int r = 0; r < count(); ++r)
        if (blockAt(r) == blockId) {
            setCurrentRow(r);
            scrollToItem(item(r));
            return;
        }
}

void PlaylistView::refresh() { viewport()->update(); }

void PlaylistView::setTallyEnabled(bool on) {
    if (on == tallyEnabled_) return;
    tallyEnabled_ = on;
    viewport()->update();
}

void PlaylistView::setArmed(int index) {
    if (index == armed_) return;
    armed_ = index;
    viewport()->update();
}

void PlaylistView::setPlaying(int index, double progress, const QString& state) {
    if (index == playing_ && std::abs(progress - progress_) < 0.001 && state == playingState_) return;
    playing_ = index;
    progress_ = progress;
    playingState_ = state;
    viewport()->update();
}

int PlaylistView::selectedIndex() const { return cueAt(currentRow()); }

void PlaylistView::selectIndex(int index) {
    const int row = rowOfCue(index);
    if (row < 0) return;
    setCurrentRow(row);
    scrollToItem(item(row));
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

void PlaylistView::dropTarget(const QPoint& pos, int& before, QString& block) const {
    const int n = project_ ? static_cast<int>(project_->cues.size()) : 0;
    before = n;
    block.clear();
    const int row = indexAt(pos).row();
    if (row < 0) return; // abaixo de tudo: no fim, fora de blocos
    if (const QString b = blockAt(row); !b.isEmpty()) {
        // Largar no cabeçalho = dentro do bloco, no início (bloco vazio: no fim da lista).
        block = b;
        const int first = project_->firstInBlock(b.toStdString());
        before = first >= 0 ? first : n;
        return;
    }
    const int c = cueAt(row);
    if (c < 0) return;
    block = qs(project_->cues[static_cast<std::size_t>(c)].block);
    before = pos.y() > visualItemRect(item(row)).center().y() ? c + 1 : c; // metade de baixo: depois
}

void PlaylistView::dropEvent(QDropEvent* e) {
    if (!editable_ || !project_) return e->ignore();
    int before = 0;
    QString block;
    dropTarget(e->position().toPoint(), before, block);
    if (e->mimeData()->hasUrls()) {
        QStringList files;
        for (const QUrl& u : e->mimeData()->urls())
            if (u.isLocalFile()) files << u.toLocalFile();
        e->acceptProposedAction();
        if (!files.isEmpty()) emit filesDropped(before, files, block);
        return;
    }
    if (e->source() == this) {
        const int from = selectedIndex();
        // Não deixa o QListWidget mexer nas linhas: o projecto é que manda.
        e->setDropAction(Qt::IgnoreAction);
        e->accept();
        if (from >= 0) emit cueDropped(from, before, block);
        return;
    }
    e->ignore();
}

bool PlaylistView::event(QEvent* e) {
    // Dica ao passar o rato nos ícones de cada cue (o que fazem e como se mudam).
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const QPoint pos = viewport()->mapFrom(this, he->pos());
        const QModelIndex idx = indexAt(pos);
        QString tip;
        if (idx.isValid() && !blockAt(idx.row()).isEmpty()) {
            tip = tr("Block: click the arrow to hide / show its videos, ⋯ or right-click for its options "
                     "(loop, what happens at the end). Drop files here to add them to the block.");
        } else if (idx.isValid()) {
            const QRect row = visualRect(idx);
            auto name = [this](int i) -> QString {
                switch (static_cast<CueIcon>(i)) {
                case CueIcon::Audio: return tr("Sound on / off (mute)");
                case CueIcon::PauseStart: return tr("Pause at start: the cue waits on its first frame for Play (Space)");
                case CueIcon::FadeIn: return tr("Fade in from black");
                case CueIcon::Loop: return tr("Loop: repeat the cue (number of repeats in the Inspector)");
                case CueIcon::Crossfade: return tr("Crossfade from the previous cue");
                case CueIcon::FadeOut: return tr("Fade out to black at the end");
                case CueIcon::PauseEnd: return tr("Pause at end: holds the last frame");
                case CueIcon::Clear: return tr("Clear at end: black when the cue ends");
                case CueIcon::Goto: return tr("Goto: after this cue, jump to another one");
                case CueIcon::Count: break;
                }
                return {};
            };
            for (int i = 0; i < static_cast<int>(CueIcon::Count); ++i)
                if (CueDelegate::iconRect(row, i).contains(pos))
                    tip = name(i) + QStringLiteral("\n") + tr("Click to turn on / off (Ctrl+Z undoes).");
            if (tip.isEmpty() && tallyEnabled_ && CueDelegate::cueButtonRect(row).contains(pos))
                tip = tr("CUE: this cue plays when the player input goes on air in the switcher (vMix / ATEM). "
                         "Click to arm / disarm.");
            if (tip.isEmpty() && cueAt(idx.row()) == playing_ && CueDelegate::thumbButtonRect(row).contains(pos))
                tip = tr("Use the current frame as the thumbnail (Ctrl+P)");
        }
        if (tip.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(he->globalPos(), tip, this);
        return true;
    }
    // A lista não "come" o ESPAÇO: fica para o play/pausa.
    if (e->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Space && ke->modifiers() == Qt::NoModifier) {
            e->ignore();
            return false;
        }
    }
    return QListWidget::event(e);
}

void PlaylistView::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier) {
        emit spacePressed();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && selectedIndex() >= 0) {
        emit playRequested(selectedIndex());
        return;
    }
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && selectedIndex() >= 0 && editable_) {
        emit deleteRequested(selectedIndex());
        return;
    }
    QListWidget::keyPressEvent(e);
}

void PlaylistView::mouseDoubleClickEvent(QMouseEvent* e) {
    const QModelIndex idx = indexAt(e->position().toPoint());
    if (!idx.isValid()) return;
    if (const QString b = blockAt(idx.row()); !b.isEmpty()) {
        // Duplo clique no bloco: toca a primeira cue do bloco.
        if (project_ && !CueDelegate::blockArrowRect(visualRect(idx)).contains(e->position().toPoint()))
            if (const int first = project_->firstInBlock(b.toStdString()); first >= 0) emit playRequested(first);
        return;
    }
    // Duplo clique num ícone não dispara a cue.
    const QRect row = visualRect(idx);
    for (int i = 0; i < static_cast<int>(CueIcon::Count); ++i)
        if (CueDelegate::iconRect(row, i).contains(e->position().toPoint())) return;
    if (CueDelegate::thumbButtonRect(row).contains(e->position().toPoint())) return;
    if (cueAt(idx.row()) >= 0) emit playRequested(cueAt(idx.row()));
}

void PlaylistView::contextMenuEvent(QContextMenuEvent* e) {
    const QModelIndex idx = indexAt(e->pos());
    if (idx.isValid()) setCurrentRow(idx.row());
    if (idx.isValid() && !blockAt(idx.row()).isEmpty()) {
        emit blockMenuRequested(blockAt(idx.row()), e->globalPos());
        return;
    }
    emit contextMenuRequested(idx.isValid() ? cueAt(idx.row()) : -1, e->globalPos());
}

// ------------------------------------------------------------------ CueDelegate

CueDelegate::CueDelegate(PlaylistView* view) : QStyledItemDelegate(view), view_(view) {}

QSize CueDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex& idx) const {
    return {300, idx.data(kKindRole).toInt() == 1 ? kBlockHeight : kRowHeight};
}

QRect CueDelegate::blockArrowRect(const QRect& row) { return {row.left(), row.top(), 28, row.height()}; }

QRect CueDelegate::blockMenuRect(const QRect& row) { return {row.right() - 34, row.top() + 5, 28, row.height() - 10}; }

QRect CueDelegate::thumbButtonRect(const QRect& row) {
    return {row.left() + kThumbX + kThumbW - 26, row.top() + kThumbY + 3, 23, 18};
}

QRect CueDelegate::cueButtonRect(const QRect& row) {
    return {row.left() + kThumbX + 3, row.top() + kThumbY + kThumbH - 19, 34, 16};
}

QRect CueDelegate::iconRect(const QRect& row, int i) {
    return {row.left() + kInfoX + i * 25, row.top() + 62, 23, 22};
}

void CueDelegate::paintBlock(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    const Project* pr = view_->project_;
    const int bi = idx.data(kIndexRole).toInt();
    if (!pr || bi < 0 || bi >= static_cast<int>(pr->blocks.size())) return;
    const vthost::Block& b = pr->blocks[static_cast<std::size_t>(bi)];
    const QRect r = opt.rect;
    const bool selected = opt.state & QStyle::State_Selected;
    // Bloco com a cue no ar: destaque verde (como no player de referência).
    const int pi = view_->playing_;
    const bool live = pi >= 0 && pi < static_cast<int>(pr->cues.size()) && pr->cues[static_cast<std::size_t>(pi)].block == b.id;
    int cues = 0;
    double total = 0.0;
    for (const auto& q : pr->cues)
        if (q.block == b.id) {
            ++cues;
            total += q.totalSeconds(view_->media_->info(qs(q.path)).duration);
        }
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->fillRect(r, live ? QColor(48, 92, 40) : selected ? QColor(52, 74, 110) : QColor(38, 52, 74));
    p->setPen(QColor(20, 20, 20));
    p->drawLine(r.bottomLeft(), r.bottomRight());
    // Seta (esconder / mostrar).
    const QRect ar = blockArrowRect(r);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(220, 220, 220));
    const QPointF c = QRectF(ar).center();
    QPolygonF tri;
    if (b.collapsed) tri << QPointF(c.x() - 3, c.y() - 5) << QPointF(c.x() + 4, c.y()) << QPointF(c.x() - 3, c.y() + 5);
    else tri << QPointF(c.x() - 5, c.y() - 3) << QPointF(c.x() + 5, c.y() - 3) << QPointF(c.x(), c.y() + 4);
    p->drawPolygon(tri);
    QFont f = opt.font;
    f.setPixelSize(13);
    f.setBold(true);
    p->setFont(f);
    p->setPen(Qt::white);
    const QString title = QStringLiteral("#%1  ").arg(idx.data(kNumberRole).toInt()) +
                          (b.name.empty() ? PlaylistView::tr("BLOCK") : qs(b.name));
    // À direita: LOOP, o que acontece no fim, nº de vídeos e duração.
    QString right = b.loop ? PlaylistView::tr("LOOP")
                           : b.end == vthost::BlockEnd::Pause ? PlaylistView::tr("PAUSE")
                           : b.end == vthost::BlockEnd::Stop  ? PlaylistView::tr("STOP")
                                                              : PlaylistView::tr("CONTINUE");
    QFont small = f;
    small.setPixelSize(11);
    const QString info = PlaylistView::tr("%1 video(s)").arg(cues) + QStringLiteral(" · ") + formatTimecode(total, 25.0).left(8);
    const QFontMetrics sm(small);
    const int infoW = sm.horizontalAdvance(info);
    const int badgeW = sm.horizontalAdvance(right) + 12;
    const QRect menu = blockMenuRect(r);
    const QRect badge(menu.left() - 8 - badgeW, r.top() + 8, badgeW, r.height() - 16);
    p->drawText(QRect(ar.right() + 2, r.top(), std::max(10, badge.left() - infoW - 20 - ar.right()), r.height()),
                Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(f).elidedText(title, Qt::ElideRight, std::max(10, badge.left() - infoW - 20 - ar.right())));
    p->setFont(small);
    p->setPen(QColor(200, 210, 225));
    p->drawText(QRect(badge.left() - infoW - 10, r.top(), infoW, r.height()), Qt::AlignRight | Qt::AlignVCenter, info);
    p->setPen(Qt::NoPen);
    p->setBrush(b.loop ? QColor(40, 140, 220) : b.end == vthost::BlockEnd::Continue ? QColor(70, 70, 70) : QColor(20, 20, 20, 150));
    p->drawRoundedRect(badge, 3, 3);
    p->setPen(Qt::white);
    p->drawText(badge, Qt::AlignCenter, right);
    // ⋯ opções
    p->setBrush(Qt::NoBrush);
    p->setPen(QColor(220, 220, 220));
    for (int k = -1; k <= 1; ++k) p->drawEllipse(QPointF(menu.center().x() + k * 5, menu.center().y()), 1.6, 1.6);
    p->restore();
}

void CueDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    if (idx.data(kKindRole).toInt() == 1) {
        paintBlock(p, opt, idx);
        return;
    }
    const Project* pr = view_->project_;
    const int i = idx.data(kIndexRole).toInt();
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
    // Miniatura escolhida pelo utilizador (ponto do vídeo) ou a automática.
    QImage thumb = info.thumb;
    if (q.thumbTime) {
        const QImage chosen = view_->media_->thumbnail(qs(q.path), *q.thumbTime);
        if (!chosen.isNull()) thumb = chosen;
    }
    if (!thumb.isNull()) {
        const QSize sz = thumb.size().scaled(th.size(), Qt::KeepAspectRatio);
        p->drawImage(QRect(th.center().x() - sz.width() / 2, th.center().y() - sz.height() / 2, sz.width(), sz.height()),
                     thumb);
    } else if (info.ready && info.ok && info.hasAudio) {
        p->setPen(QColor(120, 120, 120));
        paintCueIcon(p, QRectF(th.center().x() - 14, th.center().y() - 12, 28, 24), CueIcon::Audio, false);
    }
    if (view_->tallyEnabled_) {
        // CUE: esta cue dispara quando o input do player for para o ar no misturador.
        const QRect cb = cueButtonRect(r);
        const bool armed = i == view_->armed_;
        p->setPen(armed ? Qt::NoPen : QPen(QColor(150, 150, 150, 160), 1));
        p->setBrush(armed ? QColor(220, 45, 45) : QColor(0, 0, 0, 170));
        p->drawRoundedRect(cb, 3, 3);
        QFont cf = p->font();
        cf.setBold(true);
        cf.setPixelSize(10);
        p->setFont(cf);
        p->setPen(armed ? Qt::white : QColor(170, 170, 170));
        p->drawText(cb, Qt::AlignCenter, QStringLiteral("CUE"));
    }
    if (playing && view_->editable_ && !info.still && info.ok && !info.thumb.isNull()) {
        // Botão da câmara: a miniatura passa a ser o ponto actual do vídeo.
        const QRect cb = thumbButtonRect(r);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 170));
        p->drawRoundedRect(cb, 3, 3);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(QColor(235, 235, 235), 1.4));
        const QRectF body(cb.left() + 4.5, cb.top() + 5.5, cb.width() - 9, cb.height() - 9);
        p->drawRoundedRect(body, 1.5, 1.5);
        p->drawLine(QPointF(body.center().x() - 2.5, body.top()), QPointF(body.center().x() - 1.5, body.top() - 2));
        p->drawLine(QPointF(body.center().x() - 1.5, body.top() - 2), QPointF(body.center().x() + 2.5, body.top() - 2));
        p->drawLine(QPointF(body.center().x() + 2.5, body.top() - 2), QPointF(body.center().x() + 3.5, body.top()));
        p->drawEllipse(body.center(), 2.6, 2.6);
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
    if (idx.data(kKindRole).toInt() == 1) {
        const QString id = idx.data(Qt::UserRole).toString();
        if (blockArrowRect(opt.rect).contains(me->position().toPoint())) {
            emit view_->blockToggleRequested(id); // esconder / mostrar também em Show Mode
            return true;
        }
        if (blockMenuRect(opt.rect).contains(me->position().toPoint())) {
            emit view_->blockMenuRequested(id, me->globalPosition().toPoint());
            return true;
        }
        return false;
    }
    const int cue = idx.data(kIndexRole).toInt();
    if (cue == view_->playing_ && view_->editable_ && thumbButtonRect(opt.rect).contains(me->position().toPoint())) {
        emit view_->thumbnailRequested(cue);
        return true;
    }
    if (view_->tallyEnabled_ && cueButtonRect(opt.rect).contains(me->position().toPoint())) {
        emit view_->armClicked(cue); // também em Show Mode: armar é operação ao vivo
        return true;
    }
    for (int k = 0; k < static_cast<int>(CueIcon::Count); ++k)
        if (iconRect(opt.rect, k).contains(me->position().toPoint())) {
            if (!view_->editable_) return true;
            emit view_->iconClicked(cue, static_cast<CueIcon>(k), me->globalPosition().toPoint());
            return true;
        }
    return false;
}
