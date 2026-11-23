#pragma once
// Painel da direita quando um bloco está selecionado: nome, cor, o que acontece no
// fim (loop, pausa, stop, continuar), dissolve entre os vídeos e fades do bloco.

#include "vthost/core/project.h"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

class BlockInspector : public QWidget {
    Q_OBJECT
public:
    explicit BlockInspector(QWidget* parent = nullptr);
    // count/seconds: nº de vídeos e duração total (para o resumo).
    void setBlock(const vthost::Block* block, int number, int count, double seconds);
    void setEditable(bool editable);

signals:
    void changed(const vthost::Block& block);
    void playRequested(const QString& blockId);
    void toggleRequested(const QString& blockId);

private:
    void emitChange();

    vthost::Block block_;
    bool loading_ = false;
    bool editable_ = true;
    QLabel* title_ = nullptr;
    QLabel* summary_ = nullptr;
    QLineEdit* name_ = nullptr;
    QList<QToolButton*> colors_;
    QComboBox* end_ = nullptr;
    QDoubleSpinBox* dissolve_ = nullptr;
    QDoubleSpinBox* fadeIn_ = nullptr;
    QDoubleSpinBox* fadeOut_ = nullptr;
    QPushButton* play_ = nullptr;
    QPushButton* toggle_ = nullptr;
};
