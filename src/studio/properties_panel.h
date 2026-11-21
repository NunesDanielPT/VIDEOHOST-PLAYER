#pragma once
// Properties: propriedades da layer selecionada, aplicadas ao vivo.

#include "vthost/core/project.h"

#include <QWidget>

#include <utility>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

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
    QWidget* makeSection(const QString& title, QWidget* content, bool expanded);

    QLabel* title_;
    QComboBox* blend_;
    QComboBox* canvas_;
    QDoubleSpinBox* opacity_;
    QDoubleSpinBox* posX_;
    QDoubleSpinBox* posY_;
    QDoubleSpinBox* scale_;
    QDoubleSpinBox* rotation_;
    QPushButton* reset_;
    QWidget* body_;
    bool editable_ = true;
    bool hasLayer_ = false;
    bool locked_ = false;
    double originX_ = 0.0;
    double originY_ = 0.0;
};
