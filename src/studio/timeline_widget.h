#pragma once
// Timeline: régua, segmentos, uma faixa por layer com clips e a cabeça de leitura.
//
// Rato:  clicar/arrastar na régua = posicionar a cabeça de leitura
//        arrastar um clip = mover (também para outra layer), arrastar a borda direita = duração
//        largar media da Library numa faixa = clip novo
//        Ctrl+roda = zoom, roda = deslocar no tempo
// Teclado: Delete = apagar o clip selecionado

#include "vthost/core/project.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class MediaCache;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(MediaCache* cache, QWidget* parent = nullptr);

    void setProject(vthost::Project* project, const QString& timelineId);
    void setPlayhead(double seconds, bool playing);
    double playhead() const;
    void setEditable(bool editable) { editable_ = editable; }

    double zoom() const { return pxPerSec_; }
    void setZoom(double pxPerSec);
    double scrollSeconds() const { return scroll_; }
    void setScrollSeconds(double s);
    double visibleSeconds() const;
    double contentSeconds() const;

    void addSegmentAt(double t);
    bool cutSelectedClipAtPlayhead();

signals:
    void seekRequested(double seconds, bool final);
    void edited();
    void layerClicked(int row);
    void viewChanged();
    void layerContextMenu(int row, const QPoint& globalPos);
    void layerVisibilityToggled(int row);
    void layerMoved(int from, int to);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    enum class Drag { None, Scrub, MoveClip, ResizeClip, FadeIn, FadeOut, MoveLayer };

    vthost::Timeline* timeline() const;
    double timeToX(double t) const;
    double xToTime(double x) const;
    int rowAt(double y) const;
    double rowTop(int row) const;
    int clipAt(const QPointF& pos, bool* onRightEdge) const;
    int fadeHandleAt(const QPointF& pos, bool* isFadeIn) const;
    QRectF clipRect(const vthost::Clip& c, int row) const;
    int rowOfLayer(const std::string& layerId) const;
    void setClipFade(int clipIndex, bool fadeIn);
    int segmentAt(double x) const;
    double snap(double t, int ignoreClip) const;
    QString newClipId() const;
    void emitSeek(double t, bool final);

    MediaCache* cache_;
    vthost::Project* project_ = nullptr;
    QString timelineId_;
    bool editable_ = true;

    double pxPerSec_ = 40.0;
    double scroll_ = 0.0;

    double playheadBase_ = 0.0;
    bool playing_ = false;
    QElapsedTimer playClock_;
    QTimer repaint_;

    Drag drag_ = Drag::None;
    int dragClip_ = -1;
    double dragGrabOffset_ = 0.0;
    vthost::Clip dragOriginal_;
    QElapsedTimer seekThrottle_;
    QString selectedClipId_;
    double dropPreviewTime_ = -1.0;
    int dropPreviewRow_ = -1;
    int dragLayerFrom_ = -1;
    int dragLayerTo_ = -1;
};
