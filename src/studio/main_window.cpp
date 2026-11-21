#include "main_window.h"

#include "app_info.h"
#include "dashboard_table.h"
#include "library_panel.h"
#include "media_cache.h"
#include "preview_widget.h"
#include "properties_panel.h"
#include "settings_dialog.h"
#include "timeline_panel.h"
#include "vthost/core/paths.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

using namespace vthost;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }

const QColor kActiveColumn(255, 136, 0);

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    engine_ = new EngineClient(this);
    connect(engine_, &EngineClient::connectionChanged, this, &MainWindow::onEngineConnection);
    connect(engine_, &EngineClient::statusUpdated, this, &MainWindow::onEngineStatus);

    media_ = new MediaCache(this);
    connect(media_, &MediaCache::updated, this, [this](const QString&) {
        refreshDashboard();
        updateSelectionViews();
    });

    updater_ = new UpdateChecker(this);
    connect(updater_, &UpdateChecker::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(updater_, &UpdateChecker::upToDate, this, [this](const QString& latest) {
        if (!updateCheckIsManual_) return;
        QMessageBox::information(this, tr("Updates"),
                                 latest.isEmpty() ? tr("No releases have been published yet.")
                                                  : tr("VTHOST is up to date (version %1).").arg(appinfo::kVersion));
    });
    connect(updater_, &UpdateChecker::checkFailed, this, [this](const QString& err) {
        if (updateCheckIsManual_) QMessageBox::warning(this, tr("Updates"), err);
    });

    // Alterações ao projecto chegam ao motor agrupadas (no máximo ~3x/s).
    pushTimer_.setSingleShot(true);
    pushTimer_.setInterval(300);
    connect(&pushTimer_, &QTimer::timeout, this, &MainWindow::pushToEngine);

    buildUi();
    buildMenus();

    project_ = Project::makeDefault();
    project_.name = ss(tr("Untitled"));
    ensureTimeline();
    refreshAll();
    selectLayer(0);

    resize(1440, 900);
    const QSettings st;
    restoreGeometry(st.value(QStringLiteral("ui/geometry")).toByteArray());
    mainSplit_->restoreState(st.value(QStringLiteral("ui/mainSplit")).toByteArray());
    topSplit_->restoreState(st.value(QStringLiteral("ui/topSplit")).toByteArray());

    // Verificação automática pouco depois de arrancar (sem incomodar se falhar).
    if (SettingsDialog::checkUpdatesOnStartup())
        QTimer::singleShot(4000, this, [this] { checkForUpdates(false); });
}

// ------------------------------------------------------------------ UI

void MainWindow::buildUi() {
    library_ = new LibraryPanel(media_, this);
    connect(library_, &LibraryPanel::addRequested, this, &MainWindow::addMedia);
    connect(library_, &LibraryPanel::removeRequested, this, &MainWindow::removeMedia);

    preview_ = new PreviewWidget(this);
    connect(preview_, &PreviewWidget::layerDragged, this, &MainWindow::onLayerDragged);

    properties_ = new PropertiesPanel(this);
    connect(properties_, &PropertiesPanel::propsChanged, this, &MainWindow::onPropsChanged);

    topSplit_ = new QSplitter(Qt::Horizontal, this);
    topSplit_->addWidget(library_);
    topSplit_->addWidget(preview_);
    topSplit_->addWidget(properties_);
    topSplit_->setStretchFactor(0, 0);
    topSplit_->setStretchFactor(1, 1);
    topSplit_->setStretchFactor(2, 0);
    topSplit_->setSizes({260, 860, 320});

    // ---- Dashboard
    auto* dashPage = new QWidget(this);
    auto* dashLayout = new QVBoxLayout(dashPage);
    dashLayout->setContentsMargins(0, 4, 0, 0);
    auto* dashBar = new QHBoxLayout;
    dashBar->setContentsMargins(6, 0, 6, 0);
    auto* goBtn = new QPushButton(tr("GO next  [Space]"), dashPage);
    goBtn->setMinimumHeight(34);
    goBtn->setStyleSheet(QStringLiteral("font-weight: bold; padding: 0 18px;"));
    auto* stopBtn = new QPushButton(tr("Stop all"), dashPage);
    stopBtn->setMinimumHeight(34);
    auto* addLayerBtn = new QPushButton(tr("+ Layer"), dashPage);
    auto* addColBtn = new QPushButton(tr("+ Column"), dashPage);
    connect(goBtn, &QPushButton::clicked, this, &MainWindow::goNext);
    connect(stopBtn, &QPushButton::clicked, this, &MainWindow::stopAll);
    connect(addLayerBtn, &QPushButton::clicked, this, &MainWindow::addLayer);
    connect(addColBtn, &QPushButton::clicked, this, &MainWindow::addColumn);
    dashBar->addWidget(goBtn);
    dashBar->addWidget(stopBtn);
    dashBar->addStretch();
    dashBar->addWidget(addLayerBtn);
    dashBar->addWidget(addColBtn);
    editWidgets_ << addLayerBtn << addColBtn;

    dashboard_ = new DashboardTable(dashPage);
    dashboard_->setToolTip(tr("Drag media from the Library onto a cell. Click a column header to GO."));
    connect(dashboard_, &DashboardTable::mediaDropped, this, &MainWindow::placeMedia);
    connect(dashboard_, &QTableWidget::cellDoubleClicked, this,
            [this](int r, int c) { placeMedia(r, c, library_->selectedMediaId()); });
    connect(dashboard_, &QTableWidget::customContextMenuRequested, this, &MainWindow::showCellMenu);
    connect(dashboard_->horizontalHeader(), &QHeaderView::sectionClicked, this, &MainWindow::go);
    connect(dashboard_->verticalHeader(), &QHeaderView::sectionClicked, this, &MainWindow::selectLayer);
    connect(dashboard_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0) selectLayer(row);
    });
    dashLayout->addLayout(dashBar);
    dashLayout->addWidget(dashboard_, 1);

    // ---- Timeline
    timelinePanel_ = new TimelinePanel(media_, this);
    TimelineWidget* tw = timelinePanel_->timeline();
    connect(timelinePanel_, &TimelinePanel::playRequested, this,
            [this] { sendTimeline(protocol::TimelineAction::Play, timelinePanel_->timeline()->playhead()); });
    connect(timelinePanel_, &TimelinePanel::pauseRequested, this,
            [this] { sendTimeline(protocol::TimelineAction::Pause, -1.0); });
    connect(timelinePanel_, &TimelinePanel::stopRequested, this, [this] {
        sendTimeline(protocol::TimelineAction::Stop, -1.0);
        timelinePanel_->setState(0.0, false, true);
    });
    connect(tw, &TimelineWidget::seekRequested, this,
            [this](double t, bool) { sendTimeline(protocol::TimelineAction::Seek, t); });
    connect(tw, &TimelineWidget::edited, this, &MainWindow::markChanged);
    connect(tw, &TimelineWidget::layerClicked, this, &MainWindow::selectLayer);

    bottomTabs_ = new QTabWidget(this);
    bottomTabs_->addTab(dashPage, tr("Dashboard"));
    bottomTabs_->addTab(timelinePanel_, tr("Timeline"));

    mainSplit_ = new QSplitter(Qt::Vertical, this);
    mainSplit_->addWidget(topSplit_);
    mainSplit_->addWidget(bottomTabs_);
    mainSplit_->setStretchFactor(0, 3);
    mainSplit_->setStretchFactor(1, 2);
    setCentralWidget(mainSplit_);

    engineLabel_ = new QLabel(this);
    infoLabel_ = new QLabel(this);
    statusBar()->addWidget(engineLabel_);
    statusBar()->addWidget(infoLabel_, 1);
    onEngineConnection(false);
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* aNew = file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newProject);
    auto* aOpen = file->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openProject);
    file->addAction(tr("&Save"), QKeySequence::Save, this, [this] { saveProject(); });
    file->addAction(tr("Save &as..."), QKeySequence::SaveAs, this, [this] { saveProjectAs(); });
    file->addSeparator();
    file->addAction(tr("Settings..."), QKeySequence::Preferences, this, [this] { openSettings(0); });
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    editActions_ << aNew << aOpen;

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    editActions_ << edit->addAction(tr("Add media..."), this, &MainWindow::addMedia);
    edit->addSeparator();
    editActions_ << edit->addAction(tr("Add layer"), this, &MainWindow::addLayer)
                 << edit->addAction(tr("Remove layer"), this, &MainWindow::removeLayer)
                 << edit->addAction(tr("Rename layer..."), this, &MainWindow::renameLayer);
    edit->addSeparator();
    editActions_ << edit->addAction(tr("Add column"), this, &MainWindow::addColumn)
                 << edit->addAction(tr("Remove column"), this, &MainWindow::removeColumn)
                 << edit->addAction(tr("Rename column..."), this, &MainWindow::renameColumn)
                 << edit->addAction(tr("Column fade time..."), this, &MainWindow::setColumnFade);
    edit->addSeparator();
    editActions_ << edit->addAction(tr("Canvas size..."), this, &MainWindow::setCanvasSize);

    auto* outputs = menuBar()->addMenu(tr("&Outputs"));
    editActions_ << outputs->addAction(tr("Add output..."), this, &MainWindow::addOutput)
                 << outputs->addAction(tr("Remove last output"), this, &MainWindow::removeOutput);

    auto* show = menuBar()->addMenu(tr("&Show"));
    auto* aGo = show->addAction(tr("GO next"), this, &MainWindow::goNext);
    aGo->setShortcut(Qt::Key_Space);
    aGo->setShortcutContext(Qt::ApplicationShortcut);
    show->addAction(tr("Stop all"), this, &MainWindow::stopAll);
    show->addSeparator();
    showModeAction_ = show->addAction(tr("Show Mode (lock editing)"));
    showModeAction_->setCheckable(true);
    showModeAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(showModeAction_, &QAction::triggered, this, &MainWindow::setShowMode);

    auto* eng = menuBar()->addMenu(tr("E&ngine"));
    eng->addAction(tr("Start engine"), this, &MainWindow::startEngine);
    auto* aStop = eng->addAction(tr("Stop engine"), this, &MainWindow::stopEngine);
    editActions_ << aStop;
    eng->addSeparator();
    eng->addAction(tr("Open logs folder"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(paths::logsDir().wstring())));
    });

    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check for updates..."), this, [this] { checkForUpdates(true); });
    help->addSeparator();
    help->addAction(tr("About VTHOST"), this, [this] { openSettings(1); });

    auto* tb = addToolBar(tr("Show"));
    tb->setObjectName(QStringLiteral("toolbar"));
    tb->setMovable(false);
    tb->addAction(showModeAction_);
    tb->addSeparator();
    tb->addAction(tr("Start engine"), this, &MainWindow::startEngine);
}

// ------------------------------------------------------------------ projecto

QString MainWindow::livePath() const {
    // O motor lê sempre esta cópia "ao vivo"; o ficheiro do utilizador só muda ao Guardar.
    return QString::fromStdWString((paths::dataDir() / "live" / "live.vthost").wstring());
}

void MainWindow::ensureTimeline() {
    if (project_.timelines.empty()) project_.timelines.push_back({"T1", "Timeline 1", {}, {}});
}

QString MainWindow::timelineId() const {
    return project_.timelines.empty() ? QString() : qs(project_.timelines.front().id);
}

void MainWindow::markChanged() {
    dirty_ = true;
    updateTitle();
    pushTimer_.start();
}

void MainWindow::pushToEngine() {
    std::string err;
    if (!saveProjectFile(project_, ss(livePath()), &err)) {
        infoLabel_->setText(tr("Could not write live project: %1").arg(qs(err)));
        return;
    }
    if (!engine_->isConnected()) return;
    protocol::Command c;
    c.type = protocol::CommandType::LoadProject;
    c.path = ss(livePath());
    engine_->send(c, [this](const protocol::ParsedResponse& r) {
        if (!r.ok) infoLabel_->setText(tr("Engine: %1").arg(qs(r.error)));
    });
}

void MainWindow::setProject(Project p, const QString& path) {
    project_ = std::move(p);
    ensureTimeline();
    projectPath_ = path;
    dirty_ = false;
    if (!path.isEmpty()) QSettings().setValue(QStringLiteral("lastProject"), path);
    refreshAll();
    selectLayer(project_.layers.empty() ? -1 : 0);
    pushTimer_.start();
}

bool MainWindow::confirmDiscard() {
    if (!dirty_) return true;
    const auto b = QMessageBox::question(this, tr("Unsaved changes"),
                                         tr("The project has unsaved changes. Save them?"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (b == QMessageBox::Save) return saveProject();
    return b == QMessageBox::Discard;
}

void MainWindow::newProject() {
    if (!confirmDiscard()) return;
    Project p = Project::makeDefault();
    p.name = ss(tr("Untitled"));
    setProject(std::move(p), {});
}

void MainWindow::openProject() {
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open project"), QString(),
                                                      tr("VTHOST projects (*.vthost)"));
    if (!path.isEmpty()) loadFrom(path);
}

void MainWindow::loadFrom(const QString& path) {
    auto r = loadProjectFile(ss(path));
    if (!r.project) {
        QMessageBox::warning(this, tr("Open project"), tr("Could not open the project:\n%1").arg(qs(r.error)));
        return;
    }
    if (!r.warnings.empty()) {
        QStringList w;
        for (const auto& s : r.warnings) w << qs(s);
        QMessageBox::information(this, tr("Open project"),
                                 tr("The project was opened with corrections:\n%1").arg(w.join('\n')));
    }
    setProject(std::move(*r.project), path);
}

bool MainWindow::saveProject() {
    if (projectPath_.isEmpty()) return saveProjectAs();
    std::string err;
    if (!saveProjectFile(project_, ss(projectPath_), &err)) {
        QMessageBox::critical(this, tr("Save"), tr("Could not save:\n%1").arg(qs(err)));
        return false;
    }
    dirty_ = false;
    QSettings().setValue(QStringLiteral("lastProject"), projectPath_);
    updateTitle();
    return true;
}

bool MainWindow::saveProjectAs() {
    QString path = QFileDialog::getSaveFileName(this, tr("Save project"), projectPath_,
                                                tr("VTHOST projects (*.vthost)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".vthost"), Qt::CaseInsensitive)) path += QStringLiteral(".vthost");
    projectPath_ = path;
    project_.name = ss(QFileInfo(path).completeBaseName());
    return saveProject();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    QSettings st;
    st.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    st.setValue(QStringLiteral("ui/mainSplit"), mainSplit_->saveState());
    st.setValue(QStringLiteral("ui/topSplit"), topSplit_->saveState());
    // O motor continua a correr: fechar a interface nunca pára o show.
    event->accept();
}

// ------------------------------------------------------------------ edição

QString MainWindow::nextId(const QString& prefix) const {
    for (int i = 1;; ++i) {
        const std::string id = ss(prefix + QString::number(i));
        bool used = project_.findMedia(id) || project_.findLayer(id);
        for (const auto& o : project_.outputs) used = used || o.id == id;
        if (!used) return qs(id);
    }
}

void MainWindow::addMedia() {
    if (showMode_) return;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add media"), QSettings().value(QStringLiteral("mediaDir")).toString(),
        tr("Video and images (*.mov *.mp4 *.m4v *.mkv *.avi *.mxf *.webm *.png *.jpg *.jpeg *.bmp *.tif *.tiff);;All files (*)"));
    if (files.isEmpty()) return;
    QSettings().setValue(QStringLiteral("mediaDir"), QFileInfo(files.first()).absolutePath());
    for (const QString& f : files) {
        MediaItem m;
        m.id = ss(nextId(QStringLiteral("m")));
        m.path = ss(QDir::fromNativeSeparators(QFileInfo(f).absoluteFilePath()));
        m.loop = true;
        project_.media.push_back(std::move(m));
    }
    library_->setMedia(project_.media);
    markChanged();
}

void MainWindow::removeMedia(const QString& mediaId) {
    if (showMode_) return;
    const std::string id = ss(mediaId);
    std::erase_if(project_.media, [&](const MediaItem& m) { return m.id == id; });
    for (auto& c : project_.columns) std::erase_if(c.cells, [&](const auto& kv) { return kv.second == id; });
    for (auto& t : project_.timelines) std::erase_if(t.clips, [&](const Clip& c) { return c.mediaId == id; });
    refreshAll();
    markChanged();
}

void MainWindow::addLayer() {
    if (showMode_) return;
    Layer l;
    l.id = ss(nextId(QStringLiteral("L")));
    l.name = ss(tr("Layer %1").arg(project_.layers.size() + 1));
    project_.layers.push_back(std::move(l));
    refreshAll();
    markChanged();
}

void MainWindow::removeLayer() {
    const int row = selectedLayer_;
    if (showMode_ || row < 0 || row >= static_cast<int>(project_.layers.size())) return;
    const std::string id = project_.layers[static_cast<std::size_t>(row)].id;
    project_.layers.erase(project_.layers.begin() + row);
    for (auto& c : project_.columns) c.cells.erase(id);
    for (auto& t : project_.timelines) std::erase_if(t.clips, [&](const Clip& c) { return c.layerId == id; });
    refreshAll();
    selectLayer(std::min(row, static_cast<int>(project_.layers.size()) - 1));
    markChanged();
}

void MainWindow::renameLayer() {
    const int row = selectedLayer_;
    if (row < 0 || row >= static_cast<int>(project_.layers.size())) return;
    auto& l = project_.layers[static_cast<std::size_t>(row)];
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename layer"), tr("Name:"), QLineEdit::Normal, qs(l.name), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    l.name = ss(name.trimmed());
    refreshAll();
    markChanged();
}

void MainWindow::addColumn() {
    if (showMode_) return;
    Column c;
    c.name = ss(tr("Cue %1").arg(project_.columns.size() + 1));
    project_.columns.push_back(std::move(c));
    refreshDashboard();
    markChanged();
}

void MainWindow::removeColumn() {
    const int col = dashboard_->currentColumn();
    if (col < 0 || col >= static_cast<int>(project_.columns.size())) return;
    project_.columns.erase(project_.columns.begin() + col);
    refreshDashboard();
    markChanged();
}

void MainWindow::renameColumn() {
    const int col = dashboard_->currentColumn();
    if (col < 0 || col >= static_cast<int>(project_.columns.size())) return;
    auto& c = project_.columns[static_cast<std::size_t>(col)];
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename column"), tr("Name:"), QLineEdit::Normal, qs(c.name), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    c.name = ss(name.trimmed());
    refreshDashboard();
    markChanged();
}

void MainWindow::setColumnFade() {
    const int col = dashboard_->currentColumn();
    if (col < 0 || col >= static_cast<int>(project_.columns.size())) return;
    auto& c = project_.columns[static_cast<std::size_t>(col)];
    bool ok = false;
    const double v = QInputDialog::getDouble(this, tr("Column fade time"), tr("Fade (seconds):"), c.fadeSeconds, 0.0,
                                             60.0, 2, &ok);
    if (!ok) return;
    c.fadeSeconds = v;
    refreshDashboard();
    markChanged();
}

void MainWindow::placeMedia(int row, int column, const QString& mediaId) {
    if (showMode_) return;
    if (row < 0 || column < 0 || row >= static_cast<int>(project_.layers.size()) ||
        column >= static_cast<int>(project_.columns.size()))
        return;
    if (mediaId.isEmpty() || !project_.findMedia(ss(mediaId))) {
        infoLabel_->setText(tr("Select a media in the library first."));
        return;
    }
    project_.columns[static_cast<std::size_t>(column)].cells[project_.layers[static_cast<std::size_t>(row)].id] =
        ss(mediaId);
    refreshDashboard();
    markChanged();
}

void MainWindow::clearCell(int row, int column) {
    if (row < 0 || column < 0 || row >= static_cast<int>(project_.layers.size()) ||
        column >= static_cast<int>(project_.columns.size()))
        return;
    project_.columns[static_cast<std::size_t>(column)].cells.erase(project_.layers[static_cast<std::size_t>(row)].id);
    refreshDashboard();
    markChanged();
}

void MainWindow::showCellMenu(const QPoint& pos) {
    if (showMode_) return;
    const QModelIndex idx = dashboard_->indexAt(pos);
    if (!idx.isValid()) return;
    QMenu menu(this);
    menu.addAction(tr("Place selected media"), this,
                   [this, idx] { placeMedia(idx.row(), idx.column(), library_->selectedMediaId()); });
    menu.addAction(tr("Clear cell"), this, [this, idx] { clearCell(idx.row(), idx.column()); });
    menu.exec(dashboard_->viewport()->mapToGlobal(pos));
}

void MainWindow::addOutput() {
    bool ok = false;
    const int monitor = QInputDialog::getInt(this, tr("Add output"),
                                             tr("Monitor number (0 = main monitor):"), 1, 0, 63, 1, &ok);
    if (!ok) return;
    Output o;
    o.id = ss(nextId(QStringLiteral("O")));
    o.name = ss(tr("Output %1").arg(project_.outputs.size() + 1));
    o.monitor = monitor;
    project_.outputs.push_back(std::move(o));
    markChanged();
}

void MainWindow::removeOutput() {
    if (project_.outputs.empty()) return;
    project_.outputs.pop_back();
    markChanged();
}

void MainWindow::setCanvasSize() {
    bool ok = false;
    const int w = QInputDialog::getInt(this, tr("Canvas size"), tr("Width (px):"), project_.canvasWidth, 16,
                                       kMaxCanvasSize, 1, &ok);
    if (!ok) return;
    const int h = QInputDialog::getInt(this, tr("Canvas size"), tr("Height (px):"), project_.canvasHeight, 16,
                                       kMaxCanvasSize, 1, &ok);
    if (!ok) return;
    project_.canvasWidth = w;
    project_.canvasHeight = h;
    preview_->setCanvasSize(w, h);
    updateTitle();
    markChanged();
}

// ------------------------------------------------------------------ seleção e propriedades

void MainWindow::selectLayer(int row) {
    if (row >= static_cast<int>(project_.layers.size())) row = static_cast<int>(project_.layers.size()) - 1;
    selectedLayer_ = row;
    if (row >= 0 && dashboard_->currentRow() != row) {
        const QSignalBlocker b(dashboard_);
        dashboard_->setCurrentCell(row, std::max(0, dashboard_->currentColumn()));
    }
    updateSelectionViews();
}

void MainWindow::updateSelectionViews() {
    const bool valid = selectedLayer_ >= 0 && selectedLayer_ < static_cast<int>(project_.layers.size());
    const Layer* layer = valid ? &project_.layers[static_cast<std::size_t>(selectedLayer_)] : nullptr;
    properties_->setLayer(layer);
    if (!layer) {
        preview_->setSelection(std::nullopt, 0, 0);
        return;
    }
    // Tamanho da media que está no ar nesta layer (para o contorno no Workspace).
    int mw = 0, mh = 0;
    if (engine_->hasStatus()) {
        for (const auto& ls : engine_->lastStatus().layers) {
            if (ls.id != layer->id || ls.mediaId.empty()) continue;
            if (const MediaItem* m = project_.findMedia(ls.mediaId)) {
                const MediaInfo info = media_->info(qs(m->path));
                mw = info.width;
                mh = info.height;
            }
        }
    }
    preview_->setSelection(layer->props, mw, mh);
}

void MainWindow::applyLayerProps(int row, const LayerProps& props, bool persist) {
    if (row < 0 || row >= static_cast<int>(project_.layers.size())) return;
    Layer& l = project_.layers[static_cast<std::size_t>(row)];
    l.props = props;
    l.props.clamp();
    // Ao vivo: vai já para o motor (também em Show Mode).
    protocol::Command c;
    c.type = protocol::CommandType::SetLayerProps;
    c.layer = l.id;
    c.props = l.props;
    engine_->send(c);
    if (persist) {
        dirty_ = true;
        updateTitle();
        if (!showMode_) pushTimer_.start();
    }
    updateSelectionViews();
}

void MainWindow::onPropsChanged(const LayerProps& props) { applyLayerProps(selectedLayer_, props, true); }

void MainWindow::onLayerDragged(double dx, double dy, bool finished) {
    if (selectedLayer_ < 0 || selectedLayer_ >= static_cast<int>(project_.layers.size())) return;
    if (!dragStartProps_) dragStartProps_ = project_.layers[static_cast<std::size_t>(selectedLayer_)].props;
    LayerProps p = *dragStartProps_;
    p.posX += dx;
    p.posY += dy;
    applyLayerProps(selectedLayer_, p, finished);
    if (finished) dragStartProps_.reset();
}

// ------------------------------------------------------------------ show

void MainWindow::go(int column) {
    if (column < 0 || column >= static_cast<int>(project_.columns.size())) return;
    // Garante que o motor tem a versão mais recente antes do GO.
    if (pushTimer_.isActive()) {
        pushTimer_.stop();
        pushToEngine();
    }
    protocol::Command c;
    c.type = protocol::CommandType::Go;
    c.column = column;
    engine_->send(c, [this](const protocol::ParsedResponse& r) {
        if (!r.ok) infoLabel_->setText(tr("GO failed: %1").arg(qs(r.error)));
    });
    activeColumn_ = column;
    refreshDashboard();
}

void MainWindow::goNext() {
    if (project_.columns.empty()) return;
    go((activeColumn_ + 1) % static_cast<int>(project_.columns.size()));
}

void MainWindow::stopAll() {
    protocol::Command c;
    c.type = protocol::CommandType::StopAll;
    c.value = 1.0;
    engine_->send(c);
    activeColumn_ = -1;
    refreshDashboard();
}

void MainWindow::setShowMode(bool on) {
    showMode_ = on;
    showModeAction_->setChecked(on);
    protocol::Command c;
    c.type = protocol::CommandType::SetShowMode;
    c.on = on;
    engine_->send(c);
    updateEditingEnabled();
    updateTitle();
}

// ------------------------------------------------------------------ timeline

void MainWindow::sendTimeline(protocol::TimelineAction action, double time) {
    const QString id = timelineId();
    if (id.isEmpty()) return;
    if (pushTimer_.isActive()) {
        pushTimer_.stop();
        pushToEngine();
    }
    protocol::Command c;
    c.type = protocol::CommandType::Timeline;
    c.action = action;
    c.timeline = ss(id);
    c.value = time;
    engine_->send(c, [this](const protocol::ParsedResponse& r) {
        if (!r.ok) infoLabel_->setText(tr("Timeline: %1").arg(qs(r.error)));
    });
    if (action == protocol::TimelineAction::Play || action == protocol::TimelineAction::Seek) {
        activeColumn_ = -1;
        refreshDashboard();
    }
}

// ------------------------------------------------------------------ motor

void MainWindow::startEngine() {
    pushToEngine(); // garante que a cópia ao vivo existe
    if (!engine_->startEngine(livePath()))
        QMessageBox::warning(this, tr("Engine"), tr("vthost-watchdog was not found next to the Studio."));
}

void MainWindow::stopEngine() {
    const auto b = QMessageBox::question(this, tr("Stop engine"),
                                         tr("Stop the engine? All outputs will go dark."));
    if (b != QMessageBox::Yes) return;
    protocol::Command c;
    c.type = protocol::CommandType::Shutdown;
    engine_->send(c, [this](const protocol::ParsedResponse& r) {
        if (!r.ok) QMessageBox::warning(this, tr("Engine"), qs(r.error));
    });
}

void MainWindow::onEngineConnection(bool connected) {
    engineLabel_->setText(connected ? tr("Engine: connected") : tr("Engine: offline"));
    engineLabel_->setStyleSheet(connected ? QStringLiteral("color: #3c3; padding: 0 8px;")
                                          : QStringLiteral("color: #e44; padding: 0 8px;"));
    // Ao ligar, decide no primeiro status: adoptar o show que já está a correr ou enviar o nosso.
    if (connected && adoptedEngineProject_) pushTimer_.start();
}

void MainWindow::onEngineStatus() {
    const auto& s = engine_->lastStatus();

    if (!adoptedEngineProject_) {
        adoptedEngineProject_ = true;
        // O Studio abriu (ou reabriu após um crash) com o motor já a tocar um show:
        // mostra esse show em vez de o substituir.
        if (!dirty_ && projectPath_.isEmpty() && !s.projectPath.empty()) {
            auto r = loadProjectFile(s.projectPath);
            if (r.project) {
                project_ = std::move(*r.project);
                ensureTimeline();
                projectPath_ = QSettings().value(QStringLiteral("lastProject")).toString();
                activeColumn_ = s.activeColumn;
                showMode_ = s.showMode;
                showModeAction_->setChecked(showMode_);
                refreshAll();
                selectLayer(project_.layers.empty() ? -1 : 0);
                infoLabel_->setText(tr("Connected to a running show."));
                return;
            }
        }
        pushTimer_.start();
    }

    if (s.showMode != showMode_) {
        showMode_ = s.showMode;
        showModeAction_->setChecked(showMode_);
        updateEditingEnabled();
        updateTitle();
    }
    if (s.activeColumn != activeColumn_) {
        activeColumn_ = s.activeColumn;
        refreshDashboard();
    }
    timelinePanel_->setState(s.timelineTime, s.timelinePlaying, qs(s.timelineId) == timelineId());
    preview_->setFps(s.fps);
    updateSelectionViews();

    QStringList problems;
    for (const auto& o : s.outputs)
        if (!o.ok || !o.error.empty()) problems << tr("Output %1: %2").arg(qs(o.id), qs(o.error));
    for (const auto& l : s.layers)
        if (l.state == "error") problems << tr("Layer %1: %2").arg(qs(l.id), qs(l.error));
    for (const auto& w : s.warnings) problems << qs(w);

    QString info = tr("%1 fps").arg(s.fps, 0, 'f', 1);
    if (s.recovered) info += QStringLiteral("  |  ") + tr("Engine was restarted and the show resumed");
    if (!problems.isEmpty()) info += QStringLiteral("  |  ⚠ ") + problems.join(QStringLiteral("; "));
    infoLabel_->setText(info);
}

// ------------------------------------------------------------------ vista

void MainWindow::refreshAll() {
    library_->setMedia(project_.media);
    refreshDashboard();
    timelinePanel_->timeline()->setProject(&project_, timelineId());
    preview_->setCanvasSize(project_.canvasWidth, project_.canvasHeight);
    updateSelectionViews();
    updateTitle();
    updateEditingEnabled();
}

void MainWindow::refreshDashboard() {
    const QSignalBlocker block(dashboard_);
    const int curRow = dashboard_->currentRow();
    const int curCol = dashboard_->currentColumn();
    dashboard_->clear();
    dashboard_->setRowCount(static_cast<int>(project_.layers.size()));
    dashboard_->setColumnCount(static_cast<int>(project_.columns.size()));

    for (int c = 0; c < static_cast<int>(project_.columns.size()); ++c) {
        const auto& col = project_.columns[static_cast<std::size_t>(c)];
        auto* h = new QTableWidgetItem(QStringLiteral("▶ %1  ·  %2 s").arg(qs(col.name)).arg(col.fadeSeconds, 0, 'f', 1));
        if (c == activeColumn_) {
            h->setBackground(kActiveColumn);
            h->setForeground(Qt::black);
        }
        dashboard_->setHorizontalHeaderItem(c, h);
    }
    for (int r = 0; r < static_cast<int>(project_.layers.size()); ++r) {
        const auto& layer = project_.layers[static_cast<std::size_t>(r)];
        dashboard_->setVerticalHeaderItem(r, new QTableWidgetItem(qs(layer.name)));
        for (int c = 0; c < static_cast<int>(project_.columns.size()); ++c) {
            const auto& cells = project_.columns[static_cast<std::size_t>(c)].cells;
            auto* item = new QTableWidgetItem;
            auto it = cells.find(layer.id);
            if (it != cells.end()) {
                if (const MediaItem* m = project_.findMedia(it->second)) {
                    const QString path = qs(m->path);
                    item->setText(QFileInfo(path).completeBaseName());
                    item->setToolTip(QDir::toNativeSeparators(path));
                    const MediaInfo info = media_->info(path);
                    if (info.ok && !info.thumb.isNull()) item->setIcon(QIcon(QPixmap::fromImage(info.thumb)));
                }
            }
            if (c == activeColumn_) item->setBackground(kActiveColumn.darker(220));
            dashboard_->setItem(r, c, item);
        }
    }
    if (curRow >= 0 && curCol >= 0 && curRow < dashboard_->rowCount() && curCol < dashboard_->columnCount())
        dashboard_->setCurrentCell(curRow, curCol);
}

void MainWindow::updateTitle() {
    QString t = QStringLiteral("VTHOST Studio - %1 (%2x%3)")
                    .arg(qs(project_.name))
                    .arg(project_.canvasWidth)
                    .arg(project_.canvasHeight);
    if (dirty_) t += QStringLiteral(" *");
    if (showMode_) t += QStringLiteral("  [") + tr("SHOW MODE") + QStringLiteral("]");
    setWindowTitle(t);
}

void MainWindow::updateEditingEnabled() {
    const bool edit = !showMode_;
    for (auto* w : editWidgets_) w->setEnabled(edit);
    for (auto* a : editActions_) a->setEnabled(edit);
    library_->setEditable(edit);
    dashboard_->setEditable(edit);
    timelinePanel_->setEditable(edit);
    // Properties e o arrastar no Workspace continuam ativos: são controlos ao vivo.
}

// ------------------------------------------------------------------ definições e atualizações

void MainWindow::openSettings(int tab) {
    SettingsDialog dlg(this, tab);
    connect(&dlg, &SettingsDialog::checkForUpdatesRequested, this, [this] { checkForUpdates(true); });
    dlg.exec();
}

void MainWindow::checkForUpdates(bool userInitiated) {
    updateCheckIsManual_ = userInitiated;
    updater_->check();
}

void MainWindow::onUpdateAvailable(const UpdateInfo& info) {
    if (!updateCheckIsManual_ && QSettings().value(QStringLiteral("updates/skipVersion")).toString() == info.version)
        return;

    QMessageBox box(this);
    box.setWindowTitle(tr("Update available"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("VTHOST %1 is available (you have %2).").arg(info.version, appinfo::kVersion));
    if (!info.notes.trimmed().isEmpty()) box.setDetailedText(info.notes);
    auto* now = box.addButton(tr("Update now"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    auto* skip = box.addButton(tr("Skip this version"), QMessageBox::DestructiveRole);
    box.exec();
    if (box.clickedButton() == skip) {
        QSettings().setValue(QStringLiteral("updates/skipVersion"), info.version);
        return;
    }
    if (box.clickedButton() != now) return;
    if (showMode_) {
        QMessageBox::warning(this, tr("Update"), tr("Turn off Show Mode before updating."));
        return;
    }
    downloadUpdate(info);
}

void MainWindow::downloadUpdate(const UpdateInfo& info) {
    auto* progress = new QProgressDialog(tr("Downloading VTHOST %1...").arg(info.version), tr("Cancel"), 0, 100, this);
    progress->setWindowTitle(tr("Update"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);

    auto conns = std::make_shared<QList<QMetaObject::Connection>>();
    auto cleanup = [progress, conns] {
        for (const auto& c : *conns) QObject::disconnect(c);
        progress->deleteLater();
    };
    *conns << connect(progress, &QProgressDialog::canceled, updater_, &UpdateChecker::cancelDownload);
    *conns << connect(updater_, &UpdateChecker::downloadProgress, progress, [progress](qint64 rec, qint64 total) {
        if (total > 0) progress->setValue(static_cast<int>(rec * 100 / total));
    });
    *conns << connect(updater_, &UpdateChecker::downloadFailed, this, [this, cleanup](const QString& err) {
        cleanup();
        QMessageBox::warning(this, tr("Update"), err);
    });
    *conns << connect(updater_, &UpdateChecker::downloadReady, this, [this, cleanup](const QString& path) {
        cleanup();
        installUpdate(path);
    });
    progress->show();
    updater_->download(info);
}

void MainWindow::installUpdate(const QString& installerPath) {
    if (showMode_) {
        QMessageBox::warning(this, tr("Update"), tr("Turn off Show Mode before updating."));
        return;
    }
    const auto b = QMessageBox::question(
        this, tr("Update"),
        tr("The update is ready. The engine will stop and the outputs will go dark while it installs. Install now?"));
    if (b != QMessageBox::Yes) return;
    if (!confirmDiscard()) return;
    dirty_ = false; // já tratado: não perguntar outra vez ao fechar

    auto launch = [this, installerPath] {
        const QStringList args = {QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"),
                                  QStringLiteral("/NORESTART"), QStringLiteral("/RELAUNCH=1")};
        if (!QProcess::startDetached(installerPath, args)) {
            QMessageBox::critical(this, tr("Update"), tr("Could not start the installer."));
            return;
        }
        qApp->quit();
    };

    if (!engine_->isConnected()) {
        launch();
        return;
    }
    // Pára o motor de forma limpa antes de substituir os ficheiros.
    protocol::Command c;
    c.type = protocol::CommandType::Shutdown;
    auto launched = std::make_shared<bool>(false);
    auto once = [launch, launched] {
        if (*launched) return;
        *launched = true;
        launch();
    };
    engine_->send(c, [once](const protocol::ParsedResponse&) { QTimer::singleShot(500, once); });
    QTimer::singleShot(4000, this, once); // se o motor não responder, o instalador trata dele
}
