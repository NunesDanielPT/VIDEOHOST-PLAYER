#include "main_window.h"

#include "app_info.h"
#include "countdown_server.h"
#include "countdown_view.h"
#include "inspector_panel.h"
#include "outputs_dialog.h"
#include "prefs.h"
#include "preview_widget.h"
#include "settings_dialog.h"
#include "update_dialog.h"
#include "waveform_view.h"
#include "vthost/core/paths.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
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
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace vthost;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }

constexpr std::size_t kMaxUndo = 100;
const QColor kBlue(42, 160, 255);

// Medidor de nível (L/R) da barra de transporte.
class LevelMeter : public QWidget {
public:
    explicit LevelMeter(QWidget* parent) : QWidget(parent) { setFixedSize(14, 34); }
    void setLevels(double l, double r) {
        l_ = std::clamp(l, 0.0, 1.0);
        r_ = std::clamp(r, 0.0, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(20, 20, 20));
        auto bar = [&](int x, double v) {
            // Escala em dB (-60..0) para o medidor mexer como os de som.
            const double db = v > 0.0 ? 20.0 * std::log10(v) : -60.0;
            const double f = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
            const int h = static_cast<int>(f * (height() - 2));
            const QColor c = db > -3.0 ? QColor(230, 60, 50) : db > -12.0 ? QColor(240, 190, 30) : QColor(70, 200, 80);
            p.fillRect(QRect(x, height() - 1 - h, 5, h), c);
        };
        bar(1, l_);
        bar(8, r_);
    }

private:
    double l_ = 0.0, r_ = 0.0;
};

QIcon cueIcon(CueIcon kind) {
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintCueIcon(&p, QRectF(4, 4, 40, 40), kind, true);
    return QIcon(pm);
}

// Ícones da barra de transporte, desenhados em cor clara (os do sistema ficam pretos no tema escuro).
enum class TIcon { Play, Pause, Stop, Prev, Next };
QIcon transportIcon(TIcon kind, const QColor& color) {
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    switch (kind) {
    case TIcon::Play:
        p.drawPolygon(QPolygonF({QPointF(18, 10), QPointF(54, 32), QPointF(18, 54)}));
        break;
    case TIcon::Pause:
        p.drawRoundedRect(QRectF(15, 10, 12, 44), 2, 2);
        p.drawRoundedRect(QRectF(37, 10, 12, 44), 2, 2);
        break;
    case TIcon::Stop:
        p.drawRoundedRect(QRectF(13, 13, 38, 38), 3, 3);
        break;
    case TIcon::Prev:
        p.drawRect(QRectF(10, 14, 6, 36));
        p.drawPolygon(QPolygonF({QPointF(36, 14), QPointF(16, 32), QPointF(36, 50)}));
        p.drawPolygon(QPolygonF({QPointF(56, 14), QPointF(36, 32), QPointF(56, 50)}));
        break;
    case TIcon::Next:
        p.drawRect(QRectF(48, 14, 6, 36));
        p.drawPolygon(QPolygonF({QPointF(8, 14), QPointF(28, 32), QPointF(8, 50)}));
        p.drawPolygon(QPolygonF({QPointF(28, 14), QPointF(48, 32), QPointF(28, 50)}));
        break;
    }
    return QIcon(pm);
}

const QColor kIconLight(225, 225, 225);

bool isImageFile(const QString& path) {
    static const QStringList ext = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                    QStringLiteral("bmp"), QStringLiteral("tif"), QStringLiteral("tiff"),
                                    QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("tga"),
                                    QStringLiteral("exr"), QStringLiteral("dpx")};
    return ext.contains(QFileInfo(path).suffix().toLower());
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    engine_ = new EngineClient(this);
    connect(engine_, &EngineClient::connectionChanged, this, &MainWindow::onEngineConnection);
    connect(engine_, &EngineClient::statusUpdated, this, &MainWindow::onEngineStatus);

    media_ = new MediaCache(this);
    connect(media_, &MediaCache::updated, this, [this](const QString&) {
        playlist_->refresh();
        updateSelection();
    });
    connect(media_, &MediaCache::audioAnalyzed, this, &MainWindow::applyAnalysis);

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
    connect(updater_, &UpdateChecker::downloadProgress, this, [this](qint64 rec, qint64 total) {
        if (updateDialog_) updateDialog_->setProgress(rec, total);
        if (total > 0 && availableUpdate_)
            setUpdateLabel(tr("Downloading VTHOST %1... %2%").arg(availableUpdate_->version).arg(rec * 100 / total));
    });
    connect(updater_, &UpdateChecker::downloadFailed, this, [this](const QString& err) {
        downloading_ = false;
        if (updateDialog_) updateDialog_->setFailed(err);
        if (availableUpdate_)
            setUpdateLabel(tr("Version %1 available — <a href=\"#\">see what's new</a>").arg(availableUpdate_->version));
    });
    connect(updater_, &UpdateChecker::downloadReady, this, [this](const QString& path) {
        downloading_ = false;
        readyInstaller_ = path;
        if (updateDialog_) updateDialog_->setReady();
        if (availableUpdate_)
            setUpdateLabel(tr("Version %1 ready — <a href=\"#\">restart and install</a>").arg(availableUpdate_->version));
    });
    // Enquanto o Studio está aberto, procura de 30 em 30 minutos (se a opção estiver ligada).
    updateTimer_.setInterval(30 * 60 * 1000);
    connect(&updateTimer_, &QTimer::timeout, this, [this] {
        if (SettingsDialog::checkUpdatesOnStartup() && !downloading_ && readyInstaller_.isEmpty()) checkForUpdates(false);
    });
    updateTimer_.start();

    // Alterações ao projecto chegam ao motor agrupadas (no máximo ~12x/s; os pedidos de
    // carregamento são coalescidos: só segue o mais recente).
    pushTimer_.setSingleShot(true);
    pushTimer_.setInterval(80);
    connect(&pushTimer_, &QTimer::timeout, this, &MainWindow::pushToEngine);

    // Countdown: o servidor web responde com o estado interpolado no momento do pedido.
    countdownServer_ = new CountdownServer(
        [this] {
            QByteArray j = QByteArray::fromStdString(countdownJson(currentCountdown(), project_.name));
            if (j.endsWith('}')) {
                j.chop(1);
                j += ",\"warn\":" + QByteArray::number(prefs::countdownWarnSeconds()) +
                     ",\"danger\":" + QByteArray::number(prefs::countdownDangerSeconds()) + "}";
            }
            return j;
        },
        this);
    countdownClock_.start();
    countdownTimer_.setInterval(100);
    connect(&countdownTimer_, &QTimer::timeout, this, &MainWindow::refreshCountdown);
    countdownTimer_.start();

    // Tempos no ecrã (preview, código de tempo, cabeça de leitura) a ~25 fps.
    uiTimer_.setInterval(40);
    connect(&uiTimer_, &QTimer::timeout, this, [this] {
        const Countdown c = currentCountdown();
        const int idx = playingCue_;
        const Cue* q = idx >= 0 && idx < static_cast<int>(project_.cues.size()) ? &project_.cues[static_cast<std::size_t>(idx)] : nullptr;
        const double fps = q ? std::max(1.0, media_->info(qs(q->path)).fps) : 25.0;
        if (c.active) {
            const QString el = formatTimecode(c.elapsed, fps);
            const QString rem = QStringLiteral("-") + formatTimecode(c.remaining, fps);
            QColor col = Qt::white;
            if (c.running && c.remaining <= prefs::countdownDangerSeconds()) col = QColor(255, 70, 60);
            else if (c.running && c.remaining <= prefs::countdownWarnSeconds()) col = QColor(255, 170, 0);
            preview_->setTimes(el, rem, col);
            timecode_->setText(el);
            timecode_->setStyleSheet(QStringLiteral("color: %1;").arg(c.running ? QStringLiteral("#ffffff") : QStringLiteral("#78b4ff")));
        } else {
            preview_->setTimes(QString(), QString(), Qt::white);
            timecode_->setText(QStringLiteral("--:--:--:--"));
            timecode_->setStyleSheet(QStringLiteral("color: #777;"));
        }
        // Cabeça de leitura na forma de onda (só se a cue selecionada for a que está no ar).
        const double head = q && selectedCue() == idx && c.active ? playingMediaTime() : -1.0;
        waveform_->setPlayhead(head);
        inspector_->setPlayhead(head);
        if (q && c.active && c.duration > 0.0) playlist_->setPlaying(idx, std::clamp(c.elapsed / c.duration, 0.0, 1.0), qs(c.state));
    });
    uiTimer_.start();

    buildUi();
    buildMenus();

    project_ = Project::makeDefault();
    project_.name = ss(tr("Untitled"));
    clearUndo();
    refreshPlaylist();

    resize(1440, 900);
    const QSettings st;
    restoreGeometry(st.value(QStringLiteral("ui/geometry")).toByteArray());
    mainSplit_->restoreState(st.value(QStringLiteral("ui/playerSplit")).toByteArray());
    centerSplit_->restoreState(st.value(QStringLiteral("ui/centerSplit")).toByteArray());

    applyPreferences();

    // Arranque: se o motor já estiver a tocar um show, o Studio adota-o (onEngineStatus).
    // Senão, abre o último projeto e, se pedido, arranca o motor.
    QTimer::singleShot(1500, this, [this] {
        if (adoptedEngineProject_ && !projectPath_.isEmpty()) return;
        const QString last = QSettings().value(QStringLiteral("lastProject")).toString();
        if (!engine_->hasStatus() && prefs::openLastProject() && !last.isEmpty() && QFileInfo::exists(last) && !dirty_)
            loadFrom(last);
        if (!engine_->isConnected() && prefs::autoStartEngine()) startEngine();
    });

    // Verificação automática pouco depois de arrancar (sem incomodar se falhar).
    if (SettingsDialog::checkUpdatesOnStartup())
        QTimer::singleShot(4000, this, [this] { checkForUpdates(false); });
}


// ------------------------------------------------------------------ UI

QWidget* MainWindow::buildTransportBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("transport"));
    bar->setStyleSheet(QStringLiteral("#transport { background: #1d1d1d; border-bottom: 1px solid #111; }"
                                      "QToolButton { border-radius: 4px; padding: 2px; }"
                                      "QToolButton:hover { background: #333; }"
                                      "QToolButton:checked { background: #1d3d5c; }"));
    auto* l = new QHBoxLayout(bar);
    l->setContentsMargins(8, 6, 8, 6);
    l->setSpacing(6);
    auto button = [&](const QIcon& icon, const QString& tip, int size, auto slot) {
        auto* b = new QToolButton(bar);
        b->setIcon(icon);
        b->setIconSize(QSize(size, size));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, slot);
        l->addWidget(b);
        return b;
    };
    playBtn_ = button(transportIcon(TIcon::Play, kBlue), tr("Play / pause (Space)"), 34, [this] { playPause(); });
    button(transportIcon(TIcon::Stop, kIconLight), tr("Stop (Esc)"), 22, [this] { stopPlayback(); });
    l->addSpacing(6);
    button(transportIcon(TIcon::Prev, kIconLight), tr("Previous cue (Ctrl+Left)"), 20, [this] { previousCue(); });
    button(transportIcon(TIcon::Next, kIconLight), tr("Next cue (Ctrl+Right)"), 20, [this] { nextCue(); });
    loopBtn_ = button(cueIcon(CueIcon::Loop), tr("Loop the playlist (Ctrl+R)"), 22, [this] { toggleLoopPlaylist(); });
    loopBtn_->setCheckable(true);
    l->addSpacing(10);
    for (int s : {30, 20, 10}) {
        auto* b = new QToolButton(bar);
        b->setText(QStringLiteral("GOTO\n%1").arg(s));
        b->setToolTip(tr("Jump to the last %1 seconds of the cue").arg(s));
        b->setStyleSheet(QStringLiteral("QToolButton { font-size: 9px; color: #aaa; min-width: 34px; }"));
        connect(b, &QToolButton::clicked, this, [this, s] { gotoRemaining(s); });
        l->addWidget(b);
    }
    l->addSpacing(10);
    auto* tcBox = new QWidget(bar);
    tcBox->setStyleSheet(QStringLiteral("background: #101010; border: 1px solid #333; border-radius: 4px;"));
    auto* tcl = new QVBoxLayout(tcBox);
    tcl->setContentsMargins(12, 2, 12, 2);
    tcl->setSpacing(0);
    timecode_ = new QLabel(QStringLiteral("--:--:--:--"), tcBox);
    QFont tf(QStringLiteral("Consolas"));
    tf.setStyleHint(QFont::Monospace);
    tf.setPixelSize(26);
    tf.setBold(true);
    timecode_->setFont(tf);
    timecode_->setStyleSheet(QStringLiteral("border: none; color: #777;"));
    cueLabel_ = new QLabel(tcBox);
    cueLabel_->setStyleSheet(QStringLiteral("border: none; color: #8aa; font-size: 10px;"));
    cueLabel_->setMaximumWidth(260);
    tcl->addWidget(timecode_);
    tcl->addWidget(cueLabel_);
    l->addWidget(tcBox);
    l->addStretch();

    // Volume geral + medidor.
    auto* volIcon = new QLabel(bar);
    volIcon->setPixmap(cueIcon(CueIcon::Audio).pixmap(18, 18));
    l->addWidget(volIcon);
    master_ = new QSlider(Qt::Horizontal, bar);
    master_->setRange(-600, 120);
    master_->setFixedWidth(130);
    master_->setToolTip(tr("Master volume"));
    masterLabel_ = new QLabel(QStringLiteral("0.0 dB"), bar);
    masterLabel_->setMinimumWidth(56);
    connect(master_, &QSlider::valueChanged, this, [this](int v) { setMasterVolume(v / 10.0); });
    l->addWidget(master_);
    l->addWidget(masterLabel_);
    auto* meter = new LevelMeter(bar);
    meter_ = meter;
    l->addWidget(meter);
    l->addSpacing(10);
    testBtn_ = new QToolButton(bar);
    testBtn_->setText(tr("Test card"));
    testBtn_->setCheckable(true);
    connect(testBtn_, &QToolButton::toggled, this, [this](bool on) {
        protocol::Command c;
        c.type = protocol::CommandType::SetTestCard;
        c.on = on;
        engine_->send(c);
    });
    l->addWidget(testBtn_);
    auto* outputsBtn = new QToolButton(bar);
    outputsBtn->setText(tr("Outputs..."));
    connect(outputsBtn, &QToolButton::clicked, this, &MainWindow::openOutputs);
    l->addWidget(outputsBtn);
    editWidgets_ << outputsBtn;
    return bar;
}

void MainWindow::buildUi() {
    playlist_ = new PlaylistView(media_, this);
    connect(playlist_, &QListWidget::currentRowChanged, this, [this](int) { updateSelection(); });
    connect(playlist_, &PlaylistView::playRequested, this, &MainWindow::playCue);
    connect(playlist_, &PlaylistView::iconClicked, this, &MainWindow::onIconClicked);
    connect(playlist_, &PlaylistView::filesDropped, this, &MainWindow::addFiles);
    connect(playlist_, &PlaylistView::cueMoved, this, &MainWindow::moveCue);
    connect(playlist_, &PlaylistView::deleteRequested, this, &MainWindow::deleteCue);
    connect(playlist_, &PlaylistView::contextMenuRequested, this, &MainWindow::showCueMenu);
    connect(playlist_, &PlaylistView::thumbnailRequested, this, [this](int) { thumbnailFromPlayhead(); });
    connect(playlist_, &PlaylistView::spacePressed, this, &MainWindow::playPause);

    preview_ = new PreviewWidget(this);
    connect(preview_, &PreviewWidget::doubleClicked, this, &MainWindow::openCountdownWindow);
    waveform_ = new WaveformView(this);
    connect(waveform_, &WaveformView::seekRequested, this, [this](double t, bool) { seekTo(t); });
    connect(waveform_, &WaveformView::inOutChanged, this, [this](double in, double out, bool finished) {
        const int i = selectedCue();
        if (i < 0 || showMode_) return;
        Cue& q = project_.cues[static_cast<std::size_t>(i)];
        q.inPoint = in;
        q.outPoint = out;
        q.clamp();
        playlist_->refresh();
        if (finished) {
            markChanged();
            updateSelection();
        }
    });
    inspector_ = new InspectorPanel(this);
    connect(inspector_, &InspectorPanel::cueChanged, this, &MainWindow::onCueEdited);
    connect(inspector_, &InspectorPanel::reanalyzeRequested, this, [this](const QString& path) {
        media_->forget(path);
        for (auto& q : project_.cues)
            if (qs(q.path) == path) {
                q.loudness.reset();
                q.peakDb.reset();
            }
        media_->info(path);
        media_->audio(path);
        updateSelection();
    });

    centerSplit_ = new QSplitter(Qt::Vertical, this);
    centerSplit_->addWidget(preview_);
    centerSplit_->addWidget(waveform_);
    centerSplit_->setStretchFactor(0, 4);
    centerSplit_->setStretchFactor(1, 1);
    centerSplit_->setSizes({520, 150});

    mainSplit_ = new QSplitter(Qt::Horizontal, this);
    mainSplit_->addWidget(playlist_);
    mainSplit_->addWidget(centerSplit_);
    mainSplit_->addWidget(inspector_);
    mainSplit_->setStretchFactor(0, 0);
    mainSplit_->setStretchFactor(1, 1);
    mainSplit_->setStretchFactor(2, 0);
    mainSplit_->setSizes({440, 760, 340});

    auto* central = new QWidget(this);
    auto* v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(buildTransportBar());
    v->addWidget(mainSplit_, 1);
    setCentralWidget(central);

    engineLabel_ = new QLabel(this);
    infoLabel_ = new QLabel(this);
    webLabel_ = new QLabel(this);
    webLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(engineLabel_);
    statusBar()->addWidget(infoLabel_, 1);
    statusBar()->addPermanentWidget(webLabel_);
    updateLabel_ = new QLabel(this);
    updateLabel_->setTextFormat(Qt::RichText);
    updateLabel_->setStyleSheet(QStringLiteral("color: #8cf; padding: 0 8px;"));
    updateLabel_->setVisible(false);
    connect(updateLabel_, &QLabel::linkActivated, this, [this](const QString&) { showUpdateDialog(); });
    statusBar()->addPermanentWidget(updateLabel_);
    onEngineConnection(false);
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* aNew = file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newProject);
    auto* aOpen = file->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openProject);
    file->addAction(tr("&Save"), QKeySequence::Save, this, [this] { saveProject(); });
    file->addAction(tr("Save &as..."), QKeySequence::SaveAs, this, [this] { saveProjectAs(); });
    file->addSeparator();
    auto* aAdd = file->addAction(tr("Add media..."), QKeySequence(Qt::CTRL | Qt::Key_I), this, &MainWindow::addMedia);
    file->addSeparator();
    file->addAction(tr("Preferences..."), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this] { openSettings(0); });
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    editActions_ << aNew << aOpen << aAdd;

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = edit->addAction(tr("&Undo"), this, &MainWindow::undo);
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = edit->addAction(tr("&Redo"), this, &MainWindow::redo);
    redoAction_->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addSeparator();
    editActions_ << edit->addAction(tr("Duplicate cue"), QKeySequence(Qt::CTRL | Qt::Key_D), this,
                                    [this] { duplicateCue(selectedCue()); })
                 << edit->addAction(tr("Thumbnail from the playing position"), QKeySequence(Qt::CTRL | Qt::Key_P), this,
                                    [this] { thumbnailFromPlayhead(); })
                 << edit->addAction(tr("Replace file..."), this, [this] { replaceFile(selectedCue()); })
                 << edit->addAction(tr("Delete cue"), this, [this] { deleteCue(selectedCue()); });

    auto* play = menuBar()->addMenu(tr("&Playback"));
    // WindowShortcut: o ESPAÇO não é roubado quando se escreve num campo de texto.
    auto* aPlay = play->addAction(tr("Play / pause"), this, &MainWindow::playPause);
    aPlay->setShortcut(Qt::Key_Space);
    play->addAction(tr("Play the selected cue"), this, [this] { playCue(selectedCue()); });
    auto* aStop = play->addAction(tr("Stop (black)"), this, &MainWindow::stopPlayback);
    aStop->setShortcut(Qt::Key_Escape);
    play->addSeparator();
    play->addAction(tr("Next cue"), QKeySequence(Qt::CTRL | Qt::Key_Right), this, &MainWindow::nextCue);
    play->addAction(tr("Previous cue"), QKeySequence(Qt::CTRL | Qt::Key_Left), this, &MainWindow::previousCue);
    play->addSeparator();
    play->addAction(tr("Go to the last 30 seconds"), QKeySequence(Qt::CTRL | Qt::Key_3), this, [this] { gotoRemaining(30); });
    play->addAction(tr("Go to the last 20 seconds"), QKeySequence(Qt::CTRL | Qt::Key_2), this, [this] { gotoRemaining(20); });
    play->addAction(tr("Go to the last 10 seconds"), QKeySequence(Qt::CTRL | Qt::Key_1), this, [this] { gotoRemaining(10); });
    play->addSeparator();
    loopAction_ = play->addAction(tr("Loop the playlist"), QKeySequence(Qt::CTRL | Qt::Key_R), this, &MainWindow::toggleLoopPlaylist);
    loopAction_->setCheckable(true);
    play->addSeparator();
    showModeAction_ = play->addAction(tr("Show Mode (lock editing)"));
    showModeAction_->setCheckable(true);
    showModeAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(showModeAction_, &QAction::triggered, this, &MainWindow::setShowMode);
    play->addSeparator();
    play->addAction(tr("Countdown window"), QKeySequence(Qt::CTRL | Qt::Key_T), this, &MainWindow::openCountdownWindow);
    play->addAction(tr("Countdown on the network..."), this, &MainWindow::showCountdownWebInfo);

    auto* outputs = menuBar()->addMenu(tr("&Outputs"));
    auto* aOutputs = outputs->addAction(tr("Outputs..."), this, &MainWindow::openOutputs);
    aOutputs->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    editActions_ << aOutputs;
    outputs->addAction(tr("Test card"), this, [this] { testBtn_->toggle(); });

    auto* eng = menuBar()->addMenu(tr("E&ngine"));
    eng->addAction(tr("Start engine"), this, &MainWindow::startEngine);
    auto* aStopEngine = eng->addAction(tr("Stop engine"), this, &MainWindow::stopEngine);
    editActions_ << aStopEngine;
    eng->addSeparator();
    eng->addAction(tr("Open logs folder"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(paths::logsDir().wstring())));
    });

    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check for updates..."), this, [this] { checkForUpdates(true); });
    help->addSeparator();
    help->addAction(tr("About VTHOST"), this, [this] { openSettings(SettingsDialog::kAboutTab); });
}

// ------------------------------------------------------------------ projecto

void MainWindow::markChanged() {
    recordUndo();
    dirty_ = true;
    updateTitle();
    pushTimer_.start();
}

void MainWindow::pushToEngine() {
    std::string err;
    Project live = project_;
    live.normalizeTargetLufs = prefs::normalizeTarget(); // preferência desta máquina
    if (!saveProjectFile(live, ss(livePath()), &err)) {
        infoLabel_->setText(tr("Could not write live project: %1").arg(qs(err)));
        return;
    }
    if (!engine_->isConnected()) return;
    protocol::Command c;
    c.type = protocol::CommandType::LoadProject;
    c.path = ss(livePath());
    engine_->sendLatest(QStringLiteral("load"), c);
}

void MainWindow::setProject(Project p, const QString& path) {
    project_ = std::move(p);
    projectPath_ = path;
    dirty_ = false;
    if (!path.isEmpty()) QSettings().setValue(QStringLiteral("lastProject"), path);
    clearUndo();
    refreshPlaylist();
    playlist_->selectIndex(project_.cues.empty() ? -1 : 0);
    for (const auto& q : project_.cues) {
        media_->info(qs(q.path));
        media_->audio(qs(q.path));
    }
    {
        const QSignalBlocker b(master_);
        master_->setValue(static_cast<int>(std::lround(project_.masterVolumeDb * 10.0)));
    }
    masterLabel_->setText(tr("%1 dB").arg(project_.masterVolumeDb, 0, 'f', 1));
    loopBtn_->setChecked(project_.loopPlaylist);
    loopAction_->setChecked(project_.loopPlaylist);
    updateTitle();
    updateSelection();
    pushTimer_.start();
}

void MainWindow::newProject() {
    if (!confirmDiscard()) return;
    Project p = Project::makeDefault();
    p.name = ss(tr("Untitled"));
    setProject(std::move(p), {});
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (prefs::confirmQuit() && !dirty_ &&
        QMessageBox::question(this, tr("Quit"), tr("Quit VTHOST Studio? The engine and the show keep running.")) !=
            QMessageBox::Yes) {
        event->ignore();
        return;
    }
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    QSettings st;
    st.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    st.setValue(QStringLiteral("ui/playerSplit"), mainSplit_->saveState());
    st.setValue(QStringLiteral("ui/centerSplit"), centerSplit_->saveState());
    if (countdownWindow_) countdownWindow_->close();
    // O motor continua a correr: fechar a interface nunca pára o show.
    event->accept();
}

// ------------------------------------------------------------------ playlist

int MainWindow::selectedCue() const {
    const int i = playlist_ ? playlist_->selectedIndex() : -1;
    return i >= 0 && i < static_cast<int>(project_.cues.size()) ? i : -1;
}

void MainWindow::refreshPlaylist() {
    playlist_->setProject(&project_);
    playlist_->setPlaying(playingCue_, playlist_->playingProgress(), playlist_->playingState());
}

void MainWindow::updateSelection() {
    const int i = selectedCue();
    if (i < 0) {
        inspector_->setCue(nullptr, -1, &project_, {}, {}, prefs::normalizeTarget());
        waveform_->setData({}, 0.0, 0.0, false, true);
        return;
    }
    const Cue& q = project_.cues[static_cast<std::size_t>(i)];
    const QString path = qs(q.path);
    const MediaInfo info = media_->info(path);
    const AudioAnalysis audio = media_->audio(path);
    inspector_->setCue(&q, i, &project_, info, audio, prefs::normalizeTarget());
    // Imagens sem som: a "forma de onda" mostra a duração definida.
    const double dur = info.duration > 0.0 ? info.duration : q.passSeconds(0.0) * q.speed;
    waveform_->setData(audio.peaks, audio.blocksPerSecond, dur, !audio.ready, info.hasAudio || !info.ready);
    waveform_->setInOut(q.inPoint, q.outPoint);
}

void MainWindow::addMedia() {
    if (showMode_) return;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add media"), QSettings().value(QStringLiteral("lastMediaDir")).toString(),
        tr("Media (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mxf *.wmv *.mpg *.mpeg *.ts *.png *.jpg *.jpeg *.bmp *.tif "
           "*.tiff *.gif *.webp *.mp3 *.wav *.aif *.aiff *.flac *.m4a *.ogg);;All files (*)"));
    if (files.isEmpty()) return;
    QSettings().setValue(QStringLiteral("lastMediaDir"), QFileInfo(files.front()).absolutePath());
    const int sel = selectedCue();
    addFiles(sel >= 0 ? sel + 1 : static_cast<int>(project_.cues.size()), files);
}

void MainWindow::addFiles(int row, const QStringList& paths) {
    if (showMode_ || paths.isEmpty()) return;
    row = std::clamp(row, 0, static_cast<int>(project_.cues.size()));
    int inserted = 0;
    for (const QString& path : paths) {
        const QFileInfo fi(path);
        if (fi.isDir() || !fi.exists()) continue;
        Cue q;
        q.id = project_.newCueId();
        q.path = ss(QDir::toNativeSeparators(fi.absoluteFilePath()).replace(QLatin1Char('\\'), QLatin1Char('/')));
        if (isImageFile(path)) q.outPoint = prefs::imageDuration(); // imagens: duração = ponto Out
        q.pauseAtEnd = prefs::newCuesPauseAtEnd();
        project_.cues.insert(project_.cues.begin() + row + inserted, q);
        media_->info(qs(q.path));
        media_->audio(qs(q.path));
        ++inserted;
    }
    if (inserted == 0) return;
    refreshPlaylist();
    playlist_->selectIndex(row);
    markChanged();
}

void MainWindow::deleteCue(int index) {
    if (showMode_ || index < 0 || index >= static_cast<int>(project_.cues.size())) return;
    const std::string id = project_.cues[static_cast<std::size_t>(index)].id;
    project_.cues.erase(project_.cues.begin() + index);
    for (auto& q : project_.cues)
        if (q.gotoCue == id) q.gotoCue.clear();
    refreshPlaylist();
    playlist_->selectIndex(std::min(index, static_cast<int>(project_.cues.size()) - 1));
    markChanged();
}

void MainWindow::duplicateCue(int index) {
    if (showMode_ || index < 0 || index >= static_cast<int>(project_.cues.size())) return;
    Cue copy = project_.cues[static_cast<std::size_t>(index)];
    copy.id = project_.newCueId();
    project_.cues.insert(project_.cues.begin() + index + 1, copy);
    refreshPlaylist();
    playlist_->selectIndex(index + 1);
    markChanged();
}

void MainWindow::moveCue(int from, int to) {
    const int n = static_cast<int>(project_.cues.size());
    if (showMode_ || from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    Cue q = std::move(project_.cues[static_cast<std::size_t>(from)]);
    project_.cues.erase(project_.cues.begin() + from);
    project_.cues.insert(project_.cues.begin() + to, std::move(q));
    refreshPlaylist();
    playlist_->selectIndex(to);
    markChanged();
}

void MainWindow::replaceFile(int index) {
    if (showMode_ || index < 0 || index >= static_cast<int>(project_.cues.size())) return;
    Cue& q = project_.cues[static_cast<std::size_t>(index)];
    const QString path = QFileDialog::getOpenFileName(this, tr("Replace file"), QFileInfo(qs(q.path)).absolutePath());
    if (path.isEmpty()) return;
    q.path = ss(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
    q.loudness.reset();
    q.peakDb.reset();
    media_->info(qs(q.path));
    media_->audio(qs(q.path));
    playlist_->refresh();
    markChanged();
    updateSelection();
}

void MainWindow::onCueEdited(const Cue& cue) {
    if (showMode_) return;
    const int i = project_.cueIndex(cue.id);
    if (i < 0) return;
    project_.cues[static_cast<std::size_t>(i)] = cue;
    playlist_->refresh();
    waveform_->setInOut(cue.inPoint, cue.outPoint);
    markChanged();
}

void MainWindow::onIconClicked(int index, CueIcon icon, const QPoint& globalPos) {
    if (showMode_ || index < 0 || index >= static_cast<int>(project_.cues.size())) return;
    playlist_->selectIndex(index);
    Cue& q = project_.cues[static_cast<std::size_t>(index)];
    const double fade = prefs::defaultFade();
    switch (icon) {
    case CueIcon::Audio: q.muted = !q.muted; break;
    case CueIcon::PauseStart: q.pauseAtStart = !q.pauseAtStart; break;
    case CueIcon::FadeIn: q.fadeIn = q.fadeIn > 0.0 ? 0.0 : fade; break;
    case CueIcon::Loop: q.playCount = q.playCount == 1 ? 0 : 1; break;
    case CueIcon::Crossfade: q.crossfade = q.crossfade > 0.0 ? 0.0 : fade; break;
    case CueIcon::FadeOut: q.fadeOut = q.fadeOut > 0.0 ? 0.0 : fade; break;
    case CueIcon::PauseEnd:
        q.pauseAtEnd = !q.pauseAtEnd;
        if (q.pauseAtEnd) q.clearAtEnd = false;
        break;
    case CueIcon::Clear:
        q.clearAtEnd = !q.clearAtEnd;
        if (q.clearAtEnd) q.pauseAtEnd = false;
        break;
    case CueIcon::Goto: {
        QMenu menu(this);
        menu.addAction(tr("None (next cue)"), this, [this, index] {
            project_.cues[static_cast<std::size_t>(index)].gotoCue.clear();
        });
        menu.addSeparator();
        for (int k = 0; k < static_cast<int>(project_.cues.size()); ++k) {
            const Cue& t = project_.cues[static_cast<std::size_t>(k)];
            const QString num = t.label.empty() ? QString::number(k + 1) : qs(t.label);
            QAction* a = menu.addAction(QStringLiteral("%1  %2").arg(num, qs(t.displayName())), this, [this, index, k] {
                project_.cues[static_cast<std::size_t>(index)].gotoCue = project_.cues[static_cast<std::size_t>(k)].id;
            });
            a->setCheckable(true);
            a->setChecked(project_.cues[static_cast<std::size_t>(index)].gotoCue == t.id);
        }
        if (!menu.exec(globalPos)) return;
        break;
    }
    case CueIcon::Count: return;
    }
    playlist_->refresh();
    markChanged();
    updateSelection();
}

void MainWindow::showCueMenu(int index, const QPoint& globalPos) {
    QMenu menu(this);
    const bool valid = index >= 0 && index < static_cast<int>(project_.cues.size());
    if (valid) {
        menu.addAction(tr("Play"), this, [this, index] { playCue(index); });
        menu.addAction(tr("Cue (ready, paused on the first frame)"), this, [this, index] {
            protocol::Command c;
            c.type = protocol::CommandType::Play;
            c.cue = index;
            c.paused = true;
            if (pushTimer_.isActive()) {
                pushTimer_.stop();
                pushToEngine();
            }
            engine_->send(c);
        });
        menu.addSeparator();
    }
    QList<QAction*> edit;
    edit << menu.addAction(tr("Add media..."), this, &MainWindow::addMedia);
    if (valid) {
        QAction* fromHead = menu.addAction(tr("Thumbnail from the playing position"), this, [this] { thumbnailFromPlayhead(); });
        fromHead->setEnabled(index == playingCue_);
        QAction* autoThumb = menu.addAction(tr("Automatic thumbnail"), this, [this, index] {
            project_.cues[static_cast<std::size_t>(index)].thumbTime.reset();
            playlist_->refresh();
            markChanged();
        });
        autoThumb->setEnabled(project_.cues[static_cast<std::size_t>(index)].thumbTime.has_value());
        edit << fromHead << autoThumb;
        menu.addSeparator();
        edit << menu.addAction(tr("Duplicate"), this, [this, index] { duplicateCue(index); })
             << menu.addAction(tr("Replace file..."), this, [this, index] { replaceFile(index); });
        menu.addAction(tr("Show in Explorer"), this, [this, index] {
            const QString path = QDir::toNativeSeparators(qs(project_.cues[static_cast<std::size_t>(index)].path));
#ifdef Q_OS_WIN
            QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), path});
#else
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
        });
        auto* colors = menu.addMenu(tr("Color"));
        edit << colors->menuAction();
        const std::pair<const char*, QString> names[] = {{"", tr("None")},         {"red", tr("Red")},
                                                         {"yellow", tr("Yellow")}, {"green", tr("Green")},
                                                         {"purple", tr("Purple")}, {"pink", tr("Pink")}};
        for (const auto& [key, label] : names) {
            const std::string k = key;
            colors->addAction(label, this, [this, index, k] {
                project_.cues[static_cast<std::size_t>(index)].color = k;
                playlist_->refresh();
                markChanged();
                updateSelection();
            });
        }
        menu.addSeparator();
        edit << menu.addAction(tr("Delete"), this, [this, index] { deleteCue(index); });
    }
    for (QAction* a : edit) a->setEnabled(!showMode_);
    menu.exec(globalPos);
}

void MainWindow::applyAnalysis(const QString& path) {
    const AudioAnalysis a = media_->audio(path);
    bool changed = false;
    for (auto& q : project_.cues) {
        if (qs(q.path) != path) continue;
        if (q.loudness != a.loudness || q.peakDb != a.peakDb) {
            q.loudness = a.loudness;
            q.peakDb = a.peakDb;
            changed = true;
        }
    }
    if (changed) {
        // Resultado de uma medição, não uma edição: não conta como passo de desfazer.
        undoBase_ = serializeProject(project_);
        dirty_ = true;
        updateTitle();
        pushTimer_.start();
    }
    updateSelection();
    playlist_->refresh();
}

double MainWindow::playingMediaTime() const {
    if (playingCue_ < 0 || playingCue_ >= static_cast<int>(project_.cues.size()) || !countdown_.active) return -1.0;
    const Cue& q = project_.cues[static_cast<std::size_t>(playingCue_)];
    double t = statusMediaTime_;
    if (countdown_.running) t += countdownClock_.elapsed() / 1000.0 * q.speed;
    const double out = q.effectiveOut(media_->info(qs(q.path)).duration);
    if (out > 0.0 && t > out)
        t = countdown_.loops ? q.inPoint + std::fmod(t - q.inPoint, std::max(0.01, out - q.inPoint)) : out;
    return t;
}

void MainWindow::thumbnailFromPlayhead() {
    // Só a cue que está a tocar: a miniatura passa a ser o frame que está no ar agora.
    if (showMode_) return;
    const double t = playingMediaTime();
    if (t < 0.0) {
        infoLabel_->setText(tr("Play the cue first: the thumbnail is taken from the position on air."));
        return;
    }
    Cue& q = project_.cues[static_cast<std::size_t>(playingCue_)];
    q.thumbTime = t;
    media_->thumbnail(qs(q.path), t);
    playlist_->refresh();
    markChanged();
    infoLabel_->setText(tr("Thumbnail updated (%1).").arg(formatTimecode(t, media_->info(qs(q.path)).fps)));
}

// ------------------------------------------------------------------ reprodução

void MainWindow::playCue(int index) {
    if (index < 0 || index >= static_cast<int>(project_.cues.size())) return;
    // Garante que o motor tem a versão mais recente antes de tocar.
    if (pushTimer_.isActive()) {
        pushTimer_.stop();
        pushToEngine();
    }
    protocol::Command c;
    c.type = protocol::CommandType::Play;
    c.cue = index;
    // SHIFT = sem transição (corte seco).
    c.cut = prefs::shiftSkipsTransitions() && (QApplication::keyboardModifiers() & Qt::ShiftModifier);
    engine_->send(c, [this](const protocol::ParsedResponse& r) {
        if (!r.ok) infoLabel_->setText(tr("Play failed: %1").arg(qs(r.error)));
    });
    playingCue_ = index;
    playlist_->setPlaying(index, 0.0, QStringLiteral("loading"));
}

void MainWindow::playPause() {
    if (!engine_->isConnected()) {
        infoLabel_->setText(tr("The engine is not running."));
        return;
    }
    const QString state = engine_->hasStatus() ? qs(engine_->lastStatus().state) : QString();
    const int sel = selectedCue();
    // A tocar / em pausa / a carregar: ESPAÇO = play/pausa da cue no ar.
    if (state == QStringLiteral("playing") || state == QStringLiteral("paused") || state == QStringLiteral("loading")) {
        protocol::Command c;
        c.type = protocol::CommandType::Transport;
        c.action = protocol::TransportAction::Toggle;
        engine_->send(c);
        return;
    }
    // Parada no último frame: se escolheu outra cue, toca essa; senão segue para a próxima.
    if (state == QStringLiteral("ended") && (sel < 0 || sel == playingCue_)) {
        protocol::Command c;
        c.type = protocol::CommandType::Transport;
        c.action = protocol::TransportAction::Toggle;
        engine_->send(c);
        return;
    }
    // Parado: ESPAÇO arranca a cue selecionada (ou a primeira).
    playCue(sel >= 0 ? sel : 0);
}

void MainWindow::stopPlayback() {
    protocol::Command c;
    c.type = protocol::CommandType::Transport;
    c.action = protocol::TransportAction::Stop;
    engine_->send(c);
}

void MainWindow::nextCue() {
    int next = -1;
    if (playingCue_ >= 0) next = project_.nextCue(playingCue_);
    else next = std::min(selectedCue() + 1, static_cast<int>(project_.cues.size()) - 1);
    if (next < 0) {
        infoLabel_->setText(tr("End of the playlist."));
        return;
    }
    playCue(next);
}

void MainWindow::previousCue() {
    const int base = playingCue_ >= 0 ? playingCue_ : selectedCue();
    if (project_.cues.empty()) return;
    playCue(std::max(0, base - 1));
}

void MainWindow::gotoRemaining(double seconds) {
    protocol::Command c;
    c.type = protocol::CommandType::SeekRemaining;
    c.time = seconds;
    engine_->send(c);
}

void MainWindow::seekTo(double fileTime) {
    if (playingCue_ < 0 || playingCue_ >= static_cast<int>(project_.cues.size())) return;
    protocol::Command c;
    c.type = protocol::CommandType::Seek;
    c.time = std::max(0.0, fileTime - project_.cues[static_cast<std::size_t>(playingCue_)].inPoint);
    engine_->sendLatest(QStringLiteral("seek"), c);
    statusMediaTime_ = fileTime;
    countdownClock_.restart();
}

void MainWindow::toggleLoopPlaylist() {
    if (showMode_) {
        loopBtn_->setChecked(project_.loopPlaylist);
        loopAction_->setChecked(project_.loopPlaylist);
        return;
    }
    project_.loopPlaylist = !project_.loopPlaylist;
    loopBtn_->setChecked(project_.loopPlaylist);
    loopAction_->setChecked(project_.loopPlaylist);
    markChanged();
}

void MainWindow::setMasterVolume(double db) {
    project_.masterVolumeDb = std::clamp(db, -60.0, 12.0);
    masterLabel_->setText(tr("%1 dB").arg(project_.masterVolumeDb, 0, 'f', 1));
    // Volume geral é um controlo ao vivo (também em Show Mode).
    dirty_ = true;
    updateTitle();
    pushTimer_.start();
}

// ------------------------------------------------------------------ show e motor

void MainWindow::applyPreferences() {
    sendEngineSettings();
    applyCountdownServer();
    playlist_->refresh();
    updateSelection();
    pushTimer_.start(); // alvo de normalização pode ter mudado
}

void MainWindow::sendEngineSettings() {
    protocol::Command c;
    c.type = protocol::CommandType::SetSettings;
    c.settings = prefs::engineSettings();
    engine_->send(c);
}

void MainWindow::openOutputs() {
    if (showMode_) return;
    const Project backup = project_;
    OutputsDialog dlg(project_.width, project_.height, project_.outputs, engine_, this);
    // Ao vivo: o motor aplica logo (sem esperar pelo OK).
    connect(&dlg, &OutputsDialog::liveChanged, this, [this, &dlg] {
        project_.width = dlg.canvasWidth();
        project_.height = dlg.canvasHeight();
        project_.outputs = dlg.outputs();
        pushTimer_.start();
    });
    if (dlg.exec() == QDialog::Accepted) {
        project_.width = dlg.canvasWidth();
        project_.height = dlg.canvasHeight();
        project_.outputs = dlg.outputs();
        markChanged();
    } else {
        project_.width = backup.width;
        project_.height = backup.height;
        project_.outputs = backup.outputs;
        pushTimer_.start();
    }
}

void MainWindow::onEngineConnection(bool connected) {
    engineLabel_->setText(connected ? tr("Engine: connected") : tr("Engine: offline"));
    engineLabel_->setStyleSheet(connected ? QStringLiteral("color: #3c3; padding: 0 8px;")
                                          : QStringLiteral("color: #e44; padding: 0 8px;"));
    if (!connected) {
        countdown_ = Countdown{};
        playingCue_ = -1;
        if (playlist_) playlist_->setPlaying(-1, 0.0, QString());
    }
    if (connected) sendEngineSettings();
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
                const QString last = QSettings().value(QStringLiteral("lastProject")).toString();
                setProject(std::move(*r.project), last);
                showMode_ = s.showMode;
                showModeAction_->setChecked(showMode_);
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
    if (s.testCard != testBtn_->isChecked()) {
        const QSignalBlocker b(testBtn_);
        testBtn_->setChecked(s.testCard);
    }

    const int previous = playingCue_;
    playingCue_ = s.state == "stopped" ? -1 : s.cue;
    statusMediaTime_ = s.mediaTime;
    countdown_ = computeCountdown(project_, s);
    countdownClock_.restart();
    refreshCountdown();
    if (playingCue_ != previous && playingCue_ >= 0 && prefs::selectionFollowsPlayback()) playlist_->selectIndex(playingCue_);
    playlist_->setPlaying(playingCue_, countdown_.duration > 0.0 ? countdown_.elapsed / countdown_.duration : 0.0, qs(s.state));
    playBtn_->setIcon(transportIcon(s.state == "playing" ? TIcon::Pause : TIcon::Play, kBlue));
    if (playingCue_ >= 0 && playingCue_ < static_cast<int>(project_.cues.size())) {
        const Cue& q = project_.cues[static_cast<std::size_t>(playingCue_)];
        QString t = (q.label.empty() ? QString::number(playingCue_ + 1) : qs(q.label)) + QStringLiteral("  ") + qs(q.displayName());
        cueLabel_->setText(cueLabel_->fontMetrics().elidedText(t, Qt::ElideRight, 250));
    } else {
        cueLabel_->setText(tr("Stopped"));
    }
    static_cast<LevelMeter*>(meter_)->setLevels(s.levelL, s.levelR);
    preview_->setFps(s.fps);

    QStringList problems;
    for (const auto& o : s.outputs)
        if (!o.ok || !o.error.empty()) problems << tr("Output %1: %2").arg(qs(o.id), qs(o.error));
    if (!s.error.empty()) problems << qs(s.error);
    if (!s.audioError.empty()) problems << tr("Audio: %1").arg(qs(s.audioError));
    for (const auto& w : s.warnings) problems << qs(w);

    QString info = tr("%1 fps").arg(s.fps, 0, 'f', 1);
    if (s.audioOk) info += QStringLiteral("  |  🔊 ") + qs(s.audioDevice);
    if (s.recovered) info += QStringLiteral("  |  ") + tr("Engine was restarted and the show resumed");
    if (!problems.isEmpty()) info += QStringLiteral("  |  ⚠ ") + problems.join(QStringLiteral("; "));
    infoLabel_->setText(info);
}

void MainWindow::updateEditingEnabled() {
    const bool edit = !showMode_;
    for (auto* w : editWidgets_) w->setEnabled(edit);
    for (auto* a : editActions_) a->setEnabled(edit);
    playlist_->setEditable(edit);
    inspector_->setEditable(edit);
    waveform_->setEditable(edit);
    updateUndoActions();
    // Play, pausa, GOTO, volume geral e test card continuam: são controlos ao vivo.
}

// ------------------------------------------------------------------ desfazer / refazer

void MainWindow::restoreSnapshot(const std::string& json) {
    auto r = parseProject(json);
    if (!r.project) {
        infoLabel_->setText(tr("Could not undo: %1").arg(qs(r.error)));
        updateUndoActions();
        return;
    }
    restoring_ = true;
    const int sel = selectedCue();
    project_ = std::move(*r.project);
    // Medições de som já feitas voltam a ser aplicadas (não fazem parte do desfazer).
    for (auto& q : project_.cues) {
        const AudioAnalysis a = media_->audio(qs(q.path));
        if (a.ready) {
            q.loudness = a.loudness;
            q.peakDb = a.peakDb;
        }
    }
    undoBase_ = serializeProject(project_);
    lastUndoPush_.invalidate(); // o próximo passo nunca se junta a este
    refreshPlaylist();
    playlist_->selectIndex(std::min(sel, static_cast<int>(project_.cues.size()) - 1));
    loopBtn_->setChecked(project_.loopPlaylist);
    loopAction_->setChecked(project_.loopPlaylist);
    markChanged(); // envia para o motor (sem registar: restoring_)
    restoring_ = false;
    updateSelection();
    updateUndoActions();
}

// ------------------------------------------------------------------ definições e atualizações

void MainWindow::openSettings(int tab) {
    SettingsDialog dlg(this, tab, engine_);
    connect(&dlg, &SettingsDialog::checkForUpdatesRequested, this, [this] { checkForUpdates(true); });
    connect(&dlg, &SettingsDialog::preferencesChanged, this, &MainWindow::applyPreferences);
    dlg.exec();
}


QString MainWindow::livePath() const {
    // O motor lê sempre esta cópia "ao vivo"; o ficheiro do utilizador só muda ao Guardar.
    return QString::fromStdWString((paths::dataDir() / "live" / "live.vthost").wstring());
}

bool MainWindow::confirmDiscard() {
    if (!dirty_) return true;
    const auto b = QMessageBox::question(this, tr("Unsaved changes"),
                                         tr("The project has unsaved changes. Save them?"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (b == QMessageBox::Save) return saveProject();
    return b == QMessageBox::Discard;
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

void MainWindow::setShowMode(bool on) {
    if (!on && showMode_ && prefs::confirmLeaveShowMode()) {
        const auto b = QMessageBox::question(this, tr("Show Mode"), tr("Leave Show Mode and unlock editing?"));
        if (b != QMessageBox::Yes) {
            showModeAction_->setChecked(true);
            return;
        }
    }
    showMode_ = on;
    showModeAction_->setChecked(on);
    protocol::Command c;
    c.type = protocol::CommandType::SetShowMode;
    c.on = on;
    engine_->send(c);
    updateEditingEnabled();
    updateTitle();
}

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

void MainWindow::checkForUpdates(bool userInitiated) {
    updateCheckIsManual_ = userInitiated;
    updater_->check();
}

void MainWindow::setUpdateLabel(const QString& html) {
    updateLabel_->setText(html);
    updateLabel_->setVisible(!html.isEmpty());
}

void MainWindow::onUpdateAvailable(const UpdateInfo& info) {
    const bool newVersion = !availableUpdate_ || availableUpdate_->version != info.version;
    availableUpdate_ = info;
    if (newVersion) readyInstaller_.clear();
    if (!downloading_ && readyInstaller_.isEmpty())
        setUpdateLabel(tr("Version %1 available — <a href=\"#\">see what's new</a>").arg(info.version));
    // A janela abre sozinha uma vez por versão, mas nunca em Show Mode nem com uma cue no ar
    // (não pode aparecer por cima do show). O aviso no rodapé fica sempre.
    const bool showBusy = showMode_ || playingCue_ >= 0;
    if (updateCheckIsManual_ || (!promptedVersions_.contains(info.version) && !showBusy)) {
        promptedVersions_.insert(info.version);
        showUpdateDialog();
    }
}

void MainWindow::showUpdateDialog() {
    if (!availableUpdate_) return;
    if (updateDialog_) {
        updateDialog_->raise();
        updateDialog_->activateWindow();
        return;
    }
    auto* dlg = new UpdateDialog(*availableUpdate_, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    updateDialog_ = dlg;
    if (!readyInstaller_.isEmpty()) dlg->setReady();
    connect(dlg, &UpdateDialog::updateNowRequested, this, [this] {
        if (showMode_) {
            if (updateDialog_) updateDialog_->setFailed(tr("Turn off Show Mode before updating."));
            return;
        }
        if (!readyInstaller_.isEmpty()) {
            if (updateDialog_) updateDialog_->setReady();
            return;
        }
        if (downloading_ || !availableUpdate_) return;
        downloading_ = true;
        updater_->download(*availableUpdate_);
    });
    connect(dlg, &UpdateDialog::installNowRequested, this, [this] {
        if (readyInstaller_.isEmpty()) return;
        installUpdate(readyInstaller_, false);
    });
    dlg->show();
}

void MainWindow::installUpdate(const QString& installerPath, bool ask) {
    if (showMode_) {
        QMessageBox::warning(this, tr("Update"), tr("Turn off Show Mode before updating."));
        return;
    }
    // O motor pára durante a instalação: confirma sempre se houver uma cue no ar.
    if (ask || playingCue_ >= 0) {
        const auto b = QMessageBox::question(
            this, tr("Update"),
            tr("The update is ready. The engine will stop and the outputs will go dark while it installs. Install now?"));
        if (b != QMessageBox::Yes) return;
    }
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

void MainWindow::recordUndo() {
    if (restoring_) return;
    std::string now = serializeProject(project_);
    if (now == undoBase_) return;
    if (undoBase_.empty()) { // ainda sem estado base: este passa a sê-lo
        undoBase_ = std::move(now);
        return;
    }
    // Alterações seguidas (ex. rodar um valor, várias teclas) ficam num só passo.
    if (undoStack_.empty() || !lastUndoPush_.isValid() || lastUndoPush_.elapsed() > 400) {
        undoStack_.push_back(std::move(undoBase_));
        while (undoStack_.size() > kMaxUndo) undoStack_.pop_front();
    }
    lastUndoPush_.restart();
    undoBase_ = std::move(now);
    redoStack_.clear();
    updateUndoActions();
}

void MainWindow::clearUndo() {
    undoStack_.clear();
    redoStack_.clear();
    undoBase_ = serializeProject(project_);
    lastUndoPush_.invalidate();
    updateUndoActions();
}

void MainWindow::undo() {
    if (showMode_) return;
    recordUndo(); // inclui alterações ainda por registar
    if (undoStack_.empty()) return;
    redoStack_.push_back(undoBase_);
    std::string target = std::move(undoStack_.back());
    undoStack_.pop_back();
    restoreSnapshot(target);
}

void MainWindow::redo() {
    if (showMode_ || redoStack_.empty()) return;
    undoStack_.push_back(undoBase_);
    std::string target = std::move(redoStack_.back());
    redoStack_.pop_back();
    restoreSnapshot(target);
}

void MainWindow::updateUndoActions() {
    if (undoAction_) undoAction_->setEnabled(!showMode_ && !undoStack_.empty());
    if (redoAction_) redoAction_->setEnabled(!showMode_ && !redoStack_.empty());
}

Countdown MainWindow::currentCountdown() const {
    if (!engine_->isConnected()) return {};
    return advanceCountdown(countdown_, countdownClock_.elapsed() / 1000.0);
}

void MainWindow::refreshCountdown() {
    const Countdown c = currentCountdown();
    if (countdownWindow_ && countdownWindow_->isVisible()) countdownWindow_->setCountdown(c);
}

void MainWindow::openCountdownWindow() {
    if (!countdownWindow_) countdownWindow_ = new CountdownWindow(this);
    countdownWindow_->setCountdown(currentCountdown());
    countdownWindow_->show();
    countdownWindow_->raise();
    countdownWindow_->activateWindow();
}

void MainWindow::applyCountdownServer() {
    const bool want = prefs::countdownWebEnabled();
    const auto port = static_cast<quint16>(std::clamp(prefs::countdownWebPort(), 1024, 65535));
    if (!want) {
        countdownServer_->stop();
        webLabel_->clear();
        return;
    }
    if (!countdownServer_->isRunning() || countdownServer_->port() != port) {
        QString err;
        if (!countdownServer_->start(port, &err)) {
            webLabel_->setText(tr("Countdown web: port %1 unavailable").arg(port));
            webLabel_->setToolTip(err);
            return;
        }
    }
    const QStringList urls = countdownServer_->urls();
    webLabel_->setText(tr("Countdown: %1").arg(urls.value(0)));
    webLabel_->setToolTip(tr("Anyone on the same network can open this address in a browser:\n%1").arg(urls.join('\n')));
}

void MainWindow::showCountdownWebInfo() {
    applyCountdownServer();
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Countdown on the network"));
    auto* l = new QVBoxLayout(&dlg);
    if (!countdownServer_->isRunning()) {
        l->addWidget(new QLabel(prefs::countdownWebEnabled()
                                    ? tr("The countdown page could not start on port %1. Choose another port in "
                                         "Preferences → Countdown.").arg(prefs::countdownWebPort())
                                    : tr("The countdown page is turned off. Turn it on in Preferences → Countdown."),
                                &dlg));
    } else {
        const QStringList urls = countdownServer_->urls();
        auto* text = new QLabel(tr("People on the same network (Wi-Fi or cable) can follow the countdown on a "
                                   "phone, tablet or computer by opening this address in the browser:"),
                                &dlg);
        text->setWordWrap(true);
        l->addWidget(text);
        for (const QString& u : urls) {
            auto* row = new QHBoxLayout;
            auto* link = new QLabel(QStringLiteral("<a href=\"%1\" style=\"font-size:16pt\">%1</a>").arg(u), &dlg);
            link->setOpenExternalLinks(true);
            link->setTextInteractionFlags(Qt::TextBrowserInteraction);
            auto* copy = new QPushButton(tr("Copy"), &dlg);
            connect(copy, &QPushButton::clicked, &dlg, [u] { QApplication::clipboard()->setText(u); });
            row->addWidget(link, 1);
            row->addWidget(copy);
            l->addLayout(row);
        }
        auto* hint = new QLabel(tr("If Windows asks, allow VTHOST Studio on private networks. "
                                   "Double-click the page to go full screen."),
                                &dlg);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color: gray;"));
        l->addWidget(hint);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    l->addWidget(buttons);
    dlg.setMinimumWidth(480);
    dlg.exec();
}

void MainWindow::updateTitle() {
    QString t = QStringLiteral("VTHOST Studio - %1").arg(qs(project_.name));
    if (dirty_) t += QStringLiteral(" *");
    if (showMode_) t += QStringLiteral("  [") + tr("SHOW MODE") + QStringLiteral("]");
    setWindowTitle(t);
}
