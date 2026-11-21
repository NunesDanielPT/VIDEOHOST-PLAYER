#include "transport_bar.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace {
QString formatTime(double s) {
    if (!std::isfinite(s) || s < 0.0) s = 0.0;
    const int total = static_cast<int>(s);
    const int h = total / 3600;
    const int m = (total / 60) % 60;
    const int sec = total % 60;
    const int tenths = static_cast<int>((s - total) * 10.0);
    if (h > 0) return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2.%3").arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0')).arg(tenths);
}
} // namespace

// ------------------------------------------------------------------ TimeBar

TimeBar::TimeBar(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(18);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TimeBar::setTimes(double position, double duration) {
    position_ = position;
    duration_ = duration;
    update();
}

double TimeBar::timeAt(int x) const {
    if (duration_ <= 0.0 || width() <= 2) return 0.0;
    return std::clamp(static_cast<double>(x) / (width() - 1), 0.0, 1.0) * duration_;
}

void TimeBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 4.5, -0.5, -4.5);
    p.setPen(QColor(70, 70, 70));
    p.setBrush(QColor(28, 28, 28));
    p.drawRoundedRect(r, 3, 3);
    if (duration_ <= 0.0 || !isEnabled()) return;
    const double f = std::clamp(position_ / duration_, 0.0, 1.0);
    QRectF fill = r.adjusted(1, 1, -1, -1);
    fill.setWidth(fill.width() * f);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 150, 220));
    p.drawRoundedRect(fill, 2, 2);
    // Cabeça de leitura.
    const double x = r.left() + r.width() * f;
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(x - 2.0, 1.0, 4.0, height() - 2.0), 1.5, 1.5);
}

void TimeBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || duration_ <= 0.0) return;
    dragging_ = true;
    position_ = timeAt(e->position().toPoint().x());
    update();
    emit seekRequested(position_, false);
}

void TimeBar::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    position_ = timeAt(e->position().toPoint().x());
    update();
    emit seekRequested(position_, false);
}

void TimeBar::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_ || e->button() != Qt::LeftButton) return;
    dragging_ = false;
    position_ = timeAt(e->position().toPoint().x());
    update();
    emit seekRequested(position_, true);
}

// ------------------------------------------------------------------ TransportBar

TransportBar::TransportBar(QWidget* parent) : QWidget(parent) {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(6, 0, 6, 4);
    l->setSpacing(6);
    playPause_ = new QToolButton(this);
    playPause_->setAutoRaise(true);
    playPause_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    playPause_->setToolTip(tr("Pause / resume the video of the selected layer"));
    connect(playPause_, &QToolButton::clicked, this, [this] { emit pauseRequested(!paused_); });
    title_ = new QLabel(this);
    title_->setMinimumWidth(80);
    title_->setMaximumWidth(260);
    title_->setStyleSheet(QStringLiteral("color: #bbb;"));
    elapsed_ = new QLabel(QStringLiteral("00:00.0"), this);
    remaining_ = new QLabel(QStringLiteral("-00:00.0"), this);
    const QFont mono = QFont(QStringLiteral("Consolas"));
    elapsed_->setFont(mono);
    remaining_->setFont(mono);
    bar_ = new TimeBar(this);
    l->addWidget(playPause_);
    l->addWidget(title_);
    l->addWidget(elapsed_);
    l->addWidget(bar_, 1);
    l->addWidget(remaining_);

    connect(bar_, &TimeBar::seekRequested, this, [this](double t, bool finished) {
        hasSeekTarget_ = true;
        seekTarget_ = t;
        sinceSeek_.start();
        showTimes(t);
        emit seekRequested(t, finished);
    });
    timer_.setInterval(40);
    connect(&timer_, &QTimer::timeout, this, &TransportBar::tick);
    sinceStatus_.start();
    setUnavailable(tr("No video on the selected layer"));
}

void TransportBar::setUnavailable(const QString& message) {
    running_ = false;
    duration_ = 0.0;
    timer_.stop();
    title_->setText(message);
    title_->setToolTip(QString());
    elapsed_->setText(QStringLiteral("--:--.-"));
    remaining_->setText(QStringLiteral("--:--.-"));
    bar_->setTimes(0.0, 0.0);
    bar_->setEnabled(false);
    playPause_->setEnabled(false);
}

void TransportBar::setState(const QString& title, double position, double duration, bool paused, bool playing) {
    if (duration <= 0.0) {
        setUnavailable(title.isEmpty() ? tr("No video on the selected layer") : title);
        return;
    }
    title_->setText(fontMetrics().elidedText(title, Qt::ElideMiddle, title_->maximumWidth()));
    title_->setToolTip(title);
    bar_->setEnabled(true);
    playPause_->setEnabled(true);
    duration_ = duration;
    paused_ = paused;
    running_ = playing && !paused;
    playPause_->setIcon(style()->standardIcon(paused ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    // Depois de um salto, ignora estados antigos até o motor lá chegar (evita "saltos para trás").
    if (hasSeekTarget_) {
        if (std::abs(position - seekTarget_) < 0.5 || sinceSeek_.elapsed() > 1500) hasSeekTarget_ = false;
        else position = seekTarget_;
    }
    basePosition_ = position;
    sinceStatus_.restart();
    if (running_) {
        if (!timer_.isActive()) timer_.start();
    } else {
        timer_.stop();
    }
    if (!bar_->dragging()) showTimes(position);
}

void TransportBar::tick() {
    if (!running_ || bar_->dragging() || duration_ <= 0.0) return;
    double t = basePosition_ + sinceStatus_.elapsed() / 1000.0;
    if (t > duration_) t = std::fmod(t, duration_); // vídeo em loop
    showTimes(t);
}

void TransportBar::showTimes(double position) {
    position = std::clamp(position, 0.0, std::max(0.0, duration_));
    elapsed_->setText(formatTime(position));
    remaining_->setText(QStringLiteral("-") + formatTime(duration_ - position));
    if (!bar_->dragging()) bar_->setTimes(position, duration_);
}
