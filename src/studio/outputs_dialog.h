#pragma once
// Saídas (Ctrl+;): resolução de trabalho e para onde vai a imagem — ecrãs em
// ecrã inteiro (com o nome do monitor e a placa gráfica), janelas e NDI.
// As alterações são aplicadas ao vivo (liveChanged); Cancelar repõe o original.

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
    OutputsDialog(int width, int height, const std::vector<vthost::Output>& outputs, EngineClient* engine,
                  QWidget* parent = nullptr);

    int canvasWidth() const;
    int canvasHeight() const;
    std::vector<vthost::Output> outputs() const { return outputs_; }

signals:
    void liveChanged(); // aplicar já ao motor (o MainWindow lê outputs()/canvasWidth())

private:
    void refreshDisplays();
    void setDisplays(std::vector<vthost::protocol::DisplayInfo> displays, bool fromEngine);
    void rebuildList();
    void showOutput(int index);
    void commitForm();
    void changed();
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

    QComboBox* preset_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
    QListWidget* list_ = nullptr;
    QWidget* form_ = nullptr;
    QLineEdit* name_ = nullptr;
    QCheckBox* enabled_ = nullptr;
    QComboBox* type_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QComboBox* display_ = nullptr;
    QSpinBox* winW_ = nullptr;
    QSpinBox* winH_ = nullptr;
    QLineEdit* ndiName_ = nullptr;
    QCheckBox* ndiAlpha_ = nullptr;
    QCheckBox* dsm_ = nullptr;          // Output para DSM
    QLineEdit* dsmLength_ = nullptr;
    QLineEdit* dsmRemaining_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* source_ = nullptr;
};
