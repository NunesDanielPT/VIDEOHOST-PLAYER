#include "comp_grid.h"

#include "media_cache.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QUrl>

#include <algorithm>

using namespace vthost;

namespace {
constexpr int kHeaderW = 200;   // cabeçalho das layers
constexpr int kColHeaderH = 26; // cabeçalho das colunas
constexpr int kColW = 118;
constexpr int kRowH = 86;
constexpr int kGap = 2;
const QColor kAccent(108, 245, 210); // verde-água (como no Resolume)
const char* kClipMime = "application/x-vthost-clip";
QString qs(const std::string& s) { return QString::fromStdString(s); }
} // namespace

CompGrid::CompGrid(MediaCache* media, QWidget* parent) : QWidget(parent), media_(media) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumHeight(kColHeaderH + kRowH);
}

void CompGrid::setComposition(const Project* project, const QString& compId) {
    project_ = project;
    compId_ = compId;
    updateGeometry();
    update();
}

void CompGrid::setPlaying(const QHash<QString, QPair<QString, double>>& playing) {
    if (playing == playing_) return;
    playing_ = playing;
    update();
}

void CompGrid::setSelection(const QString& layer, const QString& column) {
    selLayer_ = layer;
    selColumn_ = column;
    update();
}

const Composition* CompGrid::comp() const { return project_ ? project_->findComposition(compId_.toStdString()) : nullptr; }

QSize CompGrid::sizeHint() const {
    const Composition* c = comp();
    const int cols = c ? static_cast<int>(c->columns.size()) + 1 : 1;
    const int rows = c ? static_cast<int>(c->layers.size()) : 1;
    return {kHeaderW + cols * (kColW + kGap) + 4, kColHeaderH + rows * (kRowH + kGap) + 4};
}

int CompGrid::layerIndexForRow(int row) const {
    const Composition* c = comp();
    if (!c) return -1;
    const int n = static_cast<int>(c->layers.size());
    return (row >= 0 && row < n) ? n - 1 - row : -1; // linha 0 = layer de cima
}

QRect CompGrid::cellRect(int row, int column) const {
    return {kHeaderW + column * (kColW + kGap), kColHeaderH + row * (kRowH + kGap), kColW, kRowH};
}

QRect CompGrid::layerButtonRect(int row, QChar which) const {
    const int y = kColHeaderH + row * (kRowH + kGap);
    static const QString order = QStringLiteral("XBSVA");
    const int i = static_cast<int>(order.indexOf(which));
    if (i == 0) return {4, y + 6, 34, kRowH - 34};                       // X grande à esquerda
    if (i == 1 || i == 2) return {42 + (i - 1) * 42, y + 6, 40, 30};     // B, S
    return {130 + (i - 3) * 30, y + 6, 28, 22};                           // V, A
}

CompGrid::Hit CompGrid::hitAt(const QPoint& p) const {
    Hit h;
    const Composition* c = comp();
    if (!c) return h;
    const int nCols = static_cast<int>(c->columns.size());
    const int nRows = static_cast<int>(c->layers.size());
    if (p.y() < kColHeaderH) {
        if (p.x() < kHeaderW) return h;
        const int col = (p.x() - kHeaderW) / (kColW + kGap);
        if (col < nCols) {
            h.kind = Hit::ColumnHeader;
            h.column = col;
        } else if (col == nCols) {
            h.kind = Hit::AddColumn;
        }
        return h;
    }
    const int row = (p.y() - kColHeaderH) / (kRowH + kGap);
    if (row < 0 || row >= nRows) return h;
    h.row = row;
    if (p.x() < kHeaderW) {
        for (QChar b : QStringLiteral("XBSVA"))
            if (layerButtonRect(row, b).contains(p)) {
                h.kind = Hit::LayerButton;
                h.button = b;
                return h;
            }
        h.kind = Hit::LayerHeader;
        return h;
    }
    const int col = (p.x() - kHeaderW) / (kColW + kGap);
    if (col >= 0 && col < nCols) {
        h.kind = Hit::Cell;
        h.column = col;
    }
    return h;
}

void CompGrid::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 24));
    const Composition* c = comp();
    if (!c) {
        p.setPen(QColor(130, 130, 130));
        p.drawText(rect(), Qt::AlignCenter, tr("No composition"));
        return;
    }
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    const int nCols = static_cast<int>(c->columns.size());
    const int nRows = static_cast<int>(c->layers.size());

    // Colunas a tocar (alguma layer com clip dessa coluna no ar).
    QSet<QString> liveColumns;
    for (auto it = playing_.cbegin(); it != playing_.cend(); ++it) liveColumns.insert(it.value().first);

    // Cabeçalhos das colunas.
    p.fillRect(QRect(0, 0, kHeaderW, kColHeaderH), QColor(32, 32, 32));
    p.setPen(QColor(200, 200, 200));
    p.drawText(QRect(8, 0, kHeaderW - 8, kColHeaderH), Qt::AlignVCenter | Qt::AlignLeft, qs(c->name));
    for (int k = 0; k < nCols; ++k) {
        const QRect r(kHeaderW + k * (kColW + kGap), 2, kColW, kColHeaderH - 4);
        const bool live = liveColumns.contains(qs(c->columns[static_cast<std::size_t>(k)].id));
        p.fillRect(r, live ? kAccent : QColor(52, 52, 52));
        p.setPen(live ? Qt::black : QColor(220, 220, 220));
        p.drawText(r, Qt::AlignCenter, p.fontMetrics().elidedText(qs(c->columns[static_cast<std::size_t>(k)].name), Qt::ElideRight, kColW - 6));
    }
    {
        const QRect r(kHeaderW + nCols * (kColW + kGap), 2, 30, kColHeaderH - 4);
        p.fillRect(r, QColor(40, 40, 40));
        p.setPen(QColor(170, 170, 170));
        p.drawText(r, Qt::AlignCenter, QStringLiteral("+"));
    }

    for (int row = 0; row < nRows; ++row) {
        const int li = layerIndexForRow(row);
        const CompLayer& l = c->layers[static_cast<std::size_t>(li)];
        const QString lid = qs(l.id);
        const int y = kColHeaderH + row * (kRowH + kGap);
        const bool layerSel = selLayer_ == lid;
        // Cabeçalho da layer.
        p.fillRect(QRect(0, y, kHeaderW - 4, kRowH), QColor(44, 44, 44));
        auto button = [&](QChar b, bool on, const QColor& onColor) {
            const QRect br = layerButtonRect(row, b);
            p.fillRect(br, on ? onColor : QColor(60, 60, 60));
            p.setPen(on ? Qt::black : QColor(210, 210, 210));
            p.drawText(br, Qt::AlignCenter, b == QLatin1Char('X') ? QStringLiteral("✕") : QString(b));
        };
        button(QLatin1Char('X'), false, kAccent);
        button(QLatin1Char('B'), l.bypass, QColor(230, 160, 60));
        button(QLatin1Char('S'), l.solo, QColor(240, 220, 80));
        button(QLatin1Char('V'), l.video, kAccent);
        button(QLatin1Char('A'), l.audio, QColor(200, 110, 170));
        const QRect nameR(4, y + kRowH - 24, kHeaderW - 12, 20);
        p.fillRect(nameR, layerSel ? kAccent : QColor(36, 36, 36));
        p.setPen(layerSel ? Qt::black : QColor(220, 220, 220));
        QString label = qs(l.name);
        if (l.props.opacity < 0.999) label += QStringLiteral("  %1%").arg(static_cast<int>(l.props.opacity * 100));
        p.drawText(nameR.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(label, Qt::ElideRight, nameR.width() - 10));

        const auto live = playing_.value(lid);
        for (int k = 0; k < nCols; ++k) {
            const CompColumn& col = c->columns[static_cast<std::size_t>(k)];
            const QRect r = cellRect(row, k);
            p.fillRect(r, QColor(34, 34, 34));
            auto cell = col.cells.find(l.id);
            const bool isLive = !live.first.isEmpty() && live.first == qs(col.id);
            if (cell != col.cells.end()) {
                const Clip& clip = cell->second;
                const QString path = qs(clip.path);
                QImage thumb = media_->info(path).thumb;
                if (clip.thumbTime) {
                    const QImage t = media_->thumbnail(path, *clip.thumbTime);
                    if (!t.isNull()) thumb = t;
                }
                const QRect tr = r.adjusted(2, 2, -2, -20);
                p.fillRect(tr, Qt::black);
                if (!thumb.isNull()) {
                    const QSize sz = thumb.size().scaled(tr.size(), Qt::KeepAspectRatio);
                    p.drawImage(QRect(tr.center().x() - sz.width() / 2, tr.center().y() - sz.height() / 2, sz.width(), sz.height()), thumb);
                }
                const QRect nr(r.left(), r.bottom() - 18, r.width(), 19);
                p.fillRect(nr, isLive ? kAccent : QColor(48, 48, 48));
                p.setPen(isLive ? Qt::black : QColor(220, 220, 220));
                p.drawText(nr.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                           p.fontMetrics().elidedText(qs(clip.displayName()), Qt::ElideRight, nr.width() - 8));
                if (isLive) {
                    p.fillRect(QRect(tr.left(), tr.bottom() - 3, tr.width(), 4), QColor(0, 0, 0, 170));
                    p.fillRect(QRect(tr.left(), tr.bottom() - 3, static_cast<int>(tr.width() * std::clamp(live.second, 0.0, 1.0)), 4), kAccent);
                }
            } else {
                p.fillRect(QRect(r.left(), r.bottom() - 18, r.width(), 19), QColor(40, 40, 40));
            }
            if (isLive) {
                p.setPen(QPen(kAccent, 2));
                p.drawRect(r.adjusted(1, 1, -1, -1));
            }
            if (layerSel && selColumn_ == qs(col.id)) {
                p.setPen(QPen(Qt::white, 2, Qt::DashLine));
                p.drawRect(r.adjusted(2, 2, -2, -2));
            }
        }
    }
}

void CompGrid::mousePressEvent(QMouseEvent* e) {
    pressPos_ = e->position().toPoint();
    pressHit_ = hitAt(pressPos_);
    dragStarted_ = false;
    if (e->button() != Qt::LeftButton) return;
    const Composition* c = comp();
    if (!c) return;
    const Hit& h = pressHit_;
    if (h.kind == Hit::ColumnHeader) {
        emit launchColumn(qs(c->columns[static_cast<std::size_t>(h.column)].id));
    } else if (h.kind == Hit::AddColumn) {
        if (editable_) emit addColumnRequested();
    } else if (h.kind == Hit::LayerButton || h.kind == Hit::LayerHeader) {
        const QString lid = qs(c->layers[static_cast<std::size_t>(layerIndexForRow(h.row))].id);
        if (h.kind == Hit::LayerButton) {
            if (h.button == QLatin1Char('X')) emit clearLayer(lid);
            else emit layerToggle(lid, h.button);
        }
        selLayer_ = lid;
        emit selectionChanged(selLayer_, selColumn_);
        update();
    }
}

void CompGrid::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton) || dragStarted_ || pressHit_.kind != Hit::Cell || !editable_) return;
    if ((e->position().toPoint() - pressPos_).manhattanLength() < QApplication::startDragDistance()) return;
    const Composition* c = comp();
    if (!c) return;
    const QString lid = qs(c->layers[static_cast<std::size_t>(layerIndexForRow(pressHit_.row))].id);
    const QString cid = qs(c->columns[static_cast<std::size_t>(pressHit_.column)].id);
    if (!c->columns[static_cast<std::size_t>(pressHit_.column)].cells.count(lid.toStdString())) return;
    dragStarted_ = true;
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kClipMime), (lid + QLatin1Char('|') + cid).toUtf8());
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
}

void CompGrid::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || dragStarted_ || pressHit_.kind != Hit::Cell) return;
    const Hit h = hitAt(e->position().toPoint());
    if (h.kind != Hit::Cell || h.row != pressHit_.row || h.column != pressHit_.column) return;
    const Composition* c = comp();
    if (!c) return;
    const QString lid = qs(c->layers[static_cast<std::size_t>(layerIndexForRow(h.row))].id);
    const QString cid = qs(c->columns[static_cast<std::size_t>(h.column)].id);
    selLayer_ = lid;
    selColumn_ = cid;
    update();
    emit selectionChanged(lid, cid);
    // Clique = lançar (como no Resolume); Ctrl+clique = só selecionar (ver no Preview).
    const bool hasClip = c->columns[static_cast<std::size_t>(h.column)].cells.count(lid.toStdString()) > 0;
    if (hasClip && !(e->modifiers() & Qt::ControlModifier)) emit launchClip(lid, cid);
}

void CompGrid::mouseDoubleClickEvent(QMouseEvent* e) {
    const Hit h = hitAt(e->position().toPoint());
    const Composition* c = comp();
    if (!c || h.kind != Hit::LayerHeader || !editable_) return;
    emit layerRename(qs(c->layers[static_cast<std::size_t>(layerIndexForRow(h.row))].id));
}

void CompGrid::contextMenuEvent(QContextMenuEvent* e) {
    const Hit h = hitAt(e->pos());
    const Composition* c = comp();
    if (!c) return;
    if (h.kind == Hit::Cell) {
        const QString lid = qs(c->layers[static_cast<std::size_t>(layerIndexForRow(h.row))].id);
        const QString cid = qs(c->columns[static_cast<std::size_t>(h.column)].id);
        selLayer_ = lid;
        selColumn_ = cid;
        update();
        emit selectionChanged(lid, cid);
        emit cellMenu(lid, cid, e->globalPos());
    } else if (h.kind == Hit::ColumnHeader) {
        emit columnMenu(qs(c->columns[static_cast<std::size_t>(h.column)].id), e->globalPos());
    } else if (h.kind == Hit::LayerHeader || h.kind == Hit::LayerButton) {
        emit layerMenu(qs(c->layers[static_cast<std::size_t>(layerIndexForRow(h.row))].id), e->globalPos());
    }
}

void CompGrid::dragEnterEvent(QDragEnterEvent* e) {
    if (!editable_) return e->ignore();
    if (e->mimeData()->hasUrls() || e->mimeData()->hasFormat(QString::fromLatin1(kClipMime))) e->acceptProposedAction();
    else e->ignore();
}

void CompGrid::dragMoveEvent(QDragMoveEvent* e) {
    if (!editable_ || hitAt(e->position().toPoint()).kind != Hit::Cell) return e->ignore();
    if (e->mimeData()->hasFormat(QString::fromLatin1(kClipMime)))
        e->setDropAction((e->modifiers() & Qt::ControlModifier) ? Qt::CopyAction : Qt::MoveAction);
    e->accept();
}

void CompGrid::dropEvent(QDropEvent* e) {
    const Hit h = hitAt(e->position().toPoint());
    const Composition* c = comp();
    if (!c || h.kind != Hit::Cell || !editable_) return e->ignore();
    const QString lid = qs(c->layers[static_cast<std::size_t>(layerIndexForRow(h.row))].id);
    const QString cid = qs(c->columns[static_cast<std::size_t>(h.column)].id);
    if (e->mimeData()->hasFormat(QString::fromLatin1(kClipMime))) {
        const QStringList parts = QString::fromUtf8(e->mimeData()->data(QString::fromLatin1(kClipMime))).split(QLatin1Char('|'));
        const bool copy = e->modifiers() & Qt::ControlModifier;
        e->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction);
        e->accept();
        if (parts.size() == 2 && !(parts[0] == lid && parts[1] == cid)) emit clipMoved(parts[0], parts[1], lid, cid, copy);
        return;
    }
    QStringList files;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) files << u.toLocalFile();
    e->acceptProposedAction();
    if (!files.isEmpty()) emit filesDropped(lid, cid, files);
}
