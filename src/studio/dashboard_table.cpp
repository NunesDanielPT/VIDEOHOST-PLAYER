#include "dashboard_table.h"

#include "library_panel.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>

#include <algorithm>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QStyledItemDelegate>

namespace {
// Célula como no Millumin: a miniatura ocupa a célula e o nome fica por baixo.
class CellDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        p->save();
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        const bool active = idx.data(DashboardTable::kActiveRole).toBool();
        const bool selected = opt.state & QStyle::State_Selected;
        const QRect r = opt.rect.adjusted(1, 1, -1, -1);
        p->fillRect(opt.rect, active ? QColor(70, 42, 8) : QColor(30, 30, 30));
        const QImage thumb = idx.data(DashboardTable::kThumbRole).value<QImage>();
        const QString name = idx.data(Qt::DisplayRole).toString();
        const int band = name.isEmpty() ? 0 : opt.fontMetrics.height() + 2;
        const QRect area = r.adjusted(2, 2, -2, -2 - band);
        if (!thumb.isNull() && area.width() > 4 && area.height() > 4) {
            const QSize sz = thumb.size().scaled(area.size(), Qt::KeepAspectRatio);
            const QRect target(area.left() + (area.width() - sz.width()) / 2, area.top() + (area.height() - sz.height()) / 2,
                           sz.width(), sz.height());
            p->drawImage(target, thumb);
        } else if (!name.isEmpty()) {
            p->setPen(QColor(90, 90, 90));
            p->drawRect(area);
        }
        if (band > 0) {
            const QRect tb(r.left() + 2, r.bottom() - band + 1, r.width() - 4, band);
            p->setPen(active ? QColor(255, 200, 120) : QColor(220, 220, 220));
            p->drawText(tb, Qt::AlignCenter, opt.fontMetrics.elidedText(name, Qt::ElideMiddle, tb.width()));
        }
        bool ok = false;
        const double prog = idx.data(DashboardTable::kProgressRole).toDouble(&ok);
        if (ok && prog >= 0.0) {
            const QRect pb(r.left() + 2, area.bottom() - 3, r.width() - 4, 4);
            p->fillRect(pb, QColor(0, 0, 0, 160));
            p->fillRect(QRect(pb.left(), pb.top(), static_cast<int>(pb.width() * std::min(1.0, prog)), pb.height()),
                        QColor(255, 136, 0));
        }
        if (active) {
            p->setPen(QPen(QColor(255, 136, 0), 2));
            p->drawRect(opt.rect.adjusted(1, 1, -1, -1));
        }
        if (selected) {
            p->setPen(QPen(QColor(0, 150, 220), 2));
            p->drawRect(opt.rect.adjusted(2, 2, -2, -2));
        }
        p->restore();
    }
};
} // namespace

DashboardTable::DashboardTable(QWidget* parent) : QTableWidget(parent) {
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setDropIndicatorShown(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setIconSize(QSize(96, 54));
    setItemDelegate(new CellDelegate(this));
    horizontalHeader()->setDefaultSectionSize(140);
    horizontalHeader()->setSectionsClickable(true);
    verticalHeader()->setDefaultSectionSize(78);
    setContextMenuPolicy(Qt::CustomContextMenu);
}

void DashboardTable::dragEnterEvent(QDragEnterEvent* e) {
    if (editable_ && e->mimeData()->hasFormat(QString::fromLatin1(kMediaMime))) e->acceptProposedAction();
    else e->ignore();
}

void DashboardTable::dragMoveEvent(QDragMoveEvent* e) {
    const QModelIndex idx = indexAt(e->position().toPoint());
    if (editable_ && idx.isValid() && e->mimeData()->hasFormat(QString::fromLatin1(kMediaMime))) e->acceptProposedAction();
    else e->ignore();
}

void DashboardTable::dropEvent(QDropEvent* e) {
    const QModelIndex idx = indexAt(e->position().toPoint());
    if (!editable_ || !idx.isValid() || !e->mimeData()->hasFormat(QString::fromLatin1(kMediaMime))) {
        e->ignore();
        return;
    }
    const QString id = QString::fromUtf8(e->mimeData()->data(QString::fromLatin1(kMediaMime)));
    e->acceptProposedAction();
    emit mediaDropped(idx.row(), idx.column(), id);
}
