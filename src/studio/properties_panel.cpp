#include "properties_panel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

using namespace vthost;

namespace {
QDoubleSpinBox* spin(QWidget* parent, double min, double max, int decimals, const QString& suffix, double step) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    s->setSingleStep(step);
    s->setKeyboardTracking(false); // aplica ao confirmar, não a cada tecla
    s->setAccelerated(true);
    return s;
}
} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    title_ = new QLabel(this);
    title_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    outer->addWidget(title_);

    body_ = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);

    auto* layerBox = new QWidget(body_);
    auto* form = new QFormLayout(layerBox);
    form->setLabelAlignment(Qt::AlignRight);
    blend_ = new QComboBox(layerBox);
    blend_->addItem(tr("normal"), static_cast<int>(BlendMode::Normal));
    blend_->addItem(tr("add"), static_cast<int>(BlendMode::Add));
    blend_->addItem(tr("multiply"), static_cast<int>(BlendMode::Multiply));
    blend_->addItem(tr("screen"), static_cast<int>(BlendMode::Screen));
    opacity_ = spin(layerBox, 0.0, 100.0, 2, QStringLiteral(" %"), 1.0);
    posX_ = spin(layerBox, -100000.0, 100000.0, 2, QStringLiteral(" px"), 1.0);
    posY_ = spin(layerBox, -100000.0, 100000.0, 2, QStringLiteral(" px"), 1.0);
    scale_ = spin(layerBox, 1.0, 1000.0, 2, QStringLiteral(" %"), 1.0);
    rotation_ = spin(layerBox, -3600.0, 3600.0, 2, QStringLiteral(" °"), 1.0);
    auto* pos = new QHBoxLayout;
    pos->addWidget(posX_);
    pos->addWidget(posY_);
    form->addRow(tr("blend mode"), blend_);
    form->addRow(tr("opacity"), opacity_);
    form->addRow(tr("position"), pos);
    form->addRow(tr("scale"), scale_);
    form->addRow(tr("rotation"), rotation_);
    reset_ = new QPushButton(tr("Reset"), layerBox);
    form->addRow(QString(), reset_);
    bodyLayout->addWidget(makeSection(tr("Layer"), layerBox, true));

    // Secções futuras (como no Millumin): visíveis para se perceber o caminho.
    for (const QString& name : {tr("Mapping"), tr("Effects"), tr("Masks")}) {
        auto* soon = new QLabel(tr("Coming in a future version."), body_);
        soon->setStyleSheet(QStringLiteral("color: gray; padding: 6px;"));
        bodyLayout->addWidget(makeSection(name, soon, false));
    }
    bodyLayout->addStretch();
    outer->addWidget(body_, 1);

    auto emitChange = [this] {
        if (hasLayer_ && editable_) emit propsChanged(current());
    };
    connect(blend_, &QComboBox::currentIndexChanged, this, emitChange);
    for (QDoubleSpinBox* s : {opacity_, posX_, posY_, scale_, rotation_})
        connect(s, &QDoubleSpinBox::valueChanged, this, emitChange);
    connect(reset_, &QPushButton::clicked, this, [this] {
        LayerProps def;
        def.opacity = opacity_->value() / 100.0; // o reset não mexe na opacidade nem no blend
        def.blend = static_cast<BlendMode>(blend_->currentData().toInt());
        if (hasLayer_ && editable_) emit propsChanged(def);
    });

    setLayer(nullptr);
}

QWidget* PropertiesPanel::makeSection(const QString& title, QWidget* content, bool expanded) {
    auto* box = new QWidget(body_);
    auto* l = new QVBoxLayout(box);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    auto* header = new QToolButton(box);
    header->setText(title);
    header->setCheckable(true);
    header->setChecked(expanded);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    header->setAutoRaise(true);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header->setStyleSheet(QStringLiteral("QToolButton { text-align: left; font-weight: bold; padding: 4px; }"));
    content->setVisible(expanded);
    connect(header, &QToolButton::toggled, content, [header, content](bool on) {
        content->setVisible(on);
        header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    l->addWidget(header);
    l->addWidget(content);
    return box;
}

LayerProps PropertiesPanel::current() const {
    LayerProps p;
    p.blend = static_cast<BlendMode>(blend_->currentData().toInt());
    p.opacity = opacity_->value() / 100.0;
    p.posX = posX_->value();
    p.posY = posY_->value();
    p.scale = scale_->value() / 100.0;
    p.rotation = rotation_->value();
    p.clamp();
    return p;
}

void PropertiesPanel::setLayer(const Layer* layer) {
    hasLayer_ = layer != nullptr;
    title_->setText(layer ? tr("Layer: %1").arg(QString::fromStdString(layer->name)) : tr("No layer selected"));
    body_->setEnabled(hasLayer_ && editable_);
    if (!layer) return;
    const LayerProps& p = layer->props;
    const QSignalBlocker b1(blend_), b2(opacity_), b3(posX_), b4(posY_), b5(scale_), b6(rotation_);
    blend_->setCurrentIndex(qMax(0, blend_->findData(static_cast<int>(p.blend))));
    opacity_->setValue(p.opacity * 100.0);
    posX_->setValue(p.posX);
    posY_->setValue(p.posY);
    scale_->setValue(p.scale * 100.0);
    rotation_->setValue(p.rotation);
}

void PropertiesPanel::setEditable(bool editable) {
    editable_ = editable;
    body_->setEnabled(hasLayer_ && editable_);
}
