#include "layer_header.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>

void paintEye(QPainter* p, const QRect& r, bool open, const QColor& color) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF e = QRectF(r).adjusted(1, r.height() * 0.25, -1, -r.height() * 0.25);
    QPainterPath path;
    path.moveTo(e.left(), e.center().y());
    path.quadTo(e.center().x(), e.top() - e.height() * 0.6, e.right(), e.center().y());
    path.quadTo(e.center().x(), e.bottom() + e.height() * 0.6, e.left(), e.center().y());
    p->setPen(QPen(color, 1.4));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
    p->setBrush(color);
    p->drawEllipse(e.center(), e.height() * 0.28, e.height() * 0.28);
    if (!open) {
        p->setPen(QPen(QColor(230, 70, 60), 1.8));
        p->drawLine(QPointF(r.left() + 1, r.bottom() - 1), QPointF(r.right() - 1, r.top() + 1));
    }
    p->restore();
}

void paintLock(QPainter* p, const QRect& r, bool locked, const QColor& color) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF body(r.left() + r.width() * 0.2, r.top() + r.height() * 0.45, r.width() * 0.6, r.height() * 0.45);
    p->setPen(QPen(color, 1.4));
    p->setBrush(locked ? color : Qt::NoBrush);
    p->drawRoundedRect(body, 1.5, 1.5);
    QRectF shackle(body.left() + body.width() * 0.15, r.top() + r.height() * 0.1, body.width() * 0.7, r.height() * 0.6);
    if (!locked) shackle.translate(body.width() * 0.35, -r.height() * 0.05);
    p->setBrush(Qt::NoBrush);
    p->drawArc(shackle, 0, 180 * 16);
    p->restore();
}

void paintSolo(QPainter* p, const QRect& r, bool on, const QColor& color) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF b = QRectF(r).adjusted(1, 1, -1, -1);
    p->setPen(QPen(on ? QColor(255, 196, 0) : color, 1.2));
    p->setBrush(on ? QColor(255, 196, 0) : Qt::NoBrush);
    p->drawRoundedRect(b, 2, 2);
    QFont f = p->font();
    f.setPixelSize(std::max(8, r.height() - 5));
    f.setBold(true);
    p->setFont(f);
    p->setPen(on ? QColor(20, 20, 20) : color);
    p->drawText(b, Qt::AlignCenter, QStringLiteral("S"));
    p->restore();
}

QIcon makeToolIcon(ToolIcon kind) {
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c(225, 225, 225);
    p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    switch (kind) {
    case ToolIcon::Add:
        p.drawLine(10, 4, 10, 16);
        p.drawLine(4, 10, 16, 10);
        break;
    case ToolIcon::Duplicate:
        p.drawRect(3, 6, 9, 9);
        p.drawRect(8, 3, 9, 9);
        break;
    case ToolIcon::Up:
        p.drawLine(10, 4, 10, 16);
        p.drawLine(10, 4, 5, 9);
        p.drawLine(10, 4, 15, 9);
        break;
    case ToolIcon::Down:
        p.drawLine(10, 4, 10, 16);
        p.drawLine(10, 16, 5, 11);
        p.drawLine(10, 16, 15, 11);
        break;
    case ToolIcon::Delete:
        p.drawLine(4, 6, 16, 6);
        p.drawLine(8, 6, 8, 4);
        p.drawLine(12, 6, 12, 4);
        p.drawLine(8, 4, 12, 4);
        p.drawRect(QRectF(5.5, 6, 9, 11));
        p.drawLine(QPointF(8.5, 9), QPointF(8.5, 14));
        p.drawLine(QPointF(11.5, 9), QPointF(11.5, 14));
        break;
    case ToolIcon::Eye: paintEye(&p, QRect(1, 2, 18, 16), true, c); break;
    case ToolIcon::Lock: paintLock(&p, QRect(2, 2, 16, 16), true, c); break;
    case ToolIcon::Solo: paintSolo(&p, QRect(2, 2, 16, 16), false, c); break;
    }
    return QIcon(pm);
}

LayerHeader::LayerHeader(QWidget* parent) : QHeaderView(Qt::Vertical, parent) {
    setSectionsClickable(true);
    setSectionsMovable(true); // arrastar = mudar a ordem das layers
    setHighlightSections(true);
    setDefaultAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    setMinimumWidth(170);
}

void LayerHeader::setStates(std::vector<RowState> states) {
    states_ = std::move(states);
    viewport()->update();
}

// Ícones à direita do nome, como no Premiere: [olho] [cadeado] [S]
QRect LayerHeader::eyeRect(const QRect& s) const { return {s.right() - 64, s.center().y() - 8, 18, 16}; }
QRect LayerHeader::lockRect(const QRect& s) const { return {s.right() - 42, s.center().y() - 8, 16, 16}; }
QRect LayerHeader::soloRect(const QRect& s) const { return {s.right() - 20, s.center().y() - 8, 16, 16}; }

QSize LayerHeader::sectionSizeFromContents(int logicalIndex) const {
    QSize s = QHeaderView::sectionSizeFromContents(logicalIndex);
    s.setWidth(std::max(s.width() + 76, 170));
    return s;
}

void LayerHeader::paintSection(QPainter* p, const QRect& rect, int logicalIndex) const {
    const bool selected = selectionModel() && selectionModel()->isRowSelected(logicalIndex, rootIndex());
    p->save();
    p->fillRect(rect, selected ? QColor(255, 136, 0) : QColor(52, 52, 52));
    p->setPen(QColor(30, 30, 30));
    p->drawLine(rect.bottomLeft(), rect.bottomRight());
    const QString text = model() ? model()->headerData(logicalIndex, orientation()).toString() : QString();
    const RowState st = logicalIndex < static_cast<int>(states_.size()) ? states_[static_cast<std::size_t>(logicalIndex)]
                                                                        : RowState{};
    const QColor fg = selected ? QColor(20, 20, 20) : (st.visible ? QColor(225, 225, 225) : QColor(130, 130, 130));
    p->setPen(fg);
    p->drawText(rect.adjusted(8, 0, -72, 0), Qt::AlignVCenter | Qt::AlignLeft,
                p->fontMetrics().elidedText(text, Qt::ElideRight, rect.width() - 80));
    paintEye(p, eyeRect(rect), st.visible, fg);
    paintLock(p, lockRect(rect), st.locked, st.locked ? QColor(255, 176, 74) : fg);
    paintSolo(p, soloRect(rect), st.solo, fg);
    p->restore();
}

void LayerHeader::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const int logical = logicalIndexAt(e->position().toPoint());
        if (logical >= 0) {
            const QRect s(0, sectionViewportPosition(logical), viewport()->width(), sectionSize(logical));
            if (eyeRect(s).adjusted(-3, -3, 3, 3).contains(e->position().toPoint())) {
                emit visibilityToggled(logical);
                return;
            }
            if (lockRect(s).adjusted(-3, -3, 3, 3).contains(e->position().toPoint())) {
                emit lockToggled(logical);
                return;
            }
            if (soloRect(s).adjusted(-3, -3, 3, 3).contains(e->position().toPoint())) {
                emit soloToggled(logical);
                return;
            }
        }
    }
    QHeaderView::mousePressEvent(e);
}

void LayerHeader::contextMenuEvent(QContextMenuEvent* e) {
    const int logical = logicalIndexAt(e->pos());
    if (logical >= 0) emit contextMenuRequested(logical, e->globalPos());
}
