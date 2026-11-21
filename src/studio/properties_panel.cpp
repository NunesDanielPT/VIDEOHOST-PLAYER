#include "properties_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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

QPushButton* resetButton(const QString& text, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return b;
}
} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    title_ = new QLabel(this);
    title_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    outer->addWidget(title_);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    body_ = new QWidget(scroll);
    auto* bodyLayout = new QVBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);

    // ---------------- Layer
    auto* layerBox = new QWidget(body_);
    auto* form = new QFormLayout(layerBox);
    form->setLabelAlignment(Qt::AlignRight);
    canvas_ = new QComboBox(layerBox);
    blend_ = new QComboBox(layerBox);
    blend_->addItem(tr("normal"), static_cast<int>(BlendMode::Normal));
    blend_->addItem(tr("add"), static_cast<int>(BlendMode::Add));
    blend_->addItem(tr("multiply"), static_cast<int>(BlendMode::Multiply));
    blend_->addItem(tr("screen"), static_cast<int>(BlendMode::Screen));
    fit_ = new QComboBox(layerBox);
    fit_->addItem(tr("fit to canvas"), static_cast<int>(FitMode::Fit));
    fit_->addItem(tr("fill canvas (crop)"), static_cast<int>(FitMode::Fill));
    fit_->addItem(tr("stretch to canvas"), static_cast<int>(FitMode::Stretch));
    fit_->addItem(tr("native size"), static_cast<int>(FitMode::Native));
    opacity_ = spin(layerBox, 0.0, 100.0, 2, QStringLiteral(" %"), 1.0);
    posX_ = spin(layerBox, -100000.0, 100000.0, 2, QStringLiteral(" px"), 1.0);
    posY_ = spin(layerBox, -100000.0, 100000.0, 2, QStringLiteral(" px"), 1.0);
    scale_ = spin(layerBox, 1.0, 1000.0, 2, QStringLiteral(" %"), 1.0);
    rotation_ = spin(layerBox, -3600.0, 3600.0, 2, QStringLiteral(" °"), 1.0);
    flipH_ = new QCheckBox(tr("horizontal"), layerBox);
    flipV_ = new QCheckBox(tr("vertical"), layerBox);
    auto* pos = new QHBoxLayout;
    pos->addWidget(posX_);
    pos->addWidget(posY_);
    auto* flips = new QHBoxLayout;
    flips->addWidget(flipH_);
    flips->addWidget(flipV_);
    flips->addStretch();
    auto* resetLayer = resetButton(tr("Reset"), layerBox);
    form->addRow(tr("canvas"), canvas_);
    form->addRow(tr("blend mode"), blend_);
    form->addRow(tr("opacity"), opacity_);
    form->addRow(tr("constraint"), fit_);
    form->addRow(tr("position"), pos);
    form->addRow(tr("scale"), scale_);
    form->addRow(tr("rotation"), rotation_);
    form->addRow(tr("flip"), flips);
    form->addRow(QString(), resetLayer);
    bodyLayout->addWidget(makeSection(QStringLiteral("layer"), tr("Layer"), layerBox, true));

    // ---------------- Mapping (corner pin)
    auto* mapBox = new QWidget(body_);
    auto* mg = new QGridLayout(mapBox);
    const QString names[4] = {tr("top left"), tr("top right"), tr("bottom right"), tr("bottom left")};
    for (int i = 0; i < 4; ++i) {
        pin_[i * 2] = spin(mapBox, -100000.0, 100000.0, 1, QStringLiteral(" px"), 1.0);
        pin_[i * 2 + 1] = spin(mapBox, -100000.0, 100000.0, 1, QStringLiteral(" px"), 1.0);
        mg->addWidget(new QLabel(names[i], mapBox), i, 0);
        mg->addWidget(pin_[i * 2], i, 1);
        mg->addWidget(pin_[i * 2 + 1], i, 2);
    }
    auto* pinNote = new QLabel(tr("Drag the orange corners of the layer in the Workspace to pin it onto a surface."), mapBox);
    pinNote->setWordWrap(true);
    pinNote->setStyleSheet(QStringLiteral("color: gray;"));
    auto* resetPin = resetButton(tr("Reset corners"), mapBox);
    mg->addWidget(pinNote, 4, 0, 1, 3);
    mg->addWidget(resetPin, 5, 0, 1, 3, Qt::AlignLeft);
    bodyLayout->addWidget(makeSection(QStringLiteral("mapping"), tr("Mapping"), mapBox, false));

    // ---------------- Efeitos
    auto* fxBox = new QWidget(body_);
    auto* ff = new QFormLayout(fxBox);
    ff->setLabelAlignment(Qt::AlignRight);
    brightness_ = spin(fxBox, -100.0, 100.0, 0, QStringLiteral(" %"), 1.0);
    contrast_ = spin(fxBox, 0.0, 300.0, 0, QStringLiteral(" %"), 1.0);
    saturation_ = spin(fxBox, 0.0, 300.0, 0, QStringLiteral(" %"), 1.0);
    gamma_ = spin(fxBox, 0.2, 5.0, 2, QString(), 0.05);
    hue_ = spin(fxBox, -180.0, 180.0, 0, QStringLiteral(" °"), 1.0);
    invert_ = new QCheckBox(tr("invert colors"), fxBox);
    auto* resetFx = resetButton(tr("Reset effects"), fxBox);
    ff->addRow(tr("brightness"), brightness_);
    ff->addRow(tr("contrast"), contrast_);
    ff->addRow(tr("saturation"), saturation_);
    ff->addRow(tr("gamma"), gamma_);
    ff->addRow(tr("hue"), hue_);
    ff->addRow(QString(), invert_);
    ff->addRow(QString(), resetFx);
    bodyLayout->addWidget(makeSection(QStringLiteral("effects"), tr("Effects"), fxBox, false));

    // ---------------- Máscaras
    auto* maskBox = new QWidget(body_);
    auto* mf = new QFormLayout(maskBox);
    mf->setLabelAlignment(Qt::AlignRight);
    const QString sides[4] = {tr("crop left"), tr("crop right"), tr("crop top"), tr("crop bottom")};
    for (int i = 0; i < 4; ++i) {
        crop_[i] = spin(maskBox, 0.0, 100.0, 1, QStringLiteral(" %"), 1.0);
        mf->addRow(sides[i], crop_[i]);
    }
    feather_ = spin(maskBox, 0.0, 50.0, 1, QStringLiteral(" %"), 0.5);
    mf->addRow(tr("feather"), feather_);
    auto* resetMask = resetButton(tr("Reset mask"), maskBox);
    mf->addRow(QString(), resetMask);
    bodyLayout->addWidget(makeSection(QStringLiteral("masks"), tr("Masks"), maskBox, false));

    bodyLayout->addStretch();
    scroll->setWidget(body_);
    outer->addWidget(scroll, 1);

    // ---------------- ligações
    auto changed = [this] { emitChange(); };
    for (QComboBox* c : {blend_, fit_}) connect(c, &QComboBox::currentIndexChanged, this, changed);
    for (QDoubleSpinBox* s : {opacity_, posX_, posY_, scale_, rotation_, brightness_, contrast_, saturation_, gamma_,
                              hue_, feather_})
        connect(s, &QDoubleSpinBox::valueChanged, this, changed);
    for (QDoubleSpinBox* s : pin_) connect(s, &QDoubleSpinBox::valueChanged, this, changed);
    for (QDoubleSpinBox* s : crop_) connect(s, &QDoubleSpinBox::valueChanged, this, changed);
    for (QCheckBox* c : {flipH_, flipV_, invert_}) connect(c, &QCheckBox::toggled, this, changed);
    connect(canvas_, &QComboBox::activated, this, [this](int) {
        if (hasLayer_ && editable_) emit canvasChanged(canvas_->currentData().toString());
    });
    // Cada "repor" só mexe na sua secção.
    connect(resetLayer, &QPushButton::clicked, this, [this] {
        LayerProps p = last_;
        const LayerProps d;
        p.posX = d.posX;
        p.posY = d.posY;
        p.scale = d.scale;
        p.rotation = d.rotation;
        p.fit = d.fit;
        p.flipH = p.flipV = false;
        if (hasLayer_ && editable_ && !locked_) emit propsChanged(p);
    });
    connect(resetPin, &QPushButton::clicked, this, [this] {
        LayerProps p = last_;
        p.cornerPin = {};
        if (hasLayer_ && editable_ && !locked_) emit propsChanged(p);
    });
    connect(resetFx, &QPushButton::clicked, this, [this] {
        LayerProps p = last_;
        const LayerProps d;
        p.brightness = d.brightness;
        p.contrast = d.contrast;
        p.saturation = d.saturation;
        p.gamma = d.gamma;
        p.hue = d.hue;
        p.invert = false;
        if (hasLayer_ && editable_ && !locked_) emit propsChanged(p);
    });
    connect(resetMask, &QPushButton::clicked, this, [this] {
        LayerProps p = last_;
        p.cropLeft = p.cropRight = p.cropTop = p.cropBottom = p.feather = 0.0;
        if (hasLayer_ && editable_ && !locked_) emit propsChanged(p);
    });

    setLayer(nullptr);
}

QWidget* PropertiesPanel::makeSection(const QString& key, const QString& title, QWidget* content, bool expandedByDefault) {
    auto* box = new QWidget(body_);
    auto* l = new QVBoxLayout(box);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    // O estado aberto/fechado de cada secção fica guardado.
    const QString settingsKey = QStringLiteral("ui/properties/") + key;
    const bool expanded = QSettings().value(settingsKey, expandedByDefault).toBool();
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
    connect(header, &QToolButton::toggled, content, [header, content, settingsKey](bool on) {
        content->setVisible(on);
        header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        QSettings().setValue(settingsKey, on);
    });
    l->addWidget(header);
    l->addWidget(content);
    return box;
}

void PropertiesPanel::emitChange() {
    if (loading_ || !hasLayer_ || !editable_ || locked_) return;
    last_ = current();
    emit propsChanged(last_);
}

LayerProps PropertiesPanel::current() const {
    LayerProps p = last_;
    p.blend = static_cast<BlendMode>(blend_->currentData().toInt());
    p.fit = static_cast<FitMode>(fit_->currentData().toInt());
    p.opacity = opacity_->value() / 100.0;
    p.posX = posX_->value() - originX_;
    p.posY = posY_->value() - originY_;
    p.scale = scale_->value() / 100.0;
    p.rotation = rotation_->value();
    p.flipH = flipH_->isChecked();
    p.flipV = flipV_->isChecked();
    for (std::size_t i = 0; i < 4; ++i) p.cornerPin[i] = {pin_[i * 2]->value(), pin_[i * 2 + 1]->value()};
    p.brightness = brightness_->value() / 100.0;
    p.contrast = contrast_->value() / 100.0;
    p.saturation = saturation_->value() / 100.0;
    p.gamma = gamma_->value();
    p.hue = hue_->value();
    p.invert = invert_->isChecked();
    p.cropLeft = crop_[0]->value() / 100.0;
    p.cropRight = crop_[1]->value() / 100.0;
    p.cropTop = crop_[2]->value() / 100.0;
    p.cropBottom = crop_[3]->value() / 100.0;
    p.feather = feather_->value() / 100.0;
    p.clamp();
    return p;
}

void PropertiesPanel::setLayer(const Layer* layer) {
    hasLayer_ = layer != nullptr;
    locked_ = layer && layer->locked;
    QString title = layer ? tr("Layer: %1").arg(QString::fromStdString(layer->name)) : tr("No layer selected");
    if (locked_) title += QStringLiteral("  ") + tr("(locked)");
    title_->setText(title);
    body_->setEnabled(hasLayer_ && editable_ && !locked_);
    if (!layer) return;

    loading_ = true;
    {
        const QSignalBlocker bc(canvas_);
        const int ci = canvas_->findData(QString::fromStdString(layer->canvasId));
        canvas_->setCurrentIndex(ci >= 0 ? ci : 0);
    }
    const LayerProps& p = layer->props;
    last_ = p;
    // Os campos que estão a ser editados (com o cursor) não são reescritos.
    auto set = [](QDoubleSpinBox* s, double v) {
        if (!s->hasFocus()) s->setValue(v);
    };
    blend_->setCurrentIndex(std::max(0, blend_->findData(static_cast<int>(p.blend))));
    fit_->setCurrentIndex(std::max(0, fit_->findData(static_cast<int>(p.fit))));
    set(opacity_, p.opacity * 100.0);
    set(posX_, p.posX + originX_);
    set(posY_, p.posY + originY_);
    set(scale_, p.scale * 100.0);
    set(rotation_, p.rotation);
    flipH_->setChecked(p.flipH);
    flipV_->setChecked(p.flipV);
    for (std::size_t i = 0; i < 4; ++i) {
        set(pin_[i * 2], p.cornerPin[i].x);
        set(pin_[i * 2 + 1], p.cornerPin[i].y);
    }
    set(brightness_, p.brightness * 100.0);
    set(contrast_, p.contrast * 100.0);
    set(saturation_, p.saturation * 100.0);
    set(gamma_, p.gamma);
    set(hue_, p.hue);
    invert_->setChecked(p.invert);
    set(crop_[0], p.cropLeft * 100.0);
    set(crop_[1], p.cropRight * 100.0);
    set(crop_[2], p.cropTop * 100.0);
    set(crop_[3], p.cropBottom * 100.0);
    set(feather_, p.feather * 100.0);
    loading_ = false;
}

void PropertiesPanel::setCanvases(const std::vector<std::pair<QString, QString>>& canvases) {
    const QSignalBlocker b(canvas_);
    const QString cur = canvas_->currentData().toString();
    canvas_->clear();
    for (const auto& [id, name] : canvases) canvas_->addItem(name, id);
    const int i = canvas_->findData(cur);
    canvas_->setCurrentIndex(i >= 0 ? i : 0);
}

void PropertiesPanel::setOrigin(double offsetX, double offsetY) {
    originX_ = offsetX;
    originY_ = offsetY;
}

void PropertiesPanel::setEditable(bool editable) {
    editable_ = editable;
    body_->setEnabled(hasLayer_ && editable_ && !locked_);
}
