#pragma once
// Barra de tempo por baixo do Workspace (como no Millumin): mostra o vídeo que
// está no ar na layer selecionada, tempo decorrido/restante, e permite saltar
// (arrastar na barra) e pausar.

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QToolButton;

class TimeBar : public QWidget {
    Q_OBJECT
public:
    explicit TimeBar(QWidget* parent = nullptr);
    void setTimes(double position, double duration);
    bool dragging() const { return dragging_; }
    QSize sizeHint() const override { return {300, 22}; }

signals:
    void seekRequested(double seconds, bool finished);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    double timeAt(int x) const;
    double position_ = 0.0;
    double duration_ = 0.0;
    bool dragging_ = false;
};

class TransportBar : public QWidget {
    Q_OBJECT
public:
    explicit TransportBar(QWidget* parent = nullptr);

    // duration <= 0: sem vídeo (imagem, nada no ar ou timeline activa) -> barra inactiva.
    void setState(const QString& title, double position, double duration, bool paused, bool playing);
    void setUnavailable(const QString& message);

signals:
    void seekRequested(double seconds, bool finished);
    void pauseRequested(bool pause);

private:
    void tick();
    void showTimes(double position);

    QToolButton* playPause_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* elapsed_ = nullptr;
    QLabel* remaining_ = nullptr;
    TimeBar* bar_ = nullptr;
    QTimer timer_;
    QElapsedTimer sinceStatus_;
    double basePosition_ = 0.0;
    double duration_ = 0.0;
    bool paused_ = false;
    bool running_ = false; // a tocar (interpola localmente entre estados do motor)
    bool hasSeekTarget_ = false;
    double seekTarget_ = 0.0;
    QElapsedTimer sinceSeek_;
};
