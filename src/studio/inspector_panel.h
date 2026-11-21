#pragma once
// Inspector da cue selecionada (painel direito, como no Mitti): informação da
// media, geral (nome, ID, cor, notas, In/Out, velocidade, repetições, ir para),
// transições, áudio (volume, normalização) e geometria.

#include "media_cache.h"
#include "vthost/core/project.h"

#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    // cue == nullptr: nada selecionado.
    void setCue(const vthost::Cue* cue, int index, const vthost::Project* project, const MediaInfo& info,
                const AudioAnalysis& audio, double normalizeTarget);
    // Posição actual no ficheiro, se esta cue estiver no ar (< 0 = não está).
    void setPlayhead(double fileTime);
    void setEditable(bool editable);

signals:
    void cueChanged(const vthost::Cue& cue);
    void reanalyzeRequested(const QString& path);

private:
    QWidget* makeSection(const QString& key, const QString& title, QWidget* content, bool expanded);
    void emitChange();
    void updateEnabled();

    vthost::Cue cue_;
    bool labelEdited_ = false;
    bool hasCue_ = false;
    bool loading_ = false;
    bool editable_ = true;
    double playhead_ = -1.0;
    double fileDuration_ = 0.0;

    QLabel* title_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
    // Media Info
    QLabel* infoFps_ = nullptr;
    QLabel* infoSize_ = nullptr;
    QLabel* infoCodecs_ = nullptr;
    QLabel* infoDuration_ = nullptr;
    QLabel* infoDepth_ = nullptr;
    QLabel* infoPath_ = nullptr;
    // Geral
    QLineEdit* name_ = nullptr;
    QLineEdit* label_ = nullptr;
    QList<QToolButton*> colors_;
    QPlainTextEdit* notes_ = nullptr;
    QTimer notesTimer_;
    QDoubleSpinBox* in_ = nullptr;
    QDoubleSpinBox* out_ = nullptr;
    QPushButton* inFromHead_ = nullptr;
    QPushButton* outFromHead_ = nullptr;
    QDoubleSpinBox* speed_ = nullptr;
    QSpinBox* playCount_ = nullptr;
    QComboBox* goto_ = nullptr;
    // Transições
    QDoubleSpinBox* fadeIn_ = nullptr;
    QDoubleSpinBox* fadeOut_ = nullptr;
    QDoubleSpinBox* crossfade_ = nullptr;
    QCheckBox* pauseStart_ = nullptr;
    QCheckBox* pauseEnd_ = nullptr;
    QCheckBox* clearEnd_ = nullptr;
    // Áudio
    QCheckBox* mute_ = nullptr;
    QDoubleSpinBox* volume_ = nullptr;
    QCheckBox* normalize_ = nullptr;
    QLabel* loudness_ = nullptr;
    QPushButton* reanalyze_ = nullptr;
    // Geometria
    QComboBox* fit_ = nullptr;
    QDoubleSpinBox* scale_ = nullptr;
    QDoubleSpinBox* posX_ = nullptr;
    QDoubleSpinBox* posY_ = nullptr;
};
