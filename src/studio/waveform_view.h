#pragma once
// Forma de onda da cue selecionada (como no Mitti): régua de tempo, canais L/R,
// zona In..Out em destaque, pegas In/Out arrastáveis e cabeça de leitura.
// Clicar/arrastar na onda da cue que está no ar = saltar para esse ponto.

#include <QVector>
#include <QWidget>

class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);

    // Dados da cue selecionada (peaks: pico L,R por bloco).
    void setData(const QVector<float>& peaks, double blocksPerSecond, double fileDuration, bool analyzing,
                 bool hasAudio);
    void setInOut(double in, double out); // out 0 = fim do ficheiro
    void setPlayhead(double fileTime);    // < 0 = escondida (a cue no ar é outra)
    void setEditable(bool editable) { editable_ = editable; }
    QSize sizeHint() const override { return {600, 130}; }

signals:
    void seekRequested(double fileTime, bool finished);
    void inOutChanged(double in, double out, bool finished);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    enum class Drag { None, Seek, In, Out };
    QRectF waveRect() const;
    double timeAt(double x) const;
    double xAt(double t) const;
    double effectiveOut() const { return out_ > in_ ? out_ : duration_; }

    QVector<float> peaks_;
    double bps_ = 0.0;
    double duration_ = 0.0;
    bool analyzing_ = false;
    bool hasAudio_ = true;
    double in_ = 0.0;
    double out_ = 0.0;
    double playhead_ = -1.0;
    bool editable_ = true;
    Drag drag_ = Drag::None;
};
