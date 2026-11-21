#pragma once
// Properties: propriedades da layer selecionada, aplicadas ao vivo.

#include "vthost/core/project.h"

#include <QWidget>

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

signals:
    void propsChanged(const vthost::LayerProps& props);

private:
    vthost::LayerProps current() const;
    QWidget* makeSection(const QString& title, QWidget* content, bool expanded);

    QLabel* title_;
    QComboBox* blend_;
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
};
