#include "inspector_panel.h"

#include "playlist_view.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace vthost;

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }

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

QLabel* infoLabel(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
    return l;
}

const char* kColorNames[] = {"", "red", "yellow", "green", "purple", "pink"};
QColor swatch(const char* c) {
    const std::string s = c;
    if (s == "red") return {232, 76, 61};
    if (s == "yellow") return {241, 196, 15};
    if (s == "green") return {46, 204, 113};
    if (s == "purple") return {155, 89, 182};
    if (s == "pink") return {232, 67, 147};
    return {90, 90, 90};
}
} // namespace

InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    title_ = new QLabel(this);
    title_->setStyleSheet(QStringLiteral("font-weight: bold; padding: 2px;"));
    outer->addWidget(title_);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    body_ = new QWidget(scroll);
    bodyLayout_ = new QVBoxLayout(body_);
    bodyLayout_->setContentsMargins(0, 0, 0, 0);

    // ---------------- Media Info
    {
        auto* box = new QWidget(body_);
        auto* f = new QFormLayout(box);
        infoFps_ = infoLabel(box);
        infoSize_ = infoLabel(box);
        infoCodecs_ = infoLabel(box);
        infoDuration_ = infoLabel(box);
        infoDepth_ = infoLabel(box);
        infoPath_ = infoLabel(box);
        f->addRow(tr("Framerate:"), infoFps_);
        f->addRow(tr("Dimension:"), infoSize_);
        f->addRow(tr("Codecs:"), infoCodecs_);
        f->addRow(tr("Duration:"), infoDuration_);
        f->addRow(tr("Bit-depth:"), infoDepth_);
        f->addRow(tr("Location:"), infoPath_);
        bodyLayout_->addWidget(makeSection(QStringLiteral("info"), tr("Media Info"), box, true));
    }

    // ---------------- Geral
    {
        auto* box = new QWidget(body_);
        auto* f = new QFormLayout(box);
        name_ = new QLineEdit(box);
        connect(name_, &QLineEdit::editingFinished, this, &InspectorPanel::emitChange);
        f->addRow(tr("Name:"), name_);
        auto* idRow = new QHBoxLayout;
        label_ = new QLineEdit(box);
        label_->setMaxLength(32);
        label_->setPlaceholderText(tr("number"));
        label_->setMaximumWidth(90);
        connect(label_, &QLineEdit::editingFinished, this, &InspectorPanel::emitChange);
        idRow->addWidget(label_);
        idRow->addSpacing(8);
        for (const char* c : kColorNames) {
            auto* b = new QToolButton(box);
            b->setCheckable(true);
            b->setAutoExclusive(true);
            b->setFixedSize(20, 20);
            const QColor col = swatch(c);
            b->setToolTip(*c ? QString::fromLatin1(c) : tr("No color"));
            b->setText(*c ? QString() : QStringLiteral("×"));
            b->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid #222; border-radius: 3px; }"
                                            "QToolButton:checked { border: 2px solid white; }")
                                 .arg(*c ? col.name() : QStringLiteral("#3a3a3a")));
            b->setProperty("color", QString::fromLatin1(c));
            connect(b, &QToolButton::clicked, this, &InspectorPanel::emitChange);
            colors_ << b;
            idRow->addWidget(b);
        }
        idRow->addStretch();
        f->addRow(tr("ID:"), idRow);
        notes_ = new QPlainTextEdit(box);
        notes_->setMaximumHeight(60);
        notesTimer_.setSingleShot(true);
        notesTimer_.setInterval(700);
        connect(notes_, &QPlainTextEdit::textChanged, this, [this] {
            if (!loading_) notesTimer_.start();
        });
        connect(&notesTimer_, &QTimer::timeout, this, &InspectorPanel::emitChange);
        f->addRow(tr("Notes:"), notes_);

        in_ = spin(box, 0.0, kMaxMediaSeconds, 3, tr(" s"), 0.1);
        out_ = spin(box, 0.0, kMaxMediaSeconds, 3, tr(" s"), 0.1);
        out_->setSpecialValueText(tr("end"));
        inFromHead_ = new QPushButton(QStringLiteral("⤓"), box);
        outFromHead_ = new QPushButton(QStringLiteral("⤓"), box);
        for (auto* b : {inFromHead_, outFromHead_}) {
            b->setFixedWidth(28);
            b->setToolTip(tr("Set from the playhead (cue on air)"));
        }
        connect(inFromHead_, &QPushButton::clicked, this, [this] {
            if (playhead_ >= 0.0) in_->setValue(playhead_);
        });
        connect(outFromHead_, &QPushButton::clicked, this, [this] {
            if (playhead_ >= 0.0) out_->setValue(playhead_);
        });
        auto* inRow = new QHBoxLayout;
        inRow->addWidget(in_);
        inRow->addWidget(inFromHead_);
        auto* outRow = new QHBoxLayout;
        outRow->addWidget(out_);
        outRow->addWidget(outFromHead_);
        f->addRow(tr("In:"), inRow);
        f->addRow(tr("Out:"), outRow);
        speed_ = spin(box, 25.0, 400.0, 0, QStringLiteral(" %"), 5.0);
        f->addRow(tr("Playback speed:"), speed_);
        playCount_ = new QSpinBox(box);
        playCount_->setRange(0, 9999);
        playCount_->setSpecialValueText(QStringLiteral("∞"));
        playCount_->setKeyboardTracking(false);
        f->addRow(tr("Play count (loop):"), playCount_);
        goto_ = new QComboBox(box);
        f->addRow(tr("Goto:"), goto_);
        for (auto* s : {in_, out_, speed_}) connect(s, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::emitChange);
        connect(playCount_, &QSpinBox::valueChanged, this, &InspectorPanel::emitChange);
        connect(goto_, &QComboBox::activated, this, &InspectorPanel::emitChange);
        bodyLayout_->addWidget(makeSection(QStringLiteral("general"), tr("General"), box, true));
    }

    // ---------------- Transições e comportamento
    {
        auto* box = new QWidget(body_);
        auto* f = new QFormLayout(box);
        fadeIn_ = spin(box, 0.0, 60.0, 2, tr(" s"), 0.25);
        fadeOut_ = spin(box, 0.0, 60.0, 2, tr(" s"), 0.25);
        crossfade_ = spin(box, 0.0, 60.0, 2, tr(" s"), 0.25);
        fadeIn_->setSpecialValueText(tr("off"));
        fadeOut_->setSpecialValueText(tr("off"));
        crossfade_->setSpecialValueText(tr("cut"));
        f->addRow(tr("Fade in (from black):"), fadeIn_);
        f->addRow(tr("Fade out (to black):"), fadeOut_);
        f->addRow(tr("Crossfade from previous:"), crossfade_);
        pauseStart_ = new QCheckBox(tr("Pause at start (wait for play)"), box);
        pauseEnd_ = new QCheckBox(tr("Pause at end (hold last frame)"), box);
        clearEnd_ = new QCheckBox(tr("Clear at end (black, stop)"), box);
        f->addRow(QString(), pauseStart_);
        f->addRow(QString(), pauseEnd_);
        f->addRow(QString(), clearEnd_);
        for (auto* s : {fadeIn_, fadeOut_, crossfade_}) connect(s, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::emitChange);
        for (auto* c : {pauseStart_, pauseEnd_, clearEnd_}) connect(c, &QCheckBox::toggled, this, &InspectorPanel::emitChange);
        bodyLayout_->addWidget(makeSection(QStringLiteral("transitions"), tr("Transitions"), box, true));
    }

    // ---------------- Áudio
    {
        auto* box = new QWidget(body_);
        auto* f = new QFormLayout(box);
        mute_ = new QCheckBox(tr("Mute"), box);
        volume_ = spin(box, -60.0, 12.0, 1, tr(" dB"), 0.5);
        normalize_ = new QCheckBox(tr("Normalize loudness"), box);
        loudness_ = infoLabel(box);
        reanalyze_ = new QPushButton(tr("Analyse again"), box);
        f->addRow(QString(), mute_);
        f->addRow(tr("Volume:"), volume_);
        f->addRow(QString(), normalize_);
        f->addRow(tr("Loudness:"), loudness_);
        f->addRow(QString(), reanalyze_);
        connect(mute_, &QCheckBox::toggled, this, &InspectorPanel::emitChange);
        connect(normalize_, &QCheckBox::toggled, this, &InspectorPanel::emitChange);
        connect(volume_, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::emitChange);
        connect(reanalyze_, &QPushButton::clicked, this, [this] {
            if (hasCue_) emit reanalyzeRequested(qs(cue_.path));
        });
        bodyLayout_->addWidget(makeSection(QStringLiteral("audio"), tr("Audio"), box, true));
    }

    // ---------------- Geometria
    {
        auto* box = new QWidget(body_);
        auto* f = new QFormLayout(box);
        fit_ = new QComboBox(box);
        fit_->addItem(tr("Scale to fit"), static_cast<int>(FitMode::Fit));
        fit_->addItem(tr("Scale to fill (crop)"), static_cast<int>(FitMode::Fill));
        fit_->addItem(tr("Stretch"), static_cast<int>(FitMode::Stretch));
        fit_->addItem(tr("Native size"), static_cast<int>(FitMode::Native));
        scale_ = spin(box, 1.0, 1000.0, 1, QStringLiteral(" %"), 1.0);
        posX_ = spin(box, -100000.0, 100000.0, 0, QStringLiteral(" px"), 1.0);
        posY_ = spin(box, -100000.0, 100000.0, 0, QStringLiteral(" px"), 1.0);
        f->addRow(tr("Resize mode:"), fit_);
        f->addRow(tr("Scale:"), scale_);
        f->addRow(tr("Position X:"), posX_);
        f->addRow(tr("Position Y:"), posY_);
        connect(fit_, &QComboBox::activated, this, &InspectorPanel::emitChange);
        for (auto* s : {scale_, posX_, posY_}) connect(s, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::emitChange);
        bodyLayout_->addWidget(makeSection(QStringLiteral("geometry"), tr("Geometry"), box, false));
    }
    bodyLayout_->addStretch();
    scroll->setWidget(body_);
    outer->addWidget(scroll, 1);
    setMinimumWidth(300);
    setCue(nullptr, -1, nullptr, {}, {}, -16.0);
}

QWidget* InspectorPanel::makeSection(const QString& key, const QString& title, QWidget* content, bool expandedByDefault) {
    auto* box = new QWidget(body_);
    auto* l = new QVBoxLayout(box);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    // O estado aberto/fechado de cada secção fica guardado.
    const QString settingsKey = QStringLiteral("ui/inspector/") + key;
    const bool expanded = QSettings().value(settingsKey, expandedByDefault).toBool();
    auto* header = new QToolButton(box);
    header->setText(title);
    header->setCheckable(true);
    header->setChecked(expanded);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    header->setAutoRaise(true);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header->setStyleSheet(QStringLiteral("QToolButton { text-align: left; font-weight: bold; padding: 4px; "
                                         "background: #2c2c2c; border-bottom: 1px solid #1c1c1c; }"));
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

void InspectorPanel::setCue(const Cue* cue, int index, const Project* project, const MediaInfo& info,
                            const AudioAnalysis& audio, double normalizeTarget) {
    loading_ = true;
    notesTimer_.stop();
    hasCue_ = cue != nullptr;
    body_->setEnabled(hasCue_);
    if (!cue) {
        title_->setText(tr("No cue selected"));
        loading_ = false;
        return;
    }
    // Se o utilizador está a escrever num campo desta mesma cue, não o interrompe.
    const bool sameCue = cue_.id == cue->id;
    cue_ = *cue;
    fileDuration_ = info.duration;
    title_->setText(QStringLiteral("%1  %2").arg(cue->label.empty() ? QString::number(index + 1) : qs(cue->label),
                                                 qs(cue->displayName())));

    infoFps_->setText(info.fps > 0.0 ? QString::number(info.fps, 'f', info.fps == std::floor(info.fps) ? 0 : 2) : QStringLiteral("-"));
    infoSize_->setText(info.width > 0 ? QStringLiteral("%1 × %2").arg(info.width).arg(info.height) : QStringLiteral("-"));
    QStringList codecs;
    if (!info.videoCodec.isEmpty()) codecs << info.videoCodec;
    if (!info.audioCodec.isEmpty())
        codecs << info.audioCodec + (info.audioChannels > 0 ? tr(" (%1 chs)").arg(info.audioChannels) : QString());
    infoCodecs_->setText(codecs.isEmpty() ? QStringLiteral("-") : codecs.join(QStringLiteral(" / ")));
    infoDuration_->setText(info.duration > 0.0 ? formatTimecode(info.duration, info.fps)
                                               : (info.still ? tr("still image") : QStringLiteral("-")));
    infoDepth_->setText(info.bitDepth > 0 ? QString::number(info.bitDepth) : QStringLiteral("-"));
    infoPath_->setText(QDir::toNativeSeparators(qs(cue->path)));
    if (info.ready && !info.ok) infoPath_->setText(infoPath_->text() + QStringLiteral("\n") + tr("(file not found or unreadable)"));

    auto setSpin = [](QDoubleSpinBox* s, double v) {
        const QSignalBlocker b(s);
        if (!s->hasFocus() || std::abs(s->value() - v) > 1e-6) s->setValue(v);
    };
    if (!sameCue || !name_->hasFocus()) {
        name_->setPlaceholderText(QFileInfo(qs(cue->path)).fileName());
        name_->setText(qs(cue->name));
    }
    if (!sameCue || !label_->hasFocus()) label_->setText(qs(cue->label));
    for (auto* b : colors_) {
        const QSignalBlocker bl(b);
        b->setChecked(b->property("color").toString() == qs(cue->color));
    }
    if (!sameCue || !notes_->hasFocus()) {
        const QSignalBlocker b(notes_);
        if (notes_->toPlainText() != qs(cue->notes)) notes_->setPlainText(qs(cue->notes));
    }
    const double maxT = info.duration > 0.0 ? info.duration : kMaxMediaSeconds;
    {
        const QSignalBlocker b1(in_), b2(out_);
        in_->setMaximum(maxT);
        out_->setMaximum(maxT);
    }
    setSpin(in_, cue->inPoint);
    setSpin(out_, cue->outPoint);
    setSpin(speed_, cue->speed * 100.0);
    {
        const QSignalBlocker b(playCount_);
        playCount_->setValue(cue->playCount);
    }
    {
        const QSignalBlocker b(goto_);
        goto_->clear();
        goto_->addItem(tr("None (next cue)"), QString());
        if (project)
            for (int i = 0; i < static_cast<int>(project->cues.size()); ++i) {
                const Cue& q = project->cues[static_cast<std::size_t>(i)];
                const QString num = q.label.empty() ? QString::number(i + 1) : qs(q.label);
                goto_->addItem(QStringLiteral("%1  %2").arg(num, qs(q.displayName())), qs(q.id));
            }
        goto_->setCurrentIndex(std::max(0, goto_->findData(qs(cue->gotoCue))));
    }
    setSpin(fadeIn_, cue->fadeIn);
    setSpin(fadeOut_, cue->fadeOut);
    setSpin(crossfade_, cue->crossfade);
    for (auto [c, v] : {std::pair{pauseStart_, cue->pauseAtStart}, std::pair{pauseEnd_, cue->pauseAtEnd},
                        std::pair{clearEnd_, cue->clearAtEnd}, std::pair{mute_, cue->muted},
                        std::pair{normalize_, cue->normalize}}) {
        const QSignalBlocker b(c);
        c->setChecked(v);
    }
    setSpin(volume_, cue->volumeDb);
    if (cue->loudness) {
        QString t = tr("%1 LUFS").arg(*cue->loudness, 0, 'f', 1);
        if (cue->peakDb) t += tr(" · peak %1 dBFS").arg(*cue->peakDb, 0, 'f', 1);
        const double g = normalizationGainDb(*cue->loudness, cue->peakDb, normalizeTarget);
        if (cue->normalize) t += QStringLiteral("\n") + tr("Normalization: %1%2 dB (target %3 LUFS)")
                                                            .arg(g >= 0 ? QStringLiteral("+") : QString())
                                                            .arg(g, 0, 'f', 1)
                                                            .arg(normalizeTarget, 0, 'f', 0);
        loudness_->setText(t);
    } else if (audio.ready && !audio.hasAudio) {
        loudness_->setText(tr("no audio"));
    } else if (audio.ready) {
        loudness_->setText(tr("silent"));
    } else {
        loudness_->setText(tr("analysing..."));
    }
    {
        const QSignalBlocker b(fit_);
        fit_->setCurrentIndex(std::max(0, fit_->findData(static_cast<int>(cue->fit))));
    }
    setSpin(scale_, cue->scale * 100.0);
    setSpin(posX_, cue->posX);
    setSpin(posY_, cue->posY);
    loading_ = false;
    updateEnabled();
}

void InspectorPanel::setPlayhead(double fileTime) {
    playhead_ = fileTime;
    updateEnabled();
}

void InspectorPanel::setEditable(bool editable) {
    editable_ = editable;
    updateEnabled();
}

void InspectorPanel::updateEnabled() {
    body_->setEnabled(hasCue_ && editable_);
    inFromHead_->setEnabled(playhead_ >= 0.0);
    outFromHead_->setEnabled(playhead_ >= 0.0);
}

void InspectorPanel::emitChange() {
    if (loading_ || !hasCue_ || !editable_) return;
    Cue c = cue_;
    c.name = ss(name_->text().trimmed());
    c.label = ss(label_->text().trimmed());
    for (auto* b : colors_)
        if (b->isChecked()) c.color = ss(b->property("color").toString());
    c.notes = ss(notes_->toPlainText());
    c.inPoint = in_->value();
    c.outPoint = out_->value();
    c.speed = speed_->value() / 100.0;
    c.playCount = playCount_->value();
    c.gotoCue = ss(goto_->currentData().toString());
    c.fadeIn = fadeIn_->value();
    c.fadeOut = fadeOut_->value();
    c.crossfade = crossfade_->value();
    c.pauseAtStart = pauseStart_->isChecked();
    c.pauseAtEnd = pauseEnd_->isChecked();
    c.clearAtEnd = clearEnd_->isChecked();
    c.muted = mute_->isChecked();
    c.volumeDb = volume_->value();
    c.normalize = normalize_->isChecked();
    c.fit = static_cast<FitMode>(fit_->currentData().toInt());
    c.scale = scale_->value() / 100.0;
    c.posX = posX_->value();
    c.posY = posY_->value();
    c.clamp();
    cue_ = c;
    emit cueChanged(c);
}
