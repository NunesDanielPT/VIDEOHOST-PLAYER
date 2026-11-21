#include "main_window.h"

#include "vthost/core/paths.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

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

    // Alterações ao projecto chegam ao motor agrupadas (no máximo ~3x/s).
    pushTimer_.setSingleShot(true);
    pushTimer_.setInterval(300);
    connect(&pushTimer_, &QTimer::timeout, this, &MainWindow::pushToEngine);

    buildUi();
    buildMenus();

    project_ = Project::makeDefault();
    project_.name = ss(tr("Untitled"));
    refreshAll();
    resize(1280, 760);
}

// ------------------------------------------------------------------ UI

void MainWindow::buildUi() {
    // Biblioteca de media (à esquerda)
    auto* libDock = new QDockWidget(tr("Media library"), this);
    libDock->setObjectName(QStringLiteral("library"));
    libDock->setFeatures(QDockWidget::DockWidgetMovable);
    auto* libWidget = new QWidget(libDock);
    auto* libLayout = new QVBoxLayout(libWidget);
    mediaList_ = new QListWidget(libWidget);
    mediaList_->setSelectionMode(QAbstractItemView::SingleSelection);
    auto* addMediaBtn = new QPushButton(tr("Add media..."), libWidget);
    auto* removeMediaBtn = new QPushButton(tr("Remove"), libWidget);
    connect(addMediaBtn, &QPushButton::clicked, this, &MainWindow::addMedia);
    connect(removeMediaBtn, &QPushButton::clicked, this, &MainWindow::removeMedia);
    libLayout->addWidget(mediaList_);
    auto* libButtons = new QHBoxLayout;
    libButtons->addWidget(addMediaBtn);
    libButtons->addWidget(removeMediaBtn);
    libLayout->addLayout(libButtons);
    libDock->setWidget(libWidget);
    addDockWidget(Qt::LeftDockWidgetArea, libDock);
    editWidgets_ << addMediaBtn << removeMediaBtn;

    // Grelha layers x colunas (centro)
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    grid_ = new QTableWidget(central);
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    grid_->horizontalHeader()->setDefaultSectionSize(140);
    grid_->verticalHeader()->setDefaultSectionSize(56);
    grid_->horizontalHeader()->setSectionsClickable(true);
    grid_->setToolTip(tr("Double-click a cell to place the selected media. Click a column header to GO."));
    connect(grid_, &QTableWidget::cellDoubleClicked, this, &MainWindow::assignSelectedMedia);
    connect(grid_, &QTableWidget::customContextMenuRequested, this, &MainWindow::showCellMenu);
    connect(grid_->horizontalHeader(), &QHeaderView::sectionClicked, this, &MainWindow::go);
    connect(grid_, &QTableWidget::itemSelectionChanged, this, &MainWindow::onLayerSelected);
    layout->addWidget(grid_, 1);

    auto* controls = new QHBoxLayout;
    goButton_ = new QPushButton(tr("GO next  [Space]"), central);
    goButton_->setMinimumHeight(48);
    goButton_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
    stopButton_ = new QPushButton(tr("Stop all"), central);
    stopButton_->setMinimumHeight(48);
    connect(goButton_, &QPushButton::clicked, this, &MainWindow::goNext);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopAll);

    opacityLabel_ = new QLabel(tr("Layer opacity"), central);
    opacitySlider_ = new QSlider(Qt::Horizontal, central);
    opacitySlider_->setRange(0, 100);
    opacitySlider_->setEnabled(false);
    connect(opacitySlider_, &QSlider::valueChanged, this, &MainWindow::onOpacityChanged);

    controls->addWidget(goButton_, 2);
    controls->addWidget(stopButton_, 1);
    controls->addSpacing(24);
    controls->addWidget(opacityLabel_);
    controls->addWidget(opacitySlider_, 2);
    layout->addLayout(controls);
    setCentralWidget(central);

    engineLabel_ = new QLabel(this);
    infoLabel_ = new QLabel(this);
    statusBar()->addWidget(engineLabel_);
    statusBar()->addWidget(infoLabel_, 1);
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* aNew = file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newProject);
    auto* aOpen = file->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openProject);
    file->addAction(tr("&Save"), QKeySequence::Save, this, [this] { saveProject(); });
    file->addAction(tr("Save &as..."), QKeySequence::SaveAs, this, [this] { saveProjectAs(); });
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    editActions_ << aNew << aOpen;

    auto* edit = menuBar()->addMenu(tr("&Edit"));
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

    auto* lang = menuBar()->addMenu(tr("&Language"));
    auto* group = new QActionGroup(this);
    const QString current = QSettings().value(QStringLiteral("language")).toString();
    auto* pt = lang->addAction(QStringLiteral("Português"), this, [this] { setLanguage(QStringLiteral("pt_PT")); });
    auto* en = lang->addAction(QStringLiteral("English"), this, [this] { setLanguage(QStringLiteral("en")); });
    pt->setCheckable(true);
    en->setCheckable(true);
    group->addAction(pt);
    group->addAction(en);
    (current == QStringLiteral("en") ? en : pt)->setChecked(true);

    auto* tb = addToolBar(tr("Show"));
    tb->setObjectName(QStringLiteral("toolbar"));
    tb->addAction(showModeAction_);
}

// ------------------------------------------------------------------ projecto

QString MainWindow::livePath() const {
    // O motor lê sempre esta cópia "ao vivo"; o ficheiro do utilizador só muda ao Guardar.
    return QString::fromStdWString((paths::dataDir() / "live" / "live.vthost").wstring());
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
    projectPath_ = path;
    dirty_ = false;
    if (!path.isEmpty()) QSettings().setValue(QStringLiteral("lastProject"), path);
    refreshAll();
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
    refreshMediaList();
    markChanged();
}

void MainWindow::removeMedia() {
    auto* item = mediaList_->currentItem();
    if (!item) return;
    const std::string id = ss(item->data(Qt::UserRole).toString());
    std::erase_if(project_.media, [&](const MediaItem& m) { return m.id == id; });
    for (auto& c : project_.columns) std::erase_if(c.cells, [&](const auto& kv) { return kv.second == id; });
    refreshAll();
    markChanged();
}

void MainWindow::addLayer() {
    Layer l;
    l.id = ss(nextId(QStringLiteral("L")));
    l.name = ss(tr("Layer %1").arg(project_.layers.size() + 1));
    project_.layers.push_back(std::move(l));
    refreshGrid();
    markChanged();
}

void MainWindow::removeLayer() {
    const int row = grid_->currentRow();
    if (row < 0 || row >= static_cast<int>(project_.layers.size())) return;
    const std::string id = project_.layers[static_cast<std::size_t>(row)].id;
    project_.layers.erase(project_.layers.begin() + row);
    for (auto& c : project_.columns) c.cells.erase(id);
    refreshGrid();
    markChanged();
}

void MainWindow::renameLayer() {
    const int row = grid_->currentRow();
    if (row < 0 || row >= static_cast<int>(project_.layers.size())) return;
    auto& l = project_.layers[static_cast<std::size_t>(row)];
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename layer"), tr("Name:"), QLineEdit::Normal, qs(l.name), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    l.name = ss(name.trimmed());
    refreshGrid();
    markChanged();
}

void MainWindow::addColumn() {
    Column c;
    c.name = ss(tr("Cue %1").arg(project_.columns.size() + 1));
    project_.columns.push_back(std::move(c));
    refreshGrid();
    markChanged();
}

void MainWindow::removeColumn() {
    const int col = grid_->currentColumn();
    if (col < 0 || col >= static_cast<int>(project_.columns.size())) return;
    project_.columns.erase(project_.columns.begin() + col);
    refreshGrid();
    markChanged();
}

void MainWindow::renameColumn() {
    const int col = grid_->currentColumn();
    if (col < 0 || col >= static_cast<int>(project_.columns.size())) return;
    auto& c = project_.columns[static_cast<std::size_t>(col)];
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename column"), tr("Name:"), QLineEdit::Normal, qs(c.name), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    c.name = ss(name.trimmed());
    refreshGrid();
    markChanged();
}

void MainWindow::setColumnFade() {
    const int col = grid_->currentColumn();
    if (col < 0 || col >= static_cast<int>(project_.columns.size())) return;
    auto& c = project_.columns[static_cast<std::size_t>(col)];
    bool ok = false;
    const double v = QInputDialog::getDouble(this, tr("Column fade time"), tr("Fade (seconds):"), c.fadeSeconds, 0.0,
                                             60.0, 2, &ok);
    if (!ok) return;
    c.fadeSeconds = v;
    refreshGrid();
    markChanged();
}

void MainWindow::assignSelectedMedia(int row, int column) {
    if (showMode_) return;
    if (row < 0 || column < 0 || row >= static_cast<int>(project_.layers.size()) ||
        column >= static_cast<int>(project_.columns.size()))
        return;
    auto* item = mediaList_->currentItem();
    if (!item) {
        infoLabel_->setText(tr("Select a media in the library first."));
        return;
    }
    project_.columns[static_cast<std::size_t>(column)].cells[project_.layers[static_cast<std::size_t>(row)].id] =
        ss(item->data(Qt::UserRole).toString());
    refreshGrid();
    markChanged();
}

void MainWindow::clearCell(int row, int column) {
    if (row < 0 || column < 0 || row >= static_cast<int>(project_.layers.size()) ||
        column >= static_cast<int>(project_.columns.size()))
        return;
    project_.columns[static_cast<std::size_t>(column)].cells.erase(project_.layers[static_cast<std::size_t>(row)].id);
    refreshGrid();
    markChanged();
}

void MainWindow::showCellMenu(const QPoint& pos) {
    if (showMode_) return;
    const QModelIndex idx = grid_->indexAt(pos);
    if (!idx.isValid()) return;
    QMenu menu(this);
    menu.addAction(tr("Place selected media"), this, [this, idx] { assignSelectedMedia(idx.row(), idx.column()); });
    menu.addAction(tr("Clear cell"), this, [this, idx] { clearCell(idx.row(), idx.column()); });
    menu.exec(grid_->viewport()->mapToGlobal(pos));
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
    updateTitle();
    markChanged();
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
    refreshGrid();
}

void MainWindow::goNext() {
    if (project_.columns.empty()) return;
    const int next = (activeColumn_ + 1) % static_cast<int>(project_.columns.size());
    go(next);
}

void MainWindow::stopAll() {
    protocol::Command c;
    c.type = protocol::CommandType::StopAll;
    c.value = 1.0;
    engine_->send(c);
    activeColumn_ = -1;
    refreshGrid();
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

void MainWindow::onLayerSelected() {
    const int row = grid_->currentRow();
    const bool valid = row >= 0 && row < static_cast<int>(project_.layers.size());
    opacitySlider_->setEnabled(valid);
    if (!valid) return;
    const QSignalBlocker block(opacitySlider_);
    opacitySlider_->setValue(static_cast<int>(project_.layers[static_cast<std::size_t>(row)].opacity * 100.0 + 0.5));
    opacityLabel_->setText(tr("Opacity: %1").arg(qs(project_.layers[static_cast<std::size_t>(row)].name)));
}

void MainWindow::onOpacityChanged(int value) {
    const int row = grid_->currentRow();
    if (row < 0 || row >= static_cast<int>(project_.layers.size())) return;
    auto& l = project_.layers[static_cast<std::size_t>(row)];
    l.opacity = value / 100.0;
    // Controlo ao vivo: vai já para o motor (também em Show Mode).
    protocol::Command c;
    c.type = protocol::CommandType::SetLayerOpacity;
    c.layer = l.id;
    c.value = l.opacity;
    engine_->send(c);
    dirty_ = true;
    updateTitle();
    if (!showMode_) pushTimer_.start();
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
    engineLabel_->setStyleSheet(connected ? QStringLiteral("color: #3a3;") : QStringLiteral("color: #c33;"));
    if (connected && !adoptedEngineProject_) {
        // Decide no primeiro status: adoptar o show que já está a correr ou enviar o nosso.
        return;
    }
    if (connected) pushTimer_.start();
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
                projectPath_ = QSettings().value(QStringLiteral("lastProject")).toString();
                activeColumn_ = s.activeColumn;
                showMode_ = s.showMode;
                showModeAction_->setChecked(showMode_);
                refreshAll();
                updateEditingEnabled();
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
        refreshGrid();
    }

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

void MainWindow::setLanguage(const QString& code) {
    QSettings().setValue(QStringLiteral("language"), code);
    QMessageBox::information(this, tr("Language"), tr("The language will change the next time VTHOST Studio starts."));
}

void MainWindow::refreshAll() {
    refreshMediaList();
    refreshGrid();
    updateTitle();
    updateEditingEnabled();
}

void MainWindow::refreshMediaList() {
    const QString selected = mediaList_->currentItem() ? mediaList_->currentItem()->data(Qt::UserRole).toString() : QString();
    mediaList_->clear();
    for (const auto& m : project_.media) {
        const QFileInfo fi(qs(m.path));
        auto* item = new QListWidgetItem(fi.fileName(), mediaList_);
        item->setData(Qt::UserRole, qs(m.id));
        item->setToolTip(QDir::toNativeSeparators(qs(m.path)));
        if (!fi.exists()) {
            item->setForeground(Qt::red);
            item->setToolTip(tr("File not found: %1").arg(QDir::toNativeSeparators(qs(m.path))));
        }
        if (qs(m.id) == selected) mediaList_->setCurrentItem(item);
    }
}

void MainWindow::refreshGrid() {
    const QSignalBlocker block(grid_);
    const int curRow = grid_->currentRow();
    const int curCol = grid_->currentColumn();
    grid_->clear();
    grid_->setRowCount(static_cast<int>(project_.layers.size()));
    grid_->setColumnCount(static_cast<int>(project_.columns.size()));

    for (int c = 0; c < static_cast<int>(project_.columns.size()); ++c) {
        const auto& col = project_.columns[static_cast<std::size_t>(c)];
        auto* h = new QTableWidgetItem(QStringLiteral("%1\n%2 s").arg(qs(col.name)).arg(col.fadeSeconds, 0, 'f', 1));
        if (c == activeColumn_) {
            h->setBackground(kActiveColumn);
            h->setForeground(Qt::black);
        }
        grid_->setHorizontalHeaderItem(c, h);
    }
    for (int r = 0; r < static_cast<int>(project_.layers.size()); ++r) {
        const auto& layer = project_.layers[static_cast<std::size_t>(r)];
        grid_->setVerticalHeaderItem(r, new QTableWidgetItem(qs(layer.name)));
        for (int c = 0; c < static_cast<int>(project_.columns.size()); ++c) {
            const auto& cells = project_.columns[static_cast<std::size_t>(c)].cells;
            auto* item = new QTableWidgetItem;
            auto it = cells.find(layer.id);
            if (it != cells.end()) {
                if (const MediaItem* m = project_.findMedia(it->second)) {
                    item->setText(QFileInfo(qs(m->path)).completeBaseName());
                    item->setToolTip(QDir::toNativeSeparators(qs(m->path)));
                }
            }
            if (c == activeColumn_) item->setBackground(kActiveColumn.darker(250));
            grid_->setItem(r, c, item);
        }
    }
    if (curRow >= 0 && curCol >= 0 && curRow < grid_->rowCount() && curCol < grid_->columnCount())
        grid_->setCurrentCell(curRow, curCol);
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
    for (auto* w : editWidgets_) w->setEnabled(!showMode_);
    for (auto* a : editActions_) a->setEnabled(!showMode_);
}
