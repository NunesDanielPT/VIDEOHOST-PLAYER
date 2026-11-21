#pragma once
// Preview do que está no ar (lido da memória partilhada escrita pelo motor),
// como no Mitti: imagem grande, tempo decorrido em cima à esquerda e tempo que
// falta em cima à direita.

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    // slot 0 = imagem principal (MEDIAPLAYER ou composição no ar), 1 = monitor Preview do CUEPLAYER.
    explicit PreviewWidget(QWidget* parent = nullptr, int slot = 0);
    ~PreviewWidget() override;

    void setCanvasSize(int w, int h);
    void setFps(double fps) { fps_ = fps; }
    // Textos dos cantos ("" = escondido) e cor do tempo que falta.
    // Texto quando não chega imagem ("" = "No signal from the engine").
    void setIdleText(const QString& text) { idleText_ = text; update(); }
    void setTimes(const QString& elapsed, const QString& remaining, const QColor& remainingColor);

signals:
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent*) override { emit doubleClicked(); }

private:
    void poll();
    bool openMapping();
    QRectF canvasRect() const;

    int slot_ = 0;
    void* mapping_ = nullptr;
    const unsigned char* block_ = nullptr;
    QTimer timer_;
    QElapsedTimer lastFrame_;
    QElapsedTimer lastOpenAttempt_;
    quint64 lastFrameNumber_ = 0;
    QImage frame_;
    bool live_ = false;

    int canvasW_ = 1920;
    int canvasH_ = 1080;
    double fps_ = 0.0;
    QString idleText_;
    QString elapsed_;
    QString remaining_;
    QColor remainingColor_ = Qt::white;
};
