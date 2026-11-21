#include "timeline_panel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

TimelinePanel::TimelinePanel(MediaCache* cache, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 0);
    playPause_ = new QToolButton(this);
    playPause_->setText(QStringLiteral("▶"));
    playPause_->setToolTip(tr("Play / pause the timeline"));
    playPause_->setFixedSize(40, 32);
    playPause_->setStyleSheet(QStringLiteral("font-size: 16px;"));
    stop_ = new QToolButton(this);
    stop_->setText(QStringLiteral("■"));
    stop_->setToolTip(tr("Stop the timeline"));
    stop_->setFixedSize(32, 32);
    timecode_ = new QLabel(this);
    timecode_->setStyleSheet(QStringLiteral("font-family: Consolas, monospace; font-size: 18px; padding: 0 12px;"));
    addSegment_ = new QToolButton(this);
    addSegment_->setText(tr("+ Segment"));
    addSegment_->setToolTip(tr("Add a segment at the playhead"));
    zoom_ = new QSlider(Qt::Horizontal, this);
    zoom_->setRange(0, 100);
    zoom_->setFixedWidth(140);
    zoom_->setToolTip(tr("Zoom (Ctrl + mouse wheel)"));

    bar->addWidget(playPause_);
    bar->addWidget(stop_);
    bar->addWidget(timecode_);
    bar->addWidget(addSegment_);
    bar->addStretch();
    bar->addWidget(new QLabel(tr("Zoom"), this));
    bar->addWidget(zoom_);
    layout->addLayout(bar);

    timeline_ = new TimelineWidget(cache, this);
    layout->addWidget(timeline_, 1);
    scroll_ = new QScrollBar(Qt::Horizontal, this);
    layout->addWidget(scroll_);

    connect(playPause_, &QToolButton::clicked, this, [this] { playing_ ? emit pauseRequested() : emit playRequested(); });
    connect(stop_, &QToolButton::clicked, this, &TimelinePanel::stopRequested);
    connect(addSegment_, &QToolButton::clicked, this, [this] { timeline_->addSegmentAt(timeline_->playhead()); });

    // Zoom logarítmico: 2..600 px por segundo.
    auto zoomToSlider = [](double px) { return static_cast<int>(std::log(px / 2.0) / std::log(300.0) * 100.0 + 0.5); };
    zoom_->setValue(zoomToSlider(timeline_->zoom()));
    connect(zoom_, &QSlider::valueChanged, this, [this](int v) {
        timeline_->setZoom(2.0 * std::pow(300.0, v / 100.0));
    });
    connect(timeline_, &TimelineWidget::viewChanged, this, [this, zoomToSlider] {
        const QSignalBlocker b(zoom_);
        zoom_->setValue(zoomToSlider(timeline_->zoom()));
        syncScrollBar();
    });
    connect(scroll_, &QScrollBar::valueChanged, this, [this](int v) { timeline_->setScrollSeconds(v / 10.0); });

    clock_.setInterval(50);
    connect(&clock_, &QTimer::timeout, this, &TimelinePanel::updateTimecode);
    updateTimecode();
}

void TimelinePanel::syncScrollBar() {
    const QSignalBlocker b(scroll_);
    const double content = timeline_->contentSeconds();
    const double visible = timeline_->visibleSeconds();
    scroll_->setRange(0, static_cast<int>(std::max(0.0, content - visible * 0.5) * 10));
    scroll_->setPageStep(static_cast<int>(visible * 10));
    scroll_->setValue(static_cast<int>(timeline_->scrollSeconds() * 10));
}

void TimelinePanel::updateTimecode() {
    // Formato como no Millumin: mm : ss : mmm
    const double t = std::max(0.0, timeline_->playhead());
    const int ms = static_cast<int>(std::fmod(t, 1.0) * 1000.0);
    const int total = static_cast<int>(t);
    timecode_->setText(QStringLiteral("%1 : %2 : %3")
                           .arg(total / 60, 2, 10, QLatin1Char('0'))
                           .arg(total % 60, 2, 10, QLatin1Char('0'))
                           .arg(ms, 3, 10, QLatin1Char('0')));
}

void TimelinePanel::setState(double seconds, bool playing, bool active) {
    playing_ = playing && active;
    timeline_->setPlayhead(active ? seconds : timeline_->playhead(), playing_);
    playPause_->setText(playing_ ? QStringLiteral("❚❚") : QStringLiteral("▶"));
    if (playing_) clock_.start();
    else clock_.stop();
    updateTimecode();
}

void TimelinePanel::setEditable(bool editable) {
    timeline_->setEditable(editable);
    addSegment_->setEnabled(editable);
}
