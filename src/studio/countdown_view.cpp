#include "countdown_view.h"

#include "prefs.h"

#include <QKeyEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

using namespace vthost;

namespace {
QColor colorFor(const Countdown& c) {
    if (!c.active) return QColor(110, 110, 110);
    if (!c.running) return QColor(120, 180, 255);
    if (c.remaining <= prefs::countdownDangerSeconds()) return QColor(255, 70, 60);
    if (c.remaining <= prefs::countdownWarnSeconds()) return QColor(255, 170, 0);
    return QColor(240, 240, 240);
}

void fitFont(QPainter& p, const QString& text, const QRectF& box, bool bold, double maxPx) {
    QFont f = p.font();
    f.setBold(bold);
    f.setPixelSize(std::max(8, static_cast<int>(std::min(maxPx, box.height()))));
    p.setFont(f);
    const double w = p.fontMetrics().horizontalAdvance(text);
    if (w > box.width() && w > 0) {
        f.setPixelSize(std::max(8, static_cast<int>(f.pixelSize() * box.width() / w)));
        p.setFont(f);
    }
}
} // namespace

CountdownView::CountdownView(bool compact, QWidget* parent) : QWidget(parent), compact_(compact) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setToolTip(compact ? tr("Countdown of what is on air. Double-click to open the countdown window.") : QString());
}

QSize CountdownView::sizeHint() const { return compact_ ? QSize(230, 44) : QSize(800, 450); }

void CountdownView::setCountdown(const Countdown& c) {
    c_ = c;
    update();
}

void CountdownView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), compact_ ? QColor(22, 22, 22) : Qt::black);
    const QColor col = colorFor(c_);
    const QString time = c_.active ? QStringLiteral("-") + QString::fromStdString(formatCountdown(c_.remaining))
                                   : QStringLiteral("--:--");
    QString title;
    if (c_.mode == "timeline") title = QString::fromStdString(c_.cueName);
    else if (c_.cueNumber > 0)
        title = QStringLiteral("%1  %2").arg(c_.cueNumber, 2, 10, QLatin1Char('0')).arg(QString::fromStdString(c_.cueName));
    QString status;
    if (!c_.active) status = tr("Nothing on air");
    else if (!c_.running) status = tr("Paused");
    const double frac = (c_.active && c_.duration > 0.0) ? std::clamp(c_.elapsed / c_.duration, 0.0, 1.0) : 0.0;

    if (compact_) {
        const QRectF r = QRectF(rect()).adjusted(6, 2, -6, -6);
        fitFont(p, time, QRectF(r.left(), r.top(), r.width() * 0.55, r.height()), true, r.height() * 0.85);
        p.setPen(col);
        p.drawText(QRectF(r.left(), r.top(), r.width() * 0.55, r.height()), Qt::AlignVCenter | Qt::AlignLeft, time);
        QFont small = p.font();
        small.setBold(false);
        small.setPixelSize(11);
        p.setFont(small);
        p.setPen(QColor(170, 170, 170));
        const QRectF info(r.left() + r.width() * 0.56, r.top(), r.width() * 0.44, r.height());
        const QString line2 = status.isEmpty() && !c_.nextName.empty()
                                  ? tr("Next: %1").arg(QString::fromStdString(c_.nextName))
                                  : status;
        p.drawText(info, Qt::AlignLeft | Qt::AlignTop,
                   p.fontMetrics().elidedText(title, Qt::ElideRight, static_cast<int>(info.width())));
        p.drawText(info, Qt::AlignLeft | Qt::AlignBottom,
                   p.fontMetrics().elidedText(line2, Qt::ElideRight, static_cast<int>(info.width())));
        p.fillRect(QRectF(0, height() - 3, width(), 3), QColor(50, 50, 50));
        p.fillRect(QRectF(0, height() - 3, width() * frac, 3), col);
        return;
    }

    const double h = height();
    const QRectF titleBox(width() * 0.05, h * 0.04, width() * 0.9, h * 0.14);
    const QRectF timeBox(width() * 0.03, h * 0.2, width() * 0.94, h * 0.52);
    const QRectF nextBox(width() * 0.05, h * 0.76, width() * 0.9, h * 0.1);
    p.setPen(QColor(200, 200, 200));
    fitFont(p, title, titleBox, false, titleBox.height() * 0.7);
    p.drawText(titleBox, Qt::AlignCenter, title);
    p.setPen(col);
    fitFont(p, time, timeBox, true, timeBox.height());
    p.drawText(timeBox, Qt::AlignCenter, time);
    QString bottom = status;
    if (bottom.isEmpty() && !c_.nextName.empty()) bottom = tr("Next: %1").arg(QString::fromStdString(c_.nextName));
    p.setPen(QColor(160, 160, 160));
    fitFont(p, bottom, nextBox, false, nextBox.height() * 0.7);
    p.drawText(nextBox, Qt::AlignCenter, bottom);
    const QRectF bar(width() * 0.05, h * 0.9, width() * 0.9, std::max(4.0, h * 0.02));
    p.fillRect(bar, QColor(45, 45, 45));
    p.fillRect(QRectF(bar.left(), bar.top(), bar.width() * frac, bar.height()), col);
}

// ------------------------------------------------------------------ janela

CountdownWindow::CountdownWindow(QWidget* parent) : QWidget(parent, Qt::Window) {
    setWindowTitle(CountdownView::tr("VTHOST - Countdown")); // contexto do ficheiro (traduções)
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    view_ = new CountdownView(false, this);
    l->addWidget(view_);
    connect(view_, &CountdownView::doubleClicked, this, &CountdownWindow::toggleFullScreen);
    resize(800, 450);
}

void CountdownWindow::toggleFullScreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
}

void CountdownWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_F11) toggleFullScreen();
    else if (e->key() == Qt::Key_Escape && isFullScreen()) showNormal();
    else QWidget::keyPressEvent(e);
}
