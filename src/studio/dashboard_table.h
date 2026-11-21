#pragma once
// Dashboard: grelha layers × colunas (cues) com miniaturas. Aceita media
// arrastadas da Library.

#include <QTableWidget>

class DashboardTable : public QTableWidget {
    Q_OBJECT
public:
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
