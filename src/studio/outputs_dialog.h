#pragma once
// Painel de Saídas (como o painel Outputs do Millumin): lista das saídas do
// projeto e, para cada uma, o tipo (ecrã, janela, NDI, Blackmagic) e o
// destino — os ecrãs aparecem com o nome do monitor/projetor, a resolução,
// a frequência e a placa gráfica onde estão ligados.

#include "engine_client.h"
#include "vthost/core/project.h"
#include "vthost/core/protocol.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;

class OutputsDialog : public QDialog {
    Q_OBJECT
public:
    OutputsDialog(const std::vector<vthost::Output>& outputs, EngineClient* engine, QWidget* parent = nullptr);

    std::vector<vthost::Output> outputs() const { return outputs_; }

private:
    void refreshDisplays();
    void setDisplays(std::vector<vthost::protocol::DisplayInfo> displays, bool fromEngine);
    void rebuildList();
    void showOutput(int index);
    void commitForm();
    void addOutput(vthost::OutputType type);
    void removeOutput();
    void identify();
    void updateStatus();
    QString displayLabel(const vthost::protocol::DisplayInfo& d) const;

    EngineClient* engine_;
    std::vector<vthost::Output> outputs_;
    std::vector<vthost::protocol::DisplayInfo> displays_;
    int current_ = -1;
    bool loading_ = false;

    QListWidget* list_;
    QLineEdit* name_;
    QComboBox* type_;
    QCheckBox* enabled_;
    QStackedWidget* pages_;
    QComboBox* display_;
    QLabel* displayWarning_;
    QSpinBox* winW_;
    QSpinBox* winH_;
    QLineEdit* ndiName_;
    QCheckBox* ndiAlpha_;
    QLabel* status_;
    QLabel* source_;
};
