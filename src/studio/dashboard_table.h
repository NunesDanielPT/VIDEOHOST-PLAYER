#pragma once
// Dashboard: grelha layers × colunas (cues) com miniaturas. Aceita media
// arrastadas da Library.

#include <QTableWidget>

class DashboardTable : public QTableWidget {
    Q_OBJECT
public:
    // Dados de cada célula (além do texto = nome da media).
    static constexpr int kThumbRole = Qt::UserRole + 1;    // QImage: miniatura
    static constexpr int kActiveRole = Qt::UserRole + 2;   // bool: coluna no ar
    static constexpr int kProgressRole = Qt::UserRole + 3; // double 0..1 (-1 = sem barra)

    explicit DashboardTable(QWidget* parent = nullptr);
    void setEditable(bool editable) { editable_ = editable; }

signals:
    void mediaDropped(int row, int column, const QString& mediaId);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    bool editable_ = true;
};
