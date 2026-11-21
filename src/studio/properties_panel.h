#pragma once
// Properties: propriedades da layer selecionada, aplicadas ao vivo.
// Secções (como no Millumin): Layer, Mapping (corner pin), Efeitos, Máscaras.

#include "vthost/core/project.h"

#include <QWidget>

#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    // Sem layer (nullptr): painel desativado.
    void setLayer(const vthost::Layer* layer);
    void setEditable(bool editable);
    // Origem do Workspace: (0,0) no centro (omissão) ou no canto superior esquerdo.
    // Só muda os números mostrados; o projeto guarda sempre relativo ao centro.
    void setOrigin(double offsetX, double offsetY);
    // Canvas disponíveis (id, nome) para escolher o canvas da layer.
    void setCanvases(const std::vector<std::pair<QString, QString>>& canvases);

signals:
    void propsChanged(const vthost::LayerProps& props);
    void canvasChanged(const QString& canvasId);

private:
    vthost::LayerProps current() const;
    QWidget* makeSection(const QString& key, const QString& title, QWidget* content, bool expandedByDefault);
    void emitChange();

    QLabel* title_;
    QWidget* body_;
    // Layer
    QComboBox* canvas_;
    QComboBox* blend_;
    QComboBox* fit_;
    QDoubleSpinBox* opacity_;
    QDoubleSpinBox* posX_;
    QDoubleSpinBox* posY_;
    QDoubleSpinBox* scale_;
    QDoubleSpinBox* rotation_;
    QCheckBox* flipH_;
    QCheckBox* flipV_;
    // Mapping
    QDoubleSpinBox* pin_[8];
    // Efeitos
    QDoubleSpinBox* brightness_;
    QDoubleSpinBox* contrast_;
    QDoubleSpinBox* saturation_;
    QDoubleSpinBox* gamma_;
    QDoubleSpinBox* hue_;
    QCheckBox* invert_;
    // Máscaras
    QDoubleSpinBox* crop_[4];
    QDoubleSpinBox* feather_;

    bool editable_ = true;
    bool hasLayer_ = false;
    bool locked_ = false;
    bool loading_ = false;
    double originX_ = 0.0;
    double originY_ = 0.0;
    vthost::LayerProps last_;
};
