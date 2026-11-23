#include "block_inspector.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

using namespace vthost;

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }

QColor swatch(const char* c) {
    const std::string s = c;
    if (s == "red") return {232, 76, 61};
    if (s == "yellow") return {241, 196, 15};
    if (s == "green") return {46, 204, 113};
    if (s == "purple") return {155, 89, 182};
    if (s == "pink") return {232, 67, 147};
    return {58, 58, 58};
}

QDoubleSpinBox* seconds(QWidget* parent, const QString& off) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(0.0, 60.0);
    s->setDecimals(2);
    s->setSingleStep(0.25);
    s->setSuffix(QStringLiteral(" s"));
    s->setSpecialValueText(off);
    s->setKeyboardTracking(false);
    return s;
}

QString hms(double s) {
    const long long t = static_cast<long long>(std::llround(std::max(0.0, s)));
    return QStringLiteral("%1:%2:%3")
        .arg(t / 3600, 2, 10, QLatin1Char('0'))
        .arg((t / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(t % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

BlockInspector::BlockInspector(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 8, 8);
    title_ = new QLabel(this);
    title_->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 14px;"));
    v->addWidget(title_);
    summary_ = new QLabel(this);
    summary_->setStyleSheet(QStringLiteral("color: #aaa;"));
    v->addWidget(summary_);

    auto* buttons = new QHBoxLayout;
    play_ = new QPushButton(tr("Play the block"), this);
    play_->setToolTip(tr("Plays the first video of the block (also Enter or Space with the block selected)"));
    toggle_ = new QPushButton(this);
    connect(play_, &QPushButton::clicked, this, [this] { emit playRequested(qs(block_.id)); });
    connect(toggle_, &QPushButton::clicked, this, [this] { emit toggleRequested(qs(block_.id)); });
    buttons->addWidget(play_);
    buttons->addWidget(toggle_);
    v->addLayout(buttons);

    auto* general = new QGroupBox(tr("Block"), this);
    auto* g = new QFormLayout(general);
    name_ = new QLineEdit(general);
    connect(name_, &QLineEdit::editingFinished, this, &BlockInspector::emitChange);
    g->addRow(tr("Name:"), name_);
    auto* colorRow = new QHBoxLayout;
    const char* names[] = {"", "red", "yellow", "green", "purple", "pink"};
    for (const char* c : names) {
        auto* b = new QToolButton(general);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFixedSize(20, 20);
        b->setText(*c ? QString() : QStringLiteral("×"));
        b->setToolTip(*c ? QString::fromLatin1(c) : tr("No color"));
        b->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid #222; border-radius: 3px; }"
                                        "QToolButton:checked { border: 2px solid white; }")
                             .arg(swatch(c).name()));
        b->setProperty("color", QString::fromLatin1(c));
        connect(b, &QToolButton::clicked, this, &BlockInspector::emitChange);
        colors_ << b;
        colorRow->addWidget(b);
    }
    colorRow->addStretch();
    g->addRow(tr("Color:"), colorRow);
    v->addWidget(general);

    auto* play = new QGroupBox(tr("Playback"), this);
    auto* p = new QFormLayout(play);
    end_ = new QComboBox(play);
    end_->addItem(tr("Loop the block (back to the first video)"), QStringLiteral("loop"));
    end_->addItem(tr("Pause on the last frame (Space continues)"), QStringLiteral("pause"));
    end_->addItem(tr("Stop (black)"), QStringLiteral("stop"));
    end_->addItem(tr("Continue to the next video"), QStringLiteral("continue"));
    connect(end_, &QComboBox::activated, this, &BlockInspector::emitChange);
    p->addRow(tr("At the end:"), end_);
    dissolve_ = seconds(play, tr("cut"));
    dissolve_->setToolTip(tr("Dissolve from each video of the block into the next (and from the last to the first in loop)"));
    p->addRow(tr("Dissolve between videos:"), dissolve_);
    fadeIn_ = seconds(play, tr("off"));
    fadeIn_->setToolTip(tr("Fade in from black when the block starts"));
    p->addRow(tr("Fade in at the start:"), fadeIn_);
    fadeOut_ = seconds(play, tr("off"));
    fadeOut_->setToolTip(tr("Fade out to black at the end of the last video (not in loop)"));
    p->addRow(tr("Fade out at the end:"), fadeOut_);
    for (auto* s : {dissolve_, fadeIn_, fadeOut_}) connect(s, &QDoubleSpinBox::valueChanged, this, &BlockInspector::emitChange);
    auto* note = new QLabel(tr("The timers count the whole block while one of its videos is on air. Each video keeps "
                               "its own options; the block adds these on top."),
                            play);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray;"));
    p->addRow(note);
    v->addWidget(play);
    v->addStretch();
}

void BlockInspector::setBlock(const Block* b, int number, int count, double total) {
    loading_ = true;
    if (!b) {
        block_ = Block{};
        title_->setText(tr("No block selected"));
        summary_->clear();
        loading_ = false;
        return;
    }
    block_ = *b;
    title_->setText(QStringLiteral("#%1  ").arg(number) + (b->name.empty() ? tr("BLOCK") : qs(b->name)));
    summary_->setText(tr("%1 video(s) · %2").arg(count).arg(hms(total)));
    if (!name_->hasFocus()) name_->setText(qs(b->name));
    for (auto* c : colors_) c->setChecked(c->property("color").toString() == qs(b->color));
    end_->setCurrentIndex(std::max(0, end_->findData(b->loop ? QStringLiteral("loop") : QString::fromLatin1(toString(b->end)))));
    dissolve_->setValue(b->dissolve);
    fadeIn_->setValue(b->fadeIn);
    fadeOut_->setValue(b->fadeOut);
    fadeOut_->setEnabled(editable_ && !b->loop);
    toggle_->setText(b->collapsed ? tr("Show the videos") : tr("Hide the videos"));
    play_->setEnabled(count > 0);
    loading_ = false;
}

void BlockInspector::setEditable(bool editable) {
    editable_ = editable;
    for (QWidget* w : std::initializer_list<QWidget*>{name_, end_, dissolve_, fadeIn_, fadeOut_}) w->setEnabled(editable);
    for (auto* c : colors_) c->setEnabled(editable);
}

void BlockInspector::emitChange() {
    if (loading_ || block_.id.empty() || !editable_) return;
    Block b = block_;
    b.name = name_->text().trimmed().left(200).toStdString();
    for (auto* c : colors_)
        if (c->isChecked()) b.color = c->property("color").toString().toStdString();
    const QString end = end_->currentData().toString();
    b.loop = end == QLatin1String("loop");
    if (!b.loop) b.end = blockEndFromString(end.toStdString());
    b.dissolve = dissolve_->value();
    b.fadeIn = fadeIn_->value();
    b.fadeOut = fadeOut_->value();
    b.clamp();
    fadeOut_->setEnabled(!b.loop);
    block_ = b;
    emit changed(b);
}
