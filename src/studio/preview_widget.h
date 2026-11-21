#pragma once
// Workspace: preview em direto do canvas que está no ar (lido da memória
// partilhada escrita pelo motor). Mostra o contorno da layer selecionada e
// permite movê-la arrastando com o rato.

#include "vthost/core/project.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPolygonF>
#include <QTimer>
#include <QWidget>

#include <optional>

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    // Contorno da layer selecionada (tamanho da media em píxeis; 0 = desconhecido).
    void setSelection(const std::optional<vthost::LayerProps>& props, int mediaW, int mediaH);
    void setCanvasSize(int w, int h);
    void setEditable(bool editable) { editable_ = editable; }
    void setFps(double fps) { fps_ = fps; }

signals:
    // Deslocamento pedido pelo utilizador (píxeis do canvas).
    void layerDragged(double dx, double dy, bool finished);
    // Arrastar um canto laranja = corner pin desse canto (0..3: sup-esq, sup-dir, inf-dir, inf-esq).
    void cornerDragged(int corner, double dx, double dy, bool finished);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void poll();
    bool openMapping();
    QRectF canvasRect() const;

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
    std::optional<vthost::LayerProps> selection_;
    int selMediaW_ = 0;
    int selMediaH_ = 0;
    bool editable_ = true;
    bool dragging_ = false;
    int dragCorner_ = -1;
    QPointF dragStart_;
    QPolygonF handles_; // cantos da layer selecionada no ecrã (para o rato)
};
