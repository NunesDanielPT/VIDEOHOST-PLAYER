#pragma once
// Editor de mapping de uma saída (warp): perspetiva pelos 4 cantos e malha de
// pontos para superfícies irregulares. Cada alteração vai logo para o motor,
// por isso vê-se no projetor enquanto se arrasta (com o test card ligado).
//
// Rato: arrastar cantos/pontos. Teclado: setas = 1 píxel, Shift+setas = 10 píxeis,
// Tab = ponto seguinte.

#include "vthost/core/mapping.h"

#include <QDialog>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

class MappingCanvas : public QWidget {
    Q_OBJECT
public:
    explicit MappingCanvas(QWidget* parent = nullptr);
    void setMapping(const vthost::OutputMapping& m, int outW, int outH);
    const vthost::OutputMapping& mapping() const { return m_; }

signals:
    void edited();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QRectF area() const;
    QPointF toScreen(const vthost::WarpPoint& p) const;
    vthost::WarpPoint fromScreen(const QPointF& p) const;
    int hitTest(const QPointF& pos) const; // 0..3 cantos, 100+i pontos da malha, -1 nada
    vthost::WarpPoint handlePos(int handle) const;
    void moveHandle(int handle, const vthost::WarpPoint& to);

    vthost::OutputMapping m_;
    int outW_ = 1920;
    int outH_ = 1080;
    int selected_ = -1;
    bool dragging_ = false;
};

class MappingEditor : public QDialog {
    Q_OBJECT
public:
    using LiveFn = std::function<void(const vthost::OutputMapping&)>;
    MappingEditor(const QString& outputName, const vthost::OutputMapping& mapping, int outW, int outH, LiveFn live,
                  bool testCard, std::function<void(bool)> setTestCard, QWidget* parent = nullptr);

    vthost::OutputMapping mapping() const { return canvas_->mapping(); }

private:
    void sendLive();
    void syncControls();

    MappingCanvas* canvas_;
    QComboBox* mode_;
    QSpinBox* cols_;
    QSpinBox* rows_;
    QLabel* hint_;
    LiveFn live_;
    bool loading_ = false;
};
