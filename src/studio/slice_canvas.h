#pragma once
// Editor gráfico dos slices de um screen (Output avançado, como no Resolume).
//
//   Input Selection:       a fonte do slice (MEDIAPLAYER, composição ou layer) e o
//                          retângulo que dela se recorta. Arrastar move, os cantos
//                          e lados redimensionam.
//   Output Transformation: o screen com todos os slices. Clicar escolhe o slice;
//                          três modos de edição:
//                            Transform  arrastar move, cantos redimensionam;
//                            Corners    cada canto à mão (corner pin);
//                            Points     pontos de edição (malha) dentro do slice.
// Durante o arrasto emite editing() (o diálogo manda logo ao motor) e no fim
// edited() (vai para o projecto e para o desfazer).

#include "vthost/core/project.h"

#include <QWidget>

#include <array>
#include <functional>
#include <vector>

class SliceCanvas : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Input, Output };
    enum class Tool { Transform, Corners, Points };

    explicit SliceCanvas(Mode mode, QWidget* parent = nullptr);

    // Tamanho (px) da fonte de um slice: MEDIAPLAYER, composição ou layer.
    using SourceSize = std::function<QSize(const vthost::Slice&)>;
    using SourceName = std::function<QString(const vthost::Slice&)>;
    void setSourceInfo(SourceSize size, SourceName name);

    void setOutput(vthost::Output* output, int currentSlice); // nullptr = vazio
    void setTool(Tool tool) { tool_ = tool; update(); }
    void setEditable(bool editable) { editable_ = editable; }

signals:
    void sliceClicked(int index);
    void editing();
    void edited();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    vthost::Slice* current() const;
    QSize sourceSize(const vthost::Slice& s) const;
    QRectF area() const;                       // retângulo do screen/fonte no widget
    QSizeF spaceSize() const;                  // px do espaço editado (screen ou fonte)
    QPointF toWidget(double x, double y) const; // px do espaço -> widget
    QPointF toSpace(const QPointF& p) const;
    std::array<QPointF, 4> sliceCorners(const vthost::Slice& s) const;      // ecrã, px (com espelho)
    std::array<QPointF, 4> transformCorners(const vthost::Slice& s) const;  // ecrã, px (sem espelho, para as pegas)
    QRectF inputRect(const vthost::Slice& s) const;                         // fonte, px
    std::vector<QPointF> meshPoints(const vthost::Slice& s) const;          // ecrã, px

    Mode mode_;
    Tool tool_ = Tool::Transform;
    bool editable_ = true;
    vthost::Output* output_ = nullptr;
    int current_ = -1;
    SourceSize sourceSize_;
    SourceName sourceName_;

    enum class Drag { None, Move, Corner, Edge, MeshPoint };
    Drag drag_ = Drag::None;
    int dragIndex_ = -1;           // canto / lado / ponto
    QPointF dragStart_;            // espaço
    vthost::Slice dragOrig_;
};
