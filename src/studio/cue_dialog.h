#pragma once
// Definições de uma cue (coluna do Dashboard): nome, transição, avanço
// automático e "depois desta, ir para".

#include "vthost/core/project.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;

class CueDialog : public QDialog {
    Q_OBJECT
public:
    CueDialog(const vthost::Project& project, int column, QWidget* parent = nullptr);

    // Aplica os valores escolhidos à coluna (nome, transição, follow, salto).
    void applyTo(vthost::Column& column) const;

private:
    void updateEnabled();

    QLineEdit* name_ = nullptr;
    QComboBox* transition_ = nullptr;
    QDoubleSpinBox* duration_ = nullptr;
    QComboBox* follow_ = nullptr;
    QDoubleSpinBox* followSeconds_ = nullptr;
    QComboBox* jumpTo_ = nullptr;
};
