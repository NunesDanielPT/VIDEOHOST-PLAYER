#pragma once
// Workspace CUEPLAYER (como no Resolume, misturado com o MEDIAPLAYER):
//
//   ┌ composição: [Comp 1 ▾] [+] [⋯]  opacidade  volume   [Limpar tudo] [Outputs…] ┐
//   │  PREVIEW (clip selecionado)  │  PROGRAM (composição no ar)  │  Propriedades   │
//   │                              │                              │  Comp/Layer/Clip│
//   ├──────────────────────────────┴──────────────────────────────┤                 │
//   │  Grelha: layers × colunas                                   │                 │
//   └─────────────────────────────────────────────────────────────┴─────────────────┘
//
// Edita directamente o projecto da janela principal; cada alteração emite
// changed() (desfazer + envio ao motor). Opacidade, blend, transform e som de uma
// layer vão também logo ao motor por set_comp_layer (sem recarregar o projecto).

#include "vthost/core/project.h"
#include "vthost/core/protocol.h"

#include <QWidget>

class CompGrid;
class EngineClient;
class MediaCache;
class PreviewWidget;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QToolButton;

class CuePlayerPage : public QWidget {
    Q_OBJECT
public:
    CuePlayerPage(vthost::Project* project, EngineClient* engine, MediaCache* media, QWidget* parent = nullptr);

    void refresh();                        // projecto substituído (abrir, desfazer...)
    void onStatus(const vthost::protocol::EngineStatus& status);
    void setEditable(bool editable);
    void setActive(bool active);           // workspace visível: Program mostra a composição
    QString currentComposition() const { return compId_; }
    void launchSelected();                 // ESPAÇO no CUEPLAYER: lança o clip selecionado

signals:
    void changed();
    void openOutputs();
    void message(const QString& text);

private:
    vthost::Composition* comp();
    vthost::CompLayer* layer(const QString& id);
    vthost::Clip* clip(const QString& layerId, const QString& columnId);

    void selectComposition(const QString& id);
    void rebuildCompCombo();
    void addComposition();
    void compositionMenu();
    void addLayer(int aboveIndex);
    void deleteLayer(const QString& id);
    void addColumn(int atIndex);
    void deleteColumn(const QString& id);
    void renameLayer(const QString& id);
    void renameColumn(const QString& id);

    void launchClip(const QString& layer, const QString& column);
    void launchColumn(const QString& column);
    void clearLayer(const QString& layer);
    void clearAll();
    void previewSelection();
    void onSelection(const QString& layer, const QString& column);
    void onFilesDropped(const QString& layer, const QString& column, const QStringList& files);
    void onClipMoved(const QString& fromL, const QString& fromC, const QString& toL, const QString& toC, bool copy);
    void layerToggle(const QString& layer, QChar which);
    void showLayerMenu(const QString& layer, const QPoint& pos);
    void showColumnMenu(const QString& column, const QPoint& pos);
    void showCellMenu(const QString& layer, const QString& column, const QPoint& pos);
    void replaceClipFile(const QString& layer, const QString& column);

    QWidget* buildCompForm();
    QWidget* buildLayerForm();
    QWidget* buildClipForm();
    void loadForms();
    void applyCompForm();
    void applyLayerForm();
    void applyClipForm();
    void sendLayerLive(const vthost::CompLayer& l);
    void sendCompLive();
    void edited();                          // changed() + redesenhar
    void updateMonitors();

    vthost::Project* project_;
    EngineClient* engine_;
    MediaCache* media_;
    QString compId_;
    QString selLayer_;
    QString selColumn_;
    bool editable_ = true;
    bool active_ = false;
    bool loading_ = false;

    QComboBox* compCombo_ = nullptr;
    QSlider* compOpacity_ = nullptr;
    QLabel* compOpacityLabel_ = nullptr;
    QList<QWidget*> editWidgets_;
    PreviewWidget* previewMon_ = nullptr;
    PreviewWidget* programMon_ = nullptr;
    QLabel* previewTitle_ = nullptr;
    QLabel* programTitle_ = nullptr;
    CompGrid* grid_ = nullptr;
    QTabWidget* tabs_ = nullptr;

    // Composição
    QLineEdit* cName_ = nullptr;
    QSpinBox* cWidth_ = nullptr;
    QSpinBox* cHeight_ = nullptr;
    QDoubleSpinBox* cVolume_ = nullptr;
    // Layer
    QWidget* layerForm_ = nullptr;
    QLabel* lEmpty_ = nullptr;
    QLineEdit* lName_ = nullptr;
    QSlider* lOpacity_ = nullptr;
    QLabel* lOpacityLabel_ = nullptr;
    QComboBox* lBlend_ = nullptr;
    QDoubleSpinBox* lTransition_ = nullptr;
    QDoubleSpinBox* lVolume_ = nullptr;
    QComboBox* lFit_ = nullptr;
    QDoubleSpinBox* lPosX_ = nullptr;
    QDoubleSpinBox* lPosY_ = nullptr;
    QDoubleSpinBox* lScale_ = nullptr;
    QDoubleSpinBox* lRotation_ = nullptr;
    QCheckBox* lFlipH_ = nullptr;
    QCheckBox* lFlipV_ = nullptr;
    QCheckBox* lVideo_ = nullptr;
    QCheckBox* lAudio_ = nullptr;
    QCheckBox* lSolo_ = nullptr;
    QCheckBox* lBypass_ = nullptr;
    // Clip
    QWidget* clipForm_ = nullptr;
    QLabel* kEmpty_ = nullptr;
    QLineEdit* kName_ = nullptr;
    QLabel* kFile_ = nullptr;
    QLabel* kInfo_ = nullptr;
    QDoubleSpinBox* kIn_ = nullptr;
    QDoubleSpinBox* kOut_ = nullptr;
    QDoubleSpinBox* kSpeed_ = nullptr;
    QComboBox* kEnd_ = nullptr;
    QDoubleSpinBox* kVolume_ = nullptr;
    QCheckBox* kNormalize_ = nullptr;
    QLabel* kLoudness_ = nullptr;
};
