#pragma once
// Cabeçalho das layers do Dashboard: nome + olho (mostrar/esconder) + cadeado
// (bloquear). Arrastar uma layer muda a ordem; botão direito abre o menu.

#include <QHeaderView>

#include <vector>

class LayerHeader : public QHeaderView {
    Q_OBJECT
public:
    explicit LayerHeader(QWidget* parent = nullptr);

    struct RowState {
        bool visible = true;
        bool locked = false;
    };
    void setStates(std::vector<RowState> states);

signals:
    void visibilityToggled(int row);
    void lockToggled(int row);
    void contextMenuRequested(int row, const QPoint& globalPos);

protected:
    void paintSection(QPainter* p, const QRect& rect, int logicalIndex) const override;
    void mousePressEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    QSize sectionSizeFromContents(int logicalIndex) const override;

private:
    QRect eyeRect(const QRect& section) const;
    QRect lockRect(const QRect& section) const;
    std::vector<RowState> states_;
};

// Desenhos simples reutilizados pela timeline.
void paintEye(QPainter* p, const QRect& r, bool open, const QColor& color);
void paintLock(QPainter* p, const QRect& r, bool locked, const QColor& color);
