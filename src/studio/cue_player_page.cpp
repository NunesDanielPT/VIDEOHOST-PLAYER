#include "cue_player_page.h"

#include "comp_grid.h"
#include "engine_client.h"
#include "media_cache.h"
#include "preview_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
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
    s->setKeyboardTracking(false);
    s->setAccelerated(true);
    return s;
}

QLabel* monitorTitle(const QString& text, const QString& color, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("font-weight: bold; letter-spacing: 1px; padding: 3px 6px; color: %1;"
                                    "background: #1b1b1b;").arg(color));
    return l;
}

QString timeText(double s) {
    if (s < 0) s = 0;
    const int t = static_cast<int>(std::floor(s));
    return QStringLiteral("%1:%2").arg(t / 60, 2, 10, QLatin1Char('0')).arg(t % 60, 2, 10, QLatin1Char('0'));
}

const QString kMediaFilterExt =
    QStringLiteral("*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mxf *.wmv *.mpg *.mpeg *.ts *.png *.jpg *.jpeg *.bmp *.tif "
                   "*.tiff *.gif *.webp *.mp3 *.wav *.aif *.aiff *.flac *.m4a *.ogg");
} // namespace

CuePlayerPage::CuePlayerPage(Project* project, EngineClient* engine, MediaCache* media, QWidget* parent)
    : QWidget(parent), project_(project), engine_(engine), media_(media) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------------- barra da composição
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("compbar"));
    bar->setStyleSheet(QStringLiteral("#compbar { background: #202020; border-bottom: 1px solid #111; }"));
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(8, 4, 8, 4);
    bl->addWidget(new QLabel(tr("Composition:"), bar));
    compCombo_ = new QComboBox(bar);
    compCombo_->setMinimumWidth(180);
    connect(compCombo_, &QComboBox::activated, this, [this](int i) { selectComposition(compCombo_->itemData(i).toString()); });
    bl->addWidget(compCombo_);
    auto* addComp = new QToolButton(bar);
    addComp->setText(QStringLiteral("+"));
    addComp->setToolTip(tr("New composition"));
    connect(addComp, &QToolButton::clicked, this, &CuePlayerPage::addComposition);
    bl->addWidget(addComp);
    auto* compMenuBtn = new QToolButton(bar);
    compMenuBtn->setText(QStringLiteral("⋯"));
    compMenuBtn->setToolTip(tr("Composition options"));
    connect(compMenuBtn, &QToolButton::clicked, this, &CuePlayerPage::compositionMenu);
    bl->addWidget(compMenuBtn);
    editWidgets_ << addComp << compMenuBtn;
    bl->addSpacing(16);
    bl->addWidget(new QLabel(tr("Master:"), bar));
    compOpacity_ = new QSlider(Qt::Horizontal, bar);
    compOpacity_->setRange(0, 100);
    compOpacity_->setFixedWidth(140);
    compOpacity_->setToolTip(tr("Composition opacity (live)"));
    compOpacityLabel_ = new QLabel(bar);
    compOpacityLabel_->setMinimumWidth(40);
    connect(compOpacity_, &QSlider::valueChanged, this, [this](int v) {
        compOpacityLabel_->setText(QStringLiteral("%1%").arg(v));
        if (loading_) return;
        if (Composition* c = comp()) {
            c->opacity = v / 100.0;
            sendCompLive();
            emit changed();
        }
    });
    bl->addWidget(compOpacity_);
    bl->addWidget(compOpacityLabel_);
    bl->addStretch();
    auto* addLayerBtn = new QPushButton(tr("+ Layer"), bar);
    connect(addLayerBtn, &QPushButton::clicked, this, [this] {
        const Composition* c = comp();
        addLayer(c ? static_cast<int>(c->layers.size()) - 1 : 0);
    });
    auto* addColBtn = new QPushButton(tr("+ Column"), bar);
    connect(addColBtn, &QPushButton::clicked, this, [this] { addColumn(-1); });
    editWidgets_ << addLayerBtn << addColBtn;
    bl->addWidget(addLayerBtn);
    bl->addWidget(addColBtn);
    auto* clearBtn = new QPushButton(tr("Clear all"), bar);
    clearBtn->setToolTip(tr("Stop every layer of this composition (with the layer transitions)"));
    connect(clearBtn, &QPushButton::clicked, this, &CuePlayerPage::clearAll);
    bl->addWidget(clearBtn);
    auto* outBtn = new QPushButton(tr("Outputs…"), bar);
    outBtn->setToolTip(tr("Advanced output: screens and slices"));
    connect(outBtn, &QPushButton::clicked, this, &CuePlayerPage::openOutputs);
    bl->addWidget(outBtn);
    root->addWidget(bar);

    // ---------------- monitores + grelha | propriedades
    auto* hsplit = new QSplitter(Qt::Horizontal, this);
    auto* vsplit = new QSplitter(Qt::Vertical, hsplit);

    auto* monitors = new QWidget(vsplit);
    auto* ml = new QHBoxLayout(monitors);
    ml->setContentsMargins(4, 4, 4, 4);
    ml->setSpacing(6);
    {
        auto* box = new QWidget(monitors);
        auto* v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        previewTitle_ = monitorTitle(tr("PREVIEW"), QStringLiteral("#e0a030"), box);
        previewMon_ = new PreviewWidget(box, 1);
        previewMon_->setCanvasSize(1280, 720);
        previewMon_->setIdleText(tr("Ctrl+click a clip to see it here before launching it"));
        v->addWidget(previewTitle_);
        v->addWidget(previewMon_, 1);
        ml->addWidget(box, 1);
    }
    {
        auto* box = new QWidget(monitors);
        auto* v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        programTitle_ = monitorTitle(tr("PROGRAM"), QStringLiteral("#e44"), box);
        programMon_ = new PreviewWidget(box, 0);
        v->addWidget(programTitle_);
        v->addWidget(programMon_, 1);
        ml->addWidget(box, 1);
    }
    vsplit->addWidget(monitors);

    grid_ = new CompGrid(media_, vsplit);
    auto* scroll = new QScrollArea(vsplit);
    scroll->setWidget(grid_);
    scroll->setWidgetResizable(false);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #181818; }"));
    vsplit->addWidget(scroll);
    vsplit->setStretchFactor(0, 1);
    vsplit->setStretchFactor(1, 1);
    vsplit->setSizes({360, 340});

    tabs_ = new QTabWidget(hsplit);
    tabs_->addTab(buildCompForm(), tr("Composition"));
    tabs_->addTab(buildLayerForm(), tr("Layer"));
    tabs_->addTab(buildClipForm(), tr("Clip"));
    tabs_->setMinimumWidth(300);
    hsplit->addWidget(vsplit);
    hsplit->addWidget(tabs_);
    hsplit->setStretchFactor(0, 1);
    hsplit->setStretchFactor(1, 0);
    hsplit->setSizes({1100, 340});
    root->addWidget(hsplit, 1);

    connect(grid_, &CompGrid::launchClip, this, &CuePlayerPage::launchClip);
    connect(grid_, &CompGrid::launchColumn, this, &CuePlayerPage::launchColumn);
    connect(grid_, &CompGrid::selectionChanged, this, &CuePlayerPage::onSelection);
    connect(grid_, &CompGrid::clearLayer, this, &CuePlayerPage::clearLayer);
    connect(grid_, &CompGrid::layerToggle, this, &CuePlayerPage::layerToggle);
    connect(grid_, &CompGrid::layerRename, this, &CuePlayerPage::renameLayer);
    connect(grid_, &CompGrid::layerMenu, this, &CuePlayerPage::showLayerMenu);
    connect(grid_, &CompGrid::columnMenu, this, &CuePlayerPage::showColumnMenu);
    connect(grid_, &CompGrid::cellMenu, this, &CuePlayerPage::showCellMenu);
    connect(grid_, &CompGrid::filesDropped, this, &CuePlayerPage::onFilesDropped);
    connect(grid_, &CompGrid::clipMoved, this, &CuePlayerPage::onClipMoved);
    connect(grid_, &CompGrid::addColumnRequested, this, [this] { addColumn(-1); });
    connect(media_, &MediaCache::updated, grid_, [this](const QString&) {
        grid_->update();
        loadForms();
    });

    refresh();
}

// ------------------------------------------------------------------ acesso ao projecto

Composition* CuePlayerPage::comp() {
    for (auto& c : project_->compositions)
        if (qs(c.id) == compId_) return &c;
    return nullptr;
}

CompLayer* CuePlayerPage::layer(const QString& id) {
    Composition* c = comp();
    if (!c) return nullptr;
    for (auto& l : c->layers)
        if (qs(l.id) == id) return &l;
    return nullptr;
}

Clip* CuePlayerPage::clip(const QString& layerId, const QString& columnId) {
    Composition* c = comp();
    if (!c) return nullptr;
    for (auto& col : c->columns) {
        if (qs(col.id) != columnId) continue;
        auto it = col.cells.find(ss(layerId));
        return it == col.cells.end() ? nullptr : &it->second;
    }
    return nullptr;
}

void CuePlayerPage::edited() {
    grid_->setComposition(project_, compId_);
    grid_->adjustSize();
    emit changed();
}

// ------------------------------------------------------------------ composições

void CuePlayerPage::refresh() {
    if (project_->compositions.empty()) project_->compositions.push_back(Composition::makeDefault("comp1", "Composition 1"));
    if (!comp()) compId_ = qs(project_->compositions.front().id);
    if (!layer(selLayer_)) selLayer_.clear();
    rebuildCompCombo();
    grid_->setComposition(project_, compId_);
    grid_->setSelection(selLayer_, selColumn_);
    grid_->adjustSize();
    for (const auto& c : project_->compositions)
        for (const auto& col : c.columns)
            for (const auto& [lid, k] : col.cells) {
                media_->info(qs(k.path));
                media_->audio(qs(k.path));
            }
    loadForms();
    updateMonitors();
}

void CuePlayerPage::rebuildCompCombo() {
    const QSignalBlocker b(compCombo_);
    compCombo_->clear();
    for (const auto& c : project_->compositions) compCombo_->addItem(qs(c.name), qs(c.id));
    compCombo_->setCurrentIndex(compCombo_->findData(compId_));
}

void CuePlayerPage::selectComposition(const QString& id) {
    if (id == compId_) return;
    compId_ = id;
    selLayer_.clear();
    selColumn_.clear();
    rebuildCompCombo();
    grid_->setComposition(project_, compId_);
    grid_->setSelection({}, {});
    grid_->adjustSize();
    loadForms();
    updateMonitors();
    if (active_) setActive(true); // Program passa a mostrar esta composição
}

void CuePlayerPage::addComposition() {
    if (!editable_) return;
    const std::string id = project_->newCompositionId();
    const QString name = tr("Composition %1").arg(project_->compositions.size() + 1);
    Composition c = Composition::makeDefault(id, ss(name));
    if (const Composition* cur = comp()) {
        c.width = cur->width;
        c.height = cur->height;
    }
    project_->compositions.push_back(std::move(c));
    compId_.clear();
    emit changed();
    selectComposition(qs(id));
}

void CuePlayerPage::compositionMenu() {
    Composition* c = comp();
    if (!c || !editable_) return;
    QMenu m(this);
    m.addAction(tr("Rename…"), this, [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename composition"), tr("Name:"), QLineEdit::Normal, qs(comp()->name), &ok);
        Composition* cc = comp();
        if (!ok || n.trimmed().isEmpty() || !cc) return;
        cc->name = ss(n.trimmed());
        rebuildCompCombo();
        loadForms();
        edited();
    });
    m.addAction(tr("Duplicate"), this, [this] {
        Composition copy = *comp();
        copy.id = project_->newCompositionId();
        copy.name += ss(tr(" (copy)"));
        const QString id = qs(copy.id);
        project_->compositions.push_back(std::move(copy));
        emit changed();
        selectComposition(id);
    });
    auto* del = m.addAction(tr("Delete composition"), this, [this] {
        if (QMessageBox::question(this, tr("Delete composition"),
                                  tr("Delete \"%1\"? Slices that show it will go black.").arg(qs(comp()->name))) != QMessageBox::Yes)
            return;
        auto& v = project_->compositions;
        v.erase(std::remove_if(v.begin(), v.end(), [this](const Composition& x) { return qs(x.id) == compId_; }), v.end());
        compId_.clear();
        refresh();
        emit changed();
        if (active_) setActive(true);
    });
    del->setEnabled(project_->compositions.size() > 1);
    m.exec(QCursor::pos());
}

// ------------------------------------------------------------------ layers e colunas

void CuePlayerPage::addLayer(int aboveIndex) {
    Composition* c = comp();
    if (!c || !editable_ || c->layers.size() >= 32) return;
    CompLayer l;
    l.id = c->newLayerId();
    l.name = ss(tr("Layer %1").arg(c->layers.size() + 1));
    const int at = std::clamp(aboveIndex + 1, 0, static_cast<int>(c->layers.size()));
    c->layers.insert(c->layers.begin() + at, l);
    selLayer_ = qs(l.id);
    grid_->setSelection(selLayer_, selColumn_);
    loadForms();
    edited();
}

void CuePlayerPage::deleteLayer(const QString& id) {
    Composition* c = comp();
    if (!c || !editable_ || c->layers.size() <= 1) return;
    const std::string lid = ss(id);
    bool hasClips = false;
    for (const auto& col : c->columns) hasClips |= col.cells.count(lid) > 0;
    if (hasClips && QMessageBox::question(this, tr("Delete layer"), tr("Delete this layer and its clips?")) != QMessageBox::Yes)
        return;
    clearLayer(id);
    c = comp();
    c->layers.erase(std::remove_if(c->layers.begin(), c->layers.end(), [&](const CompLayer& l) { return l.id == lid; }),
                    c->layers.end());
    for (auto& col : c->columns) col.cells.erase(lid);
    if (selLayer_ == id) selLayer_.clear();
    grid_->setSelection(selLayer_, selColumn_);
    loadForms();
    edited();
}

void CuePlayerPage::addColumn(int atIndex) {
    Composition* c = comp();
    if (!c || !editable_ || c->columns.size() >= 256) return;
    CompColumn col;
    col.id = c->newColumnId();
    col.name = ss(tr("Column %1").arg(c->columns.size() + 1));
    const int at = atIndex < 0 ? static_cast<int>(c->columns.size()) : std::clamp(atIndex, 0, static_cast<int>(c->columns.size()));
    c->columns.insert(c->columns.begin() + at, std::move(col));
    edited();
}

void CuePlayerPage::deleteColumn(const QString& id) {
    Composition* c = comp();
    if (!c || !editable_ || c->columns.size() <= 1) return;
    auto it = std::find_if(c->columns.begin(), c->columns.end(), [&](const CompColumn& k) { return qs(k.id) == id; });
    if (it == c->columns.end()) return;
    if (!it->cells.empty() &&
        QMessageBox::question(this, tr("Delete column"), tr("Delete this column and its clips?")) != QMessageBox::Yes)
        return;
    c = comp();
    it = std::find_if(c->columns.begin(), c->columns.end(), [&](const CompColumn& k) { return qs(k.id) == id; });
    if (it == c->columns.end()) return;
    c->columns.erase(it);
    if (selColumn_ == id) selColumn_.clear();
    grid_->setSelection(selLayer_, selColumn_);
    loadForms();
    edited();
}

void CuePlayerPage::renameLayer(const QString& id) {
    CompLayer* l = layer(id);
    if (!l || !editable_) return;
    bool ok = false;
    const QString n = QInputDialog::getText(this, tr("Rename layer"), tr("Name:"), QLineEdit::Normal, qs(l->name), &ok);
    if (!ok || n.trimmed().isEmpty()) return;
    l = layer(id);
    if (!l) return;
    l->name = ss(n.trimmed());
    loadForms();
    edited();
}

void CuePlayerPage::renameColumn(const QString& id) {
    Composition* c = comp();
    if (!c || !editable_) return;
    for (auto& col : c->columns) {
        if (qs(col.id) != id) continue;
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename column"), tr("Name:"), QLineEdit::Normal, qs(col.name), &ok);
        if (!ok || n.trimmed().isEmpty()) return;
        // O diálogo é modal mas o projecto pode ter mudado (desfazer): procura de novo.
        for (auto& k : comp()->columns)
            if (qs(k.id) == id) k.name = ss(n.trimmed());
        edited();
        return;
    }
}

// ------------------------------------------------------------------ lançar

void CuePlayerPage::launchClip(const QString& layerId, const QString& column) {
    if (!clip(layerId, column)) return;
    protocol::Command c;
    c.type = protocol::CommandType::LaunchClip;
    c.comp = ss(compId_);
    c.layer = ss(layerId);
    c.column = ss(column);
    engine_->send(c, [this](const protocol::ParsedResponse& r) {
        if (!r.ok) emit message(tr("Could not launch the clip: %1").arg(qs(r.error)));
    });
}

void CuePlayerPage::launchSelected() {
    if (clip(selLayer_, selColumn_)) launchClip(selLayer_, selColumn_);
    else if (!selColumn_.isEmpty()) launchColumn(selColumn_);
}

void CuePlayerPage::launchColumn(const QString& column) {
    protocol::Command c;
    c.type = protocol::CommandType::LaunchColumn;
    c.comp = ss(compId_);
    c.column = ss(column);
    engine_->send(c, [this](const protocol::ParsedResponse& r) {
        if (!r.ok) emit message(tr("Could not launch the column: %1").arg(qs(r.error)));
    });
}

void CuePlayerPage::clearLayer(const QString& layerId) {
    protocol::Command c;
    c.type = protocol::CommandType::ClearLayer;
    c.comp = ss(compId_);
    c.layer = ss(layerId);
    engine_->send(c);
}

void CuePlayerPage::clearAll() {
    protocol::Command c;
    c.type = protocol::CommandType::ClearComposition;
    c.comp = ss(compId_);
    engine_->send(c);
}

void CuePlayerPage::onSelection(const QString& layerId, const QString& column) {
    const bool layerChanged = layerId != selLayer_;
    selLayer_ = layerId;
    selColumn_ = column;
    loadForms();
    if (!column.isEmpty() && clip(layerId, column)) tabs_->setCurrentIndex(2);
    else if (layerChanged) tabs_->setCurrentIndex(1);
    previewSelection();
}

void CuePlayerPage::previewSelection() {
    protocol::Command c;
    if (clip(selLayer_, selColumn_)) {
        c.type = protocol::CommandType::PreviewClip;
        c.comp = ss(compId_);
        c.layer = ss(selLayer_);
        c.column = ss(selColumn_);
    } else {
        c.type = protocol::CommandType::PreviewStop;
    }
    engine_->sendLatest(QStringLiteral("preview"), c);
    updateMonitors();
}

void CuePlayerPage::setActive(bool active) {
    active_ = active;
    protocol::Command c;
    c.type = protocol::CommandType::SetPreviewSource;
    c.source = active && !compId_.isEmpty() ? "comp:" + ss(compId_) : std::string("media");
    engine_->sendLatest(QStringLiteral("previewSource"), c);
    if (!active) {
        protocol::Command stop;
        stop.type = protocol::CommandType::PreviewStop;
        engine_->sendLatest(QStringLiteral("preview"), stop);
    } else {
        previewSelection();
    }
}

void CuePlayerPage::onStatus(const protocol::EngineStatus& s) {
    QHash<QString, QPair<QString, double>> playing;
    for (const auto& l : s.compLayers) {
        if (qs(l.comp) != compId_ || l.column.empty()) continue;
        const double total = l.elapsed + l.remaining;
        playing.insert(qs(l.layer), {qs(l.column), total > 0.0 ? l.elapsed / total : 0.0});
    }
    grid_->setPlaying(playing);
    programMon_->setFps(s.fps);
    previewMon_->setFps(s.fps);
    // Tempo que falta no clip da layer selecionada (ou da de cima a tocar).
    const protocol::CompLayerStatus* shown = nullptr;
    for (const auto& l : s.compLayers)
        if (qs(l.comp) == compId_ && !l.column.empty() && (!shown || qs(l.layer) == selLayer_)) shown = &l;
    if (shown) programMon_->setTimes(timeText(shown->elapsed), QStringLiteral("-") + timeText(shown->remaining), Qt::white);
    else programMon_->setTimes({}, {}, Qt::white);
}

void CuePlayerPage::updateMonitors() {
    const Composition* c = comp();
    if (c) {
        programMon_->setCanvasSize(c->width, c->height);
        programTitle_->setText(tr("PROGRAM — %1").arg(qs(c->name)));
    }
    const Clip* k = clip(selLayer_, selColumn_);
    previewTitle_->setText(k ? tr("PREVIEW — %1").arg(qs(k->displayName())) : tr("PREVIEW"));
}

// ------------------------------------------------------------------ edição da grelha

void CuePlayerPage::onFilesDropped(const QString& layerId, const QString& column, const QStringList& files) {
    Composition* c = comp();
    if (!c || !editable_) return;
    // Vários ficheiros: preenchem a layer para a direita (cria colunas se faltarem).
    auto colIt = std::find_if(c->columns.begin(), c->columns.end(), [&](const CompColumn& k) { return qs(k.id) == column; });
    if (colIt == c->columns.end()) return;
    std::size_t at = static_cast<std::size_t>(colIt - c->columns.begin());
    for (const QString& f : files) {
        if (at >= c->columns.size()) {
            if (c->columns.size() >= 256) break;
            CompColumn col;
            col.id = c->newColumnId();
            col.name = ss(tr("Column %1").arg(c->columns.size() + 1));
            c->columns.push_back(std::move(col));
        }
        Clip k;
        k.path = ss(QDir::toNativeSeparators(f));
        const AudioAnalysis a = media_->audio(f);
        if (a.ready) {
            k.loudness = a.loudness;
            k.peakDb = a.peakDb;
        }
        media_->info(f);
        c->columns[at].cells[ss(layerId)] = std::move(k);
        ++at;
    }
    QSettings().setValue(QStringLiteral("lastMediaDir"), QFileInfo(files.front()).absolutePath());
    selLayer_ = layerId;
    selColumn_ = column;
    grid_->setSelection(selLayer_, selColumn_);
    loadForms();
    edited();
    tabs_->setCurrentIndex(2);
}

void CuePlayerPage::onClipMoved(const QString& fromL, const QString& fromC, const QString& toL, const QString& toC, bool copy) {
    Composition* c = comp();
    const Clip* src = clip(fromL, fromC);
    if (!c || !src || !editable_) return;
    const Clip k = *src;
    for (auto& col : c->columns) {
        if (!copy && qs(col.id) == fromC) col.cells.erase(ss(fromL));
    }
    for (auto& col : c->columns)
        if (qs(col.id) == toC) col.cells[ss(toL)] = k;
    selLayer_ = toL;
    selColumn_ = toC;
    grid_->setSelection(selLayer_, selColumn_);
    loadForms();
    edited();
}

void CuePlayerPage::layerToggle(const QString& id, QChar which) {
    CompLayer* l = layer(id);
    if (!l) return;
    // Interruptores ao vivo: funcionam também em Show Mode (como o volume geral).
    if (which == QLatin1Char('B')) l->bypass = !l->bypass;
    else if (which == QLatin1Char('S')) l->solo = !l->solo;
    else if (which == QLatin1Char('V')) l->video = !l->video;
    else if (which == QLatin1Char('A')) l->audio = !l->audio;
    else return;
    sendLayerLive(*l);
    loadForms();
    edited();
}

void CuePlayerPage::showLayerMenu(const QString& id, const QPoint& pos) {
    const Composition* c = comp();
    if (!c) return;
    int index = 0;
    for (std::size_t i = 0; i < c->layers.size(); ++i)
        if (qs(c->layers[i].id) == id) index = static_cast<int>(i);
    QMenu m(this);
    m.addAction(tr("Clear layer"), this, [this, id] { clearLayer(id); });
    m.addSeparator();
    QList<QAction*> edits;
    edits << m.addAction(tr("Rename…"), this, [this, id] { renameLayer(id); });
    edits << m.addAction(tr("Add layer above"), this, [this, index] { addLayer(index); });
    edits << m.addAction(tr("Add layer below"), this, [this, index] { addLayer(index - 1); });
    edits << m.addAction(tr("Delete layer"), this, [this, id] { deleteLayer(id); });
    for (auto* a : edits) a->setEnabled(editable_);
    edits.back()->setEnabled(editable_ && c->layers.size() > 1);
    m.exec(pos);
}

void CuePlayerPage::showColumnMenu(const QString& id, const QPoint& pos) {
    const Composition* c = comp();
    if (!c) return;
    int index = 0;
    for (std::size_t i = 0; i < c->columns.size(); ++i)
        if (qs(c->columns[i].id) == id) index = static_cast<int>(i);
    QMenu m(this);
    m.addAction(tr("Launch column"), this, [this, id] { launchColumn(id); });
    m.addSeparator();
    QList<QAction*> edits;
    edits << m.addAction(tr("Rename…"), this, [this, id] { renameColumn(id); });
    edits << m.addAction(tr("Insert column before"), this, [this, index] { addColumn(index); });
    edits << m.addAction(tr("Insert column after"), this, [this, index] { addColumn(index + 1); });
    edits << m.addAction(tr("Delete column"), this, [this, id] { deleteColumn(id); });
    for (auto* a : edits) a->setEnabled(editable_);
    edits.back()->setEnabled(editable_ && c->columns.size() > 1);
    m.exec(pos);
}

void CuePlayerPage::showCellMenu(const QString& layerId, const QString& column, const QPoint& pos) {
    const bool has = clip(layerId, column) != nullptr;
    QMenu m(this);
    if (has) {
        m.addAction(tr("Launch"), this, [this, layerId, column] { launchClip(layerId, column); });
        m.addSeparator();
    }
    auto* add = m.addAction(has ? tr("Replace file…") : tr("Add media…"), this,
                            [this, layerId, column] { replaceClipFile(layerId, column); });
    add->setEnabled(editable_);
    if (has) {
        auto* rm = m.addAction(tr("Remove clip"), this, [this, layerId, column] {
            for (auto& col : comp()->columns)
                if (qs(col.id) == column) col.cells.erase(ss(layerId));
            loadForms();
            edited();
            previewSelection();
        });
        rm->setEnabled(editable_);
    }
    m.exec(pos);
}

void CuePlayerPage::replaceClipFile(const QString& layerId, const QString& column) {
    if (!editable_) return;
    const QString f = QFileDialog::getOpenFileName(this, tr("Choose media"), QSettings().value(QStringLiteral("lastMediaDir")).toString(),
                                                   tr("Media (%1);;All files (*)").arg(kMediaFilterExt));
    if (f.isEmpty()) return;
    if (Clip* k = clip(layerId, column)) {
        k->path = ss(QDir::toNativeSeparators(f));
        k->loudness.reset();
        k->peakDb.reset();
        k->thumbTime.reset();
        k->inPoint = 0.0;
        k->outPoint = 0.0;
        const AudioAnalysis a = media_->audio(f);
        if (a.ready) {
            k->loudness = a.loudness;
            k->peakDb = a.peakDb;
        }
        media_->info(f);
        QSettings().setValue(QStringLiteral("lastMediaDir"), QFileInfo(f).absolutePath());
        loadForms();
        edited();
        previewSelection();
    } else {
        onFilesDropped(layerId, column, {f});
    }
}

// ------------------------------------------------------------------ propriedades

QWidget* CuePlayerPage::buildCompForm() {
    auto* w = new QWidget(this);
    auto* f = new QFormLayout(w);
    cName_ = new QLineEdit(w);
    f->addRow(tr("Name:"), cName_);
    cWidth_ = new QSpinBox(w);
    cHeight_ = new QSpinBox(w);
    for (auto* s : {cWidth_, cHeight_}) {
        s->setRange(16, 16384);
        s->setKeyboardTracking(false);
    }
    auto* res = new QHBoxLayout;
    res->addWidget(cWidth_);
    res->addWidget(new QLabel(QStringLiteral("×"), w));
    res->addWidget(cHeight_);
    f->addRow(tr("Resolution:"), res);
    auto* presets = new QComboBox(w);
    presets->addItem(tr("Presets…"));
    const QList<QSize> sizes = {{1920, 1080}, {3840, 2160}, {1280, 720}, {2560, 1440}, {1080, 1920}, {3840, 1080}, {5760, 1080}};
    for (const QSize& s : sizes) presets->addItem(QStringLiteral("%1 × %2").arg(s.width()).arg(s.height()), s);
    connect(presets, &QComboBox::activated, this, [this, presets](int i) {
        if (i <= 0) return;
        const QSize s = presets->itemData(i).toSize();
        const QSignalBlocker b1(cWidth_), b2(cHeight_);
        cWidth_->setValue(s.width());
        cHeight_->setValue(s.height());
        presets->setCurrentIndex(0);
        applyCompForm();
    });
    f->addRow(QString(), presets);
    cVolume_ = spin(w, -60.0, 12.0, 1, tr(" dB"), 0.5);
    f->addRow(tr("Volume:"), cVolume_);
    auto* note = new QLabel(tr("The composition is a canvas of its own: layers are drawn on it from the bottom up. "
                               "In Outputs… each slice chooses what it shows: the MEDIAPLAYER, a composition or one layer."),
                            w);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #999;"));
    f->addRow(note);
    connect(cName_, &QLineEdit::editingFinished, this, &CuePlayerPage::applyCompForm);
    connect(cWidth_, &QSpinBox::valueChanged, this, &CuePlayerPage::applyCompForm);
    connect(cHeight_, &QSpinBox::valueChanged, this, &CuePlayerPage::applyCompForm);
    connect(cVolume_, &QDoubleSpinBox::valueChanged, this, &CuePlayerPage::applyCompForm);
    editWidgets_ << cName_ << cWidth_ << cHeight_ << presets;
    return w;
}

QWidget* CuePlayerPage::buildLayerForm() {
    auto* stack = new QStackedWidget(this);
    lEmpty_ = new QLabel(tr("Select a layer in the grid."), stack);
    lEmpty_->setAlignment(Qt::AlignCenter);
    stack->addWidget(lEmpty_);
    auto* scroll = new QScrollArea(stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    layerForm_ = new QWidget(scroll);
    auto* f = new QFormLayout(layerForm_);
    lName_ = new QLineEdit(layerForm_);
    f->addRow(tr("Name:"), lName_);
    lOpacity_ = new QSlider(Qt::Horizontal, layerForm_);
    lOpacity_->setRange(0, 100);
    lOpacityLabel_ = new QLabel(layerForm_);
    lOpacityLabel_->setMinimumWidth(36);
    auto* op = new QHBoxLayout;
    op->addWidget(lOpacity_, 1);
    op->addWidget(lOpacityLabel_);
    f->addRow(tr("Opacity:"), op);
    lBlend_ = new QComboBox(layerForm_);
    lBlend_->addItem(tr("Normal"), static_cast<int>(BlendMode::Normal));
    lBlend_->addItem(tr("Add"), static_cast<int>(BlendMode::Add));
    lBlend_->addItem(tr("Multiply"), static_cast<int>(BlendMode::Multiply));
    lBlend_->addItem(tr("Screen"), static_cast<int>(BlendMode::Screen));
    f->addRow(tr("Blend:"), lBlend_);
    lTransition_ = spin(layerForm_, 0.0, 30.0, 2, tr(" s"), 0.25);
    lTransition_->setSpecialValueText(tr("cut"));
    lTransition_->setToolTip(tr("Dissolve when a new clip is launched or the layer is cleared"));
    f->addRow(tr("Transition:"), lTransition_);
    lVolume_ = spin(layerForm_, -60.0, 12.0, 1, tr(" dB"), 0.5);
    f->addRow(tr("Volume:"), lVolume_);
    lFit_ = new QComboBox(layerForm_);
    lFit_->addItem(tr("Fit"), static_cast<int>(FitMode::Fit));
    lFit_->addItem(tr("Fill"), static_cast<int>(FitMode::Fill));
    lFit_->addItem(tr("Stretch"), static_cast<int>(FitMode::Stretch));
    lFit_->addItem(tr("Native size"), static_cast<int>(FitMode::Native));
    f->addRow(tr("Fit:"), lFit_);
    lPosX_ = spin(layerForm_, -16384.0, 16384.0, 0, tr(" px"), 1.0);
    lPosY_ = spin(layerForm_, -16384.0, 16384.0, 0, tr(" px"), 1.0);
    auto* pos = new QHBoxLayout;
    pos->addWidget(lPosX_);
    pos->addWidget(lPosY_);
    f->addRow(tr("Position X / Y:"), pos);
    lScale_ = spin(layerForm_, 1.0, 1000.0, 1, QStringLiteral(" %"), 1.0);
    f->addRow(tr("Scale:"), lScale_);
    lRotation_ = spin(layerForm_, -3600.0, 3600.0, 1, QStringLiteral("°"), 1.0);
    f->addRow(tr("Rotation:"), lRotation_);
    lFlipH_ = new QCheckBox(tr("Flip horizontally"), layerForm_);
    lFlipV_ = new QCheckBox(tr("Flip vertically"), layerForm_);
    f->addRow(QString(), lFlipH_);
    f->addRow(QString(), lFlipV_);
    lVideo_ = new QCheckBox(tr("Video (V)"), layerForm_);
    lAudio_ = new QCheckBox(tr("Audio (A)"), layerForm_);
    lSolo_ = new QCheckBox(tr("Solo (S)"), layerForm_);
    lBypass_ = new QCheckBox(tr("Bypass (B)"), layerForm_);
    auto* sw = new QHBoxLayout;
    for (auto* c : {lVideo_, lAudio_, lSolo_, lBypass_}) sw->addWidget(c);
    f->addRow(sw);
    auto* reset = new QPushButton(tr("Reset transform"), layerForm_);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (CompLayer* l = layer(selLayer_)) {
            const LayerProps def;
            l->props.posX = def.posX;
            l->props.posY = def.posY;
            l->props.scale = def.scale;
            l->props.rotation = def.rotation;
            l->props.flipH = l->props.flipV = false;
            l->props.fit = def.fit;
            sendLayerLive(*l);
            loadForms();
            edited();
        }
    });
    f->addRow(QString(), reset);

    connect(lName_, &QLineEdit::editingFinished, this, &CuePlayerPage::applyLayerForm);
    connect(lOpacity_, &QSlider::valueChanged, this, [this](int v) {
        lOpacityLabel_->setText(QStringLiteral("%1%").arg(v));
        applyLayerForm();
    });
    for (auto* c : {lBlend_, lFit_}) connect(c, &QComboBox::activated, this, &CuePlayerPage::applyLayerForm);
    for (auto* s : {lTransition_, lVolume_, lPosX_, lPosY_, lScale_, lRotation_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &CuePlayerPage::applyLayerForm);
    for (auto* c : {lFlipH_, lFlipV_, lVideo_, lAudio_, lSolo_, lBypass_})
        connect(c, &QCheckBox::toggled, this, &CuePlayerPage::applyLayerForm);
    editWidgets_ << lName_ << reset;

    scroll->setWidget(layerForm_);
    stack->addWidget(scroll);
    return stack;
}

QWidget* CuePlayerPage::buildClipForm() {
    auto* stack = new QStackedWidget(this);
    kEmpty_ = new QLabel(tr("Select a clip in the grid, or drop media files on a cell."), stack);
    kEmpty_->setAlignment(Qt::AlignCenter);
    kEmpty_->setWordWrap(true);
    stack->addWidget(kEmpty_);
    auto* scroll = new QScrollArea(stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    clipForm_ = new QWidget(scroll);
    auto* f = new QFormLayout(clipForm_);
    kName_ = new QLineEdit(clipForm_);
    kName_->setPlaceholderText(tr("(file name)"));
    f->addRow(tr("Name:"), kName_);
    kFile_ = new QLabel(clipForm_);
    kFile_->setWordWrap(true);
    kFile_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* replace = new QPushButton(tr("Replace…"), clipForm_);
    connect(replace, &QPushButton::clicked, this, [this] { replaceClipFile(selLayer_, selColumn_); });
    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(kFile_, 1);
    fileRow->addWidget(replace);
    f->addRow(tr("File:"), fileRow);
    kInfo_ = new QLabel(clipForm_);
    kInfo_->setStyleSheet(QStringLiteral("color: #999;"));
    f->addRow(QString(), kInfo_);
    kIn_ = spin(clipForm_, 0.0, 360000.0, 2, tr(" s"), 0.1);
    kOut_ = spin(clipForm_, 0.0, 360000.0, 2, tr(" s"), 0.1);
    kOut_->setSpecialValueText(tr("end"));
    f->addRow(tr("In:"), kIn_);
    f->addRow(tr("Out:"), kOut_);
    kSpeed_ = spin(clipForm_, 25.0, 400.0, 0, QStringLiteral(" %"), 5.0);
    f->addRow(tr("Playback speed:"), kSpeed_);
    kEnd_ = new QComboBox(clipForm_);
    kEnd_->addItem(tr("Loop"), static_cast<int>(ClipEnd::Loop));
    kEnd_->addItem(tr("Hold last frame"), static_cast<int>(ClipEnd::Hold));
    kEnd_->addItem(tr("Clear (stop the layer)"), static_cast<int>(ClipEnd::Clear));
    f->addRow(tr("At the end:"), kEnd_);
    kVolume_ = spin(clipForm_, -60.0, 12.0, 1, tr(" dB"), 0.5);
    f->addRow(tr("Volume:"), kVolume_);
    kNormalize_ = new QCheckBox(tr("Normalize loudness"), clipForm_);
    f->addRow(QString(), kNormalize_);
    kLoudness_ = new QLabel(clipForm_);
    kLoudness_->setStyleSheet(QStringLiteral("color: #999;"));
    f->addRow(QString(), kLoudness_);

    connect(kName_, &QLineEdit::editingFinished, this, &CuePlayerPage::applyClipForm);
    for (auto* s : {kIn_, kOut_, kSpeed_, kVolume_}) connect(s, &QDoubleSpinBox::valueChanged, this, &CuePlayerPage::applyClipForm);
    connect(kEnd_, &QComboBox::activated, this, &CuePlayerPage::applyClipForm);
    connect(kNormalize_, &QCheckBox::toggled, this, &CuePlayerPage::applyClipForm);
    editWidgets_ << clipForm_;

    scroll->setWidget(clipForm_);
    stack->addWidget(scroll);
    return stack;
}

void CuePlayerPage::loadForms() {
    loading_ = true;
    const Composition* c = comp();
    if (c) {
        const QSignalBlocker b1(cName_), b2(cWidth_), b3(cHeight_), b4(cVolume_), b5(compOpacity_);
        if (!cName_->hasFocus()) cName_->setText(qs(c->name));
        cWidth_->setValue(c->width);
        cHeight_->setValue(c->height);
        cVolume_->setValue(c->volumeDb);
        compOpacity_->setValue(static_cast<int>(std::lround(c->opacity * 100.0)));
        compOpacityLabel_->setText(QStringLiteral("%1%").arg(compOpacity_->value()));
    }

    auto* lStack = static_cast<QStackedWidget*>(lEmpty_->parentWidget());
    if (const CompLayer* l = layer(selLayer_)) {
        lStack->setCurrentIndex(1);
        const QSignalBlocker b0(lName_), b1(lOpacity_), b2(lBlend_), b3(lTransition_), b4(lVolume_), b5(lFit_), b6(lPosX_),
            b7(lPosY_), b8(lScale_), b9(lRotation_), b10(lFlipH_), b11(lFlipV_), b12(lVideo_), b13(lAudio_), b14(lSolo_),
            b15(lBypass_);
        if (!lName_->hasFocus()) lName_->setText(qs(l->name));
        lOpacity_->setValue(static_cast<int>(std::lround(l->props.opacity * 100.0)));
        lOpacityLabel_->setText(QStringLiteral("%1%").arg(lOpacity_->value()));
        lBlend_->setCurrentIndex(std::max(0, lBlend_->findData(static_cast<int>(l->props.blend))));
        lTransition_->setValue(l->transition);
        lVolume_->setValue(l->volumeDb);
        lFit_->setCurrentIndex(std::max(0, lFit_->findData(static_cast<int>(l->props.fit))));
        lPosX_->setValue(l->props.posX);
        lPosY_->setValue(l->props.posY);
        lScale_->setValue(l->props.scale * 100.0);
        lRotation_->setValue(l->props.rotation);
        lFlipH_->setChecked(l->props.flipH);
        lFlipV_->setChecked(l->props.flipV);
        lVideo_->setChecked(l->video);
        lAudio_->setChecked(l->audio);
        lSolo_->setChecked(l->solo);
        lBypass_->setChecked(l->bypass);
    } else {
        lStack->setCurrentIndex(0);
    }

    auto* kStack = static_cast<QStackedWidget*>(kEmpty_->parentWidget());
    if (const Clip* k = clip(selLayer_, selColumn_)) {
        kStack->setCurrentIndex(1);
        const QString path = qs(k->path);
        const MediaInfo info = media_->info(path);
        const QSignalBlocker b0(kName_), b1(kIn_), b2(kOut_), b3(kSpeed_), b4(kEnd_), b5(kVolume_), b6(kNormalize_);
        if (!kName_->hasFocus()) kName_->setText(qs(k->name));
        kFile_->setText(QFileInfo(path).fileName());
        kFile_->setToolTip(path);
        if (!info.ready) kInfo_->setText(tr("Analysing…"));
        else if (!info.ok) kInfo_->setText(tr("Cannot read: %1").arg(info.error));
        else if (info.still) kInfo_->setText(tr("Image %1×%2").arg(info.width).arg(info.height));
        else
            kInfo_->setText(tr("%1×%2 · %3 fps · %4").arg(info.width).arg(info.height).arg(info.fps, 0, 'f', 2).arg(timeText(info.duration)));
        if (info.duration > 0.0) {
            kIn_->setMaximum(info.duration);
            kOut_->setMaximum(info.duration);
        }
        kIn_->setValue(k->inPoint);
        kOut_->setValue(k->outPoint);
        kSpeed_->setValue(k->speed * 100.0);
        kEnd_->setCurrentIndex(std::max(0, kEnd_->findData(static_cast<int>(k->end))));
        kVolume_->setValue(k->volumeDb);
        kNormalize_->setChecked(k->normalize);
        if (k->loudness) {
            kLoudness_->setText(tr("Measured: %1 LUFS, peak %2 dBFS")
                                    .arg(*k->loudness, 0, 'f', 1)
                                    .arg(k->peakDb ? QString::number(*k->peakDb, 'f', 1) : QStringLiteral("?")));
        } else {
            kLoudness_->setText(media_->audio(path).ready ? tr("No audio") : tr("Measuring loudness…"));
        }
    } else {
        kStack->setCurrentIndex(0);
    }
    for (auto* w : editWidgets_) w->setEnabled(editable_);
    loading_ = false;
}

void CuePlayerPage::applyCompForm() {
    Composition* c = comp();
    if (loading_ || !c || !editable_) return;
    const std::string name = ss(cName_->text().trimmed());
    const bool renamed = !name.empty() && name != c->name;
    if (renamed) c->name = name;
    c->width = cWidth_->value();
    c->height = cHeight_->value();
    c->volumeDb = cVolume_->value();
    sendCompLive();
    if (renamed) rebuildCompCombo();
    updateMonitors();
    edited();
}

void CuePlayerPage::applyLayerForm() {
    CompLayer* l = layer(selLayer_);
    if (loading_ || !l) return;
    // Opacidade, som e V/A/S/B são controlos ao vivo (também em Show Mode).
    l->props.opacity = lOpacity_->value() / 100.0;
    l->volumeDb = lVolume_->value();
    l->video = lVideo_->isChecked();
    l->audio = lAudio_->isChecked();
    l->solo = lSolo_->isChecked();
    l->bypass = lBypass_->isChecked();
    if (editable_) {
        const std::string name = ss(lName_->text().trimmed());
        if (!name.empty()) l->name = name;
        l->props.blend = static_cast<BlendMode>(lBlend_->currentData().toInt());
        l->transition = lTransition_->value();
        l->props.fit = static_cast<FitMode>(lFit_->currentData().toInt());
        l->props.posX = lPosX_->value();
        l->props.posY = lPosY_->value();
        l->props.scale = lScale_->value() / 100.0;
        l->props.rotation = lRotation_->value();
        l->props.flipH = lFlipH_->isChecked();
        l->props.flipV = lFlipV_->isChecked();
    }
    l->clamp();
    sendLayerLive(*l);
    edited();
}

void CuePlayerPage::applyClipForm() {
    Clip* k = clip(selLayer_, selColumn_);
    if (loading_ || !k || !editable_) return;
    k->name = ss(kName_->text().trimmed());
    k->inPoint = kIn_->value();
    k->outPoint = kOut_->value();
    k->speed = kSpeed_->value() / 100.0;
    k->end = static_cast<ClipEnd>(kEnd_->currentData().toInt());
    k->volumeDb = kVolume_->value();
    k->normalize = kNormalize_->isChecked();
    k->clamp();
    updateMonitors();
    edited();
}

void CuePlayerPage::sendLayerLive(const CompLayer& l) {
    protocol::Command c;
    c.type = protocol::CommandType::SetCompLayer;
    c.comp = ss(compId_);
    c.json = compLayerToJson(l);
    engine_->sendLatest(QStringLiteral("layer:") + compId_ + QLatin1Char(':') + qs(l.id), c);
}

void CuePlayerPage::sendCompLive() {
    const Composition* cc = comp();
    if (!cc) return;
    protocol::Command c;
    c.type = protocol::CommandType::SetCompProps;
    c.comp = cc->id;
    c.opacity = cc->opacity;
    c.volumeDb = cc->volumeDb;
    engine_->sendLatest(QStringLiteral("comp:") + compId_, c);
}

void CuePlayerPage::setEditable(bool editable) {
    editable_ = editable;
    grid_->setEditable(editable);
    // Na layer, só os controlos ao vivo continuam activos em Show Mode.
    for (auto* w : std::initializer_list<QWidget*>{lBlend_, lTransition_, lFit_, lPosX_, lPosY_, lScale_, lRotation_, lFlipH_, lFlipV_,
                                                   cVolume_})
        w->setEnabled(editable);
    loadForms();
}
