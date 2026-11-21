#include "dashboard_table.h"

#include "library_panel.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMimeData>

DashboardTable::DashboardTable(QWidget* parent) : QTableWidget(parent) {
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setDropIndicatorShown(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setIconSize(QSize(96, 54));
    horizontalHeader()->setDefaultSectionSize(120);
    horizontalHeader()->setSectionsClickable(true);
    verticalHeader()->setDefaultSectionSize(64);
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
