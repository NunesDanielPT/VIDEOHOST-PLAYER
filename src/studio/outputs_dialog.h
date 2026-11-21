#pragma once
// Painel de Saídas e Canvas (como o painel Output do Millumin, Ctrl+;):
//  - Canvas: superfícies com resolução à medida; assistente de layout para
//    repartir um canvas grande por várias saídas com sobreposição (soft-edge).
//  - Saídas: tipo (ecrã, janela, NDI, Blackmagic), destino, canvas e região,
//    soft-edge, brilho e editor de mapping (warp).
// As alterações são aplicadas ao vivo (liveChanged); Cancelar repõe o original.

#include "engine_client.h"
#include "vthost/core/project.h"
#include "vthost/core/protocol.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;

class OutputsDialog : public QDialog {
    Q_OBJECT
public:
    OutputsDialog(const std::vector<vthost::Canvas>& canvases, const std::vector<vthost::Output>& outputs,
                  EngineClient* engine, QWidget* parent = nullptr);

    std::vector<vthost::Canvas> canvases() const { return canvases_; }
    std::vector<vthost::Output> outputs() const { return outputs_; }

signals:
    void liveChanged(); // aplicar já ao motor (o MainWindow lê canvases()/outputs())

private:
    void refreshDisplays();
    void setDisplays(std::vector<vthost::protocol::DisplayInfo> displays, bool fromEngine);
    void rebuildLists();
    void showOutput(int index);
    void showCanvas(int index);
    void commitForm();
    void commitCanvas();
    void changed();
    void addOutput(vthost::OutputType type);
    void removeOutput();
    void addCanvas();
    void removeCanvas();
    void layoutWizard();
    void editMapping();
    void identify();
    void updateStatus();
    void setTestCard(bool on);
    QString displayLabel(const vthost::protocol::DisplayInfo& d) const;
    void outputPixelSize(const vthost::Output& o, int& w, int& h) const;

    EngineClient* engine_;
    std::vector<vthost::Canvas> canvases_;
    std::vector<vthost::Output> outputs_;
    std::vector<vthost::protocol::DisplayInfo> displays_;
    int current_ = -1;
    int currentCanvas_ = -1;
    bool loading_ = false;

    QListWidget* canvasList_;
    QListWidget* list_;
    QStackedWidget* formStack_;

    // Canvas
    QLineEdit* cName_;
    QComboBox* cPreset_;
    QSpinBox* cW_;
    QSpinBox* cH_;

    // Saída
    QLineEdit* name_;
    QComboBox* type_;
    QCheckBox* enabled_;
    QComboBox* canvas_;
    QStackedWidget* pages_;
    QComboBox* display_;
    QLabel* displayWarning_;
    QSpinBox* winW_;
    QSpinBox* winH_;
    QLineEdit* ndiName_;
    QCheckBox* ndiAlpha_;
    QSpinBox* srcX_;
    QSpinBox* srcY_;
    QSpinBox* srcW_;
    QSpinBox* srcH_;
    QSpinBox* blendL_;
    QSpinBox* blendR_;
    QSpinBox* blendT_;
    QSpinBox* blendB_;
    QDoubleSpinBox* smooth_;
    QDoubleSpinBox* gamma_;
    QDoubleSpinBox* bright_;
    QLabel* warpInfo_;
    QLabel* status_;
    QLabel* source_;
    QCheckBox* testCard_;
};
