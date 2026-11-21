#pragma once
// Grelha do CUEPLAYER (como no Resolume): layers × colunas de clips.
//   - Cabeçalho de cada layer: nome e botões X (limpar), B (bypass), S (solo),
//     V (imagem) e A (som). A layer de cima é a última (fica por cima na imagem).
//   - Cabeçalho de cada coluna: clicar lança a coluna inteira.
//   - Células: miniatura e nome do clip; clicar lança o clip (Ctrl+clique só
//     seleciona, para ver no Preview). Arrastar ficheiros do Explorador para uma
//     célula; arrastar um clip para outra célula move-o (Ctrl = copia).

#include "vthost/core/project.h"

#include <QHash>
#include <QWidget>

class MediaCache;

class CompGrid : public QWidget {
    Q_OBJECT
public:
    CompGrid(MediaCache* media, QWidget* parent = nullptr);

    void setComposition(const vthost::Project* project, const QString& compId);
    QString compositionId() const { return compId_; }
    // Clip no ar em cada layer: coluna e progresso 0..1.
    void setPlaying(const QHash<QString, QPair<QString, double>>& playing);
    void setSelection(const QString& layer, const QString& column);
    QString selectedLayer() const { return selLayer_; }
    QString selectedColumn() const { return selColumn_; }
    void setEditable(bool editable) { editable_ = editable; }
    QSize sizeHint() const override;

signals:
    void launchClip(const QString& layer, const QString& column);
    void launchColumn(const QString& column);
    void selectionChanged(const QString& layer, const QString& column);
    void clearLayer(const QString& layer);
    void layerToggle(const QString& layer, QChar which); // 'B', 'S', 'V', 'A'
    void layerRename(const QString& layer);
    void layerMenu(const QString& layer, const QPoint& globalPos);
    void columnMenu(const QString& column, const QPoint& globalPos);
    void cellMenu(const QString& layer, const QString& column, const QPoint& globalPos);
    void filesDropped(const QString& layer, const QString& column, const QStringList& files);
    void clipMoved(const QString& fromLayer, const QString& fromColumn, const QString& toLayer, const QString& toColumn,
                   bool copy);
    void addColumnRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    struct Hit {
        enum Kind { None, Cell, LayerHeader, LayerButton, ColumnHeader, AddColumn } kind = None;
        int row = -1;    // linha no ecrã (0 = em cima)
        int column = -1;
        QChar button;    // LayerButton
    };
    Hit hitAt(const QPoint& p) const;
    const vthost::Composition* comp() const;
    int layerIndexForRow(int row) const; // linha no ecrã -> índice em comp.layers
    QRect cellRect(int row, int column) const;
    QRect layerButtonRect(int row, QChar which) const;

    MediaCache* media_;
    const vthost::Project* project_ = nullptr;
    QString compId_;
    QHash<QString, QPair<QString, double>> playing_;
    QString selLayer_;
    QString selColumn_;
    bool editable_ = true;
    QPoint pressPos_;
    Hit pressHit_;
    bool dragStarted_ = false;
};
