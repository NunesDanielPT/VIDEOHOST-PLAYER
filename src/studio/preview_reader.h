#pragma once
// Lê um dos blocos de preview que o motor escreve em memória partilhada
// (seqlock: nunca espera pelo motor). Usado pelo editor de Output avançado para
// mostrar de fundo a fonte de um slice ou o que sai de um screen.

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>

class PreviewReader : public QObject {
    Q_OBJECT
public:
    PreviewReader(int slot, QObject* parent = nullptr); // 0, 1 ou 2
    ~PreviewReader() override;

    void setActive(bool active);           // pára de ler quando não é preciso
    const QImage& frame() const { return frame_; }
    void clear() { frame_ = QImage(); lastFrame_.invalidate(); }
    bool live() const { return lastFrame_.isValid() && lastFrame_.elapsed() < 1500; }

signals:
    void frameUpdated();

private:
    void poll();
    bool openMapping();

    int slot_;
    void* mapping_ = nullptr;
    const unsigned char* block_ = nullptr;
    QTimer timer_;
    QElapsedTimer lastFrame_;
    QElapsedTimer lastOpenAttempt_;
    quint64 lastFrameNumber_ = 0;
    QImage frame_;
    bool wasLive_ = false;
};
