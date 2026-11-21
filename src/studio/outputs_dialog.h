#pragma once
// Output avançado (Ctrl+;), como no Resolume: Screens > Slices.
//   - Screen: para onde vai a imagem (ecrã inteiro com o nome do monitor e a placa,
//     janela ou NDI) e a sua resolução.
//   - Slice: um pedaço do screen que mostra uma fonte (MEDIAPLAYER, uma composição
//     do CUEPLAYER ou uma só layer). Input Selection escolhe o recorte da fonte;
//     Output Transformation põe-no no screen (posição, tamanho, rotação, cantos,
//     pontos de edição), com cor e soft edge.
// Tudo é aplicado ao vivo (liveChanged / set_output); Cancelar repõe o original.

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
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QTreeWidget;
class SliceCanvas;

class OutputsDialog : public QDialog {
    Q_OBJECT
public:
    OutputsDialog(int width, int height, const std::vector<vthost::Output>& outputs,
                  const std::vector<vthost::Composition>& compositions, EngineClient* engine, QWidget* parent = nullptr);

    int canvasWidth() const;
    int canvasHeight() const;
    std::vector<vthost::Output> outputs() const { return outputs_; }

signals:
    void liveChanged(); // aplicar já ao motor (o MainWindow lê outputs()/canvasWidth())

private:
    QWidget* buildScreenForm();
    QWidget* buildSliceEditor();
    void refreshDisplays();
    void setDisplays(std::vector<vthost::protocol::DisplayInfo> displays, bool fromEngine);
    void rebuildTree();
    void select(int screen, int slice);
    void showCurrent();
    void loadScreenForm();
    void loadSliceForm();
    void commitScreenForm();
    void commitSliceForm();
    void changed();          // alteração terminada: projecto + motor
    void sendLive();         // durante um arrasto: só o screen actual, ao motor
    void addOutput(vthost::OutputType type);
    void addSlice();
    void duplicateCurrent();
    void removeCurrent();
    void identify();
    void updateStatus();
    QString displayLabel(const vthost::protocol::DisplayInfo& d) const;
    QSize displaySize(const vthost::Output& o) const;
    QSize sourceSize(const vthost::Slice& s) const;
    QString sourceName(const vthost::Slice& s) const;
    vthost::Output* currentOutput();
    vthost::Slice* currentSliceRef();

    EngineClient* engine_;
    std::vector<vthost::Output> outputs_;
    std::vector<vthost::Composition> comps_;
    std::vector<vthost::protocol::DisplayInfo> displays_;
    int current_ = -1;      // screen
    int currentSlice_ = -1; // -1 = o screen está selecionado
    bool loading_ = false;

    QComboBox* preset_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QStackedWidget* right_ = nullptr;
    QLabel* source_ = nullptr;

    // Screen
    QLineEdit* name_ = nullptr;
    QCheckBox* enabled_ = nullptr;
    QComboBox* type_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QComboBox* display_ = nullptr;
    QSpinBox* winW_ = nullptr;
    QSpinBox* winH_ = nullptr;
    QLineEdit* ndiName_ = nullptr;
    QCheckBox* ndiAlpha_ = nullptr;
    QSpinBox* scrW_ = nullptr;
    QSpinBox* scrH_ = nullptr;
    QLabel* status_ = nullptr;
    SliceCanvas* screenCanvas_ = nullptr;

    // Slice
    QTabWidget* sliceTabs_ = nullptr;
    SliceCanvas* inputCanvas_ = nullptr;
    SliceCanvas* outputCanvas_ = nullptr;
    QToolButton* tools_[3] = {nullptr, nullptr, nullptr};
    QLineEdit* sName_ = nullptr;
    QCheckBox* sEnabled_ = nullptr;
    QComboBox* sSource_ = nullptr;
    QDoubleSpinBox* sInX_ = nullptr;
    QDoubleSpinBox* sInY_ = nullptr;
    QDoubleSpinBox* sInW_ = nullptr;
    QDoubleSpinBox* sInH_ = nullptr;
    QDoubleSpinBox* sX_ = nullptr;
    QDoubleSpinBox* sY_ = nullptr;
    QDoubleSpinBox* sW_ = nullptr;
    QDoubleSpinBox* sH_ = nullptr;
    QDoubleSpinBox* sRot_ = nullptr;
    QCheckBox* sFlipH_ = nullptr;
    QCheckBox* sFlipV_ = nullptr;
    QCheckBox* sAspect_ = nullptr;
    QSpinBox* sMeshCols_ = nullptr;
    QSpinBox* sMeshRows_ = nullptr;
    QDoubleSpinBox* sBright_ = nullptr;
    QDoubleSpinBox* sContrast_ = nullptr;
    QDoubleSpinBox* sRed_ = nullptr;
    QDoubleSpinBox* sGreen_ = nullptr;
    QDoubleSpinBox* sBlue_ = nullptr;
    QCheckBox* sSoft_ = nullptr;
    QDoubleSpinBox* sEdgeL_ = nullptr;
    QDoubleSpinBox* sEdgeR_ = nullptr;
    QDoubleSpinBox* sEdgeT_ = nullptr;
    QDoubleSpinBox* sEdgeB_ = nullptr;
    QDoubleSpinBox* sGamma_ = nullptr;
    QDoubleSpinBox* sPower_ = nullptr;
};
