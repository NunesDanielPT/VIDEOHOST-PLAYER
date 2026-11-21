#pragma once
// Separador Timeline: transporte (play/pausa/stop), timecode, zoom e a timeline.

#include "timeline_widget.h"

#include <QWidget>

class QLabel;
class QScrollBar;
class QSlider;
class QToolButton;

class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    TimelinePanel(MediaCache* cache, QWidget* parent = nullptr);

    TimelineWidget* timeline() const { return timeline_; }
    void setState(double seconds, bool playing, bool active);
    void setEditable(bool editable);

signals:
    void playRequested();
    void pauseRequested();
    void stopRequested();

private:
    void syncScrollBar();
    void updateTimecode();

    TimelineWidget* timeline_;
    QToolButton* playPause_;
    QToolButton* stop_;
    QToolButton* addSegment_;
    QLabel* timecode_;
    QSlider* zoom_;
    QScrollBar* scroll_;
    QTimer clock_;
    bool playing_ = false;
};
