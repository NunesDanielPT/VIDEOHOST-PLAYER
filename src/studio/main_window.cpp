#include "main_window.h"

#include "app_info.h"
#include "countdown_server.h"
#include "countdown_view.h"
#include "osc_server.h"
#include "tally_client.h"
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
#include <QInputDialog>
#include <QActionGroup>
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
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <cmath>
#include <memory>

using namespace vthost;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }

constexpr std::size_t kMaxUndo = 100;
const QColor kBlue(42, 160, 255);

// Medidor de nível (L/R) da barra de transporte.
// VUs L/R ao lado do preview: escala em dB (-60..0) com números, pico que desce
// devagar e o valor em dB escrito em cima (actualizado ~3 vezes por segundo, para
// se conseguir ler). O estado do motor chega 4×/s; a queda é animada aqui.
class LevelMeter : public QWidget {
public:
    explicit LevelMeter(QWidget* parent) : QWidget(parent) {
        setFixedWidth(46);
        setMinimumHeight(110);
        timer_.setInterval(40);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
        timer_.start();
    }
    void setLevels(double l, double r) {
        target_[0] = toFraction(l);
        target_[1] = toFraction(r);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(24, 24, 24));
        QFont f = font();
        f.setPixelSize(9);
        p.setFont(f);
        const int topPad = 18;    // valor em dB
        const int bottomPad = 14; // L / R
        const int top = topPad;
        const int h = std::max(10, height() - topPad - bottomPad);
        auto yFor = [&](double db) { return top + static_cast<int>((1.0 - (db + 60.0) / 60.0) * h); };

        // Valor actual (o mais alto dos dois canais).
        p.setPen(readout_ > -3.0 ? QColor(220, 90, 80) : QColor(185, 185, 185));
        p.drawText(QRect(0, 2, width(), topPad - 4), Qt::AlignCenter,
                   readout_ <= -59.5 ? QStringLiteral("-∞") : QString::number(readout_, 'f', 0));

        // Escala com números (à direita).
        for (int db : {0, -6, -12, -24, -48}) {
            const int y = yFor(db);
            p.setPen(QColor(70, 70, 70));
            p.drawLine(26, y, 28, y);
            p.setPen(QColor(105, 105, 105));
            p.drawText(QRect(29, y - 6, width() - 29, 12), Qt::AlignLeft | Qt::AlignVCenter, QString::number(-db));
        }
        for (int ch = 0; ch < 2; ++ch) {
            const int x = 3 + ch * 11;
            const QRect track(x, top, 9, h);
            p.fillRect(track, QColor(34, 34, 34));
            const int lh = static_cast<int>(shown_[ch] * h);
            // Verde até -12, amarelo até -3, vermelho acima (cores suaves).
            for (int y = 0; y < lh; y += 2) {
                const double db = static_cast<double>(y) / h * 60.0 - 60.0;
                const QColor c = db > -3.0 ? QColor(210, 70, 60) : db > -12.0 ? QColor(210, 175, 50) : QColor(60, 170, 80);
                p.fillRect(QRect(x, top + h - y - 1, 9, 1), c);
            }
            const int py = top + h - static_cast<int>(peak_[ch] * h);
            if (peak_[ch] > 0.01) p.fillRect(QRect(x, py, 9, 1), QColor(220, 220, 220));
            p.setPen(QColor(110, 110, 110));
            p.drawText(QRect(x - 1, height() - bottomPad, 11, bottomPad), Qt::AlignCenter,
                       ch == 0 ? QStringLiteral("L") : QStringLiteral("R"));
        }
    }

private:
    static double toFraction(double v) {
        const double db = v > 0.0 ? 20.0 * std::log10(v) : -60.0;
        return std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
    }
    void tick() {
        bool changed = false;
        for (int ch = 0; ch < 2; ++ch) {
            const double next = std::max(target_[ch], shown_[ch] - 0.025); // ~25 dB/s a descer
            if (next != shown_[ch]) changed = true;
            shown_[ch] = next;
            if (shown_[ch] >= peak_[ch]) {
                peak_[ch] = shown_[ch];
                peakHold_[ch] = 30; // ~1,2 s
            } else if (peakHold_[ch] > 0) {
                --peakHold_[ch];
            } else if (peak_[ch] > 0.0) {
                peak_[ch] = std::max(0.0, peak_[ch] - 0.01);
                changed = true;
            }
        }
        // Número em dB: o máximo dos últimos ~300 ms (lê-se sem saltar).
        windowMax_ = std::max({windowMax_, target_[0], target_[1]});
        if (++readoutTicks_ >= 8) {
            readoutTicks_ = 0;
            const double next = windowMax_ * 60.0 - 60.0;
            if (std::abs(next - readout_) > 0.4) changed = true;
            readout_ = next;
            windowMax_ = 0.0;
        }
        if (changed) update();
    }

    QTimer timer_;
    double target_[2] = {0.0, 0.0};
    double shown_[2] = {0.0, 0.0};
    double peak_[2] = {0.0, 0.0};
    int peakHold_[2] = {0, 0};
    double windowMax_ = 0.0;
    double readout_ = -60.0;
    int readoutTicks_ = 0;
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
                                                  : tr("VIDEOHOST PLAYER is up to date (version %1).").arg(appinfo::kVersion));
    });
    connect(updater_, &UpdateChecker::checkFailed, this, [this](const QString& err) {
        if (updateCheckIsManual_) QMessageBox::warning(this, tr("Updates"), err);
    });
    connect(updater_, &UpdateChecker::downloadProgress, this, [this](qint64 rec, qint64 total) {
        if (updateDialog_) updateDialog_->setProgress(rec, total);
        if (total > 0 && availableUpdate_)
            setUpdateLabel(tr("Downloading VIDEOHOST PLAYER %1... %2%").arg(availableUpdate_->version).arg(rec * 100 / total));
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
    tally_ = new TallyClient(this);
    connect(tally_, &TallyClient::wentOnAir, this, &MainWindow::fireArmedCue);
    connect(tally_, &TallyClient::stateChanged, this, &MainWindow::updatePerf);
    connect(tally_, &TallyClient::onAirChanged, this, &MainWindow::updatePerf);

    osc_ = new OscServer(this);
    connect(osc_, &OscServer::messageReceived, this, &MainWindow::onOscMessage);
    oscTimer_.setInterval(100); // tempo que falta para o Companion, 10×/s
    connect(&oscTimer_, &QTimer::timeout, this, [this] { sendOscState(false); });

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
    auto* blockBtn = new QToolButton(bar);
    blockBtn->setText(tr("+ BLOCK"));
    blockBtn->setToolTip(tr("New block (Ctrl+B): groups videos; drop files on it, loop it, pause at its end"));
    blockBtn->setStyleSheet(QStringLiteral("QToolButton { font-size: 10px; font-weight: bold; color: #9cc3ff; "
                                           "border: 1px solid #3c5880; padding: 3px 6px; }"));
    connect(blockBtn, &QToolButton::clicked, this, [this] { newBlock(selectedCue()); });
    l->addWidget(blockBtn);
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
    connect(playlist_, &PlaylistView::playRequested, this, [this](int i) { playCue(i); });
    connect(playlist_, &PlaylistView::iconClicked, this, &MainWindow::onIconClicked);
    connect(playlist_, &PlaylistView::filesDropped, this, [this](int before, const QStringList& files, const QString& block) {
        addFiles(before, files, block);
    });
    connect(playlist_, &PlaylistView::cueDropped, this, &MainWindow::dropCue);
    connect(playlist_, &PlaylistView::blockToggleRequested, this, &MainWindow::toggleBlock);
    connect(playlist_, &PlaylistView::blockMenuRequested, this, &MainWindow::showBlockMenu);
    connect(playlist_, &PlaylistView::deleteRequested, this, &MainWindow::deleteCue);
    connect(playlist_, &PlaylistView::contextMenuRequested, this, &MainWindow::showCueMenu);
    connect(playlist_, &PlaylistView::thumbnailRequested, this, [this](int) { thumbnailFromPlayhead(); });
    connect(playlist_, &PlaylistView::armClicked, this, [this](int i) {
        if (i < 0 || i >= static_cast<int>(project_.cues.size())) return;
        const std::string id = project_.cues[static_cast<std::size_t>(i)].id;
        armedCueId_ = armedCueId_ == id ? std::string() : id; // clicar outra vez desarma
        updateArmed();
    });
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

    // Preview com os VUs (L/R) ao lado, discretos.
    auto* previewBox = new QWidget(this);
    auto* pb = new QHBoxLayout(previewBox);
    pb->setContentsMargins(0, 0, 0, 0);
    pb->setSpacing(2);
    pb->addWidget(preview_, 1);
    meter_ = new LevelMeter(previewBox);
    meter_->setToolTip(tr("Audio level (L / R)"));
    pb->addWidget(meter_);

    centerSplit_ = new QSplitter(Qt::Vertical, this);
    centerSplit_->addWidget(previewBox);
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

    auto* mediaPage = new QWidget(this);
    auto* v = new QVBoxLayout(mediaPage);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(buildTransportBar());
    v->addWidget(mainSplit_, 1);

    setCentralWidget(mediaPage);

    engineLabel_ = new QLabel(this);
    infoLabel_ = new QLabel(this);
    webLabel_ = new QLabel(this);
    webLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(engineLabel_);
    statusBar()->addWidget(infoLabel_, 1);
    perfLabel_ = new QLabel(this);
    perfLabel_->setStyleSheet(QStringLiteral("color: #8a8a8a; padding: 0 8px;"));
    statusBar()->addPermanentWidget(perfLabel_);
    statusBar()->addPermanentWidget(webLabel_);
    perfTimer_.setInterval(1000);
    connect(&perfTimer_, &QTimer::timeout, this, &MainWindow::updatePerf);
    perfTimer_.start();
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
    auto* aBlock = file->addAction(tr("New block"), QKeySequence(Qt::CTRL | Qt::Key_B), this, [this] {
        newBlock(selectedCue());
    });
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    editActions_ << aNew << aOpen << aAdd << aBlock;

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

    // Saídas e motor num só menu (o motor arranca sozinho com o Studio).
    auto* outputs = menuBar()->addMenu(tr("&Outputs"));
    auto* aOutputs = outputs->addAction(tr("Outputs..."), this, &MainWindow::openOutputs);
    aOutputs->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    editActions_ << aOutputs;
    outputs->addAction(tr("Test card"), this, [this] { testBtn_->toggle(); });
    outputs->addSeparator();
    outputs->addAction(tr("Start engine"), this, &MainWindow::startEngine);
    auto* aRestartEngine = outputs->addAction(tr("Restart engine (the show resumes)"), this, &MainWindow::restartEngine);
    auto* aStopEngine = outputs->addAction(tr("Stop engine"), this, &MainWindow::stopEngine);
    editActions_ << aStopEngine << aRestartEngine;
    outputs->addSeparator();
    outputs->addAction(tr("Open logs folder"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(paths::logsDir().wstring())));
    });

    // Janela: countdown.
    auto* window = menuBar()->addMenu(tr("&Window"));
    window->addAction(tr("Countdown window"), QKeySequence(Qt::CTRL | Qt::Key_T), this, &MainWindow::openCountdownWindow);
    window->addAction(tr("Countdown on the network..."), this, &MainWindow::showCountdownWebInfo);

    // Definições (antes da Ajuda).
    auto* settings = menuBar()->addMenu(tr("&Settings"));
    settings->addAction(tr("Preferences..."), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this] { openSettings(0); });
    settings->addSeparator();
    settings->addAction(tr("Audio..."), this, [this] { openSettings(SettingsDialog::kAudioTab); });
    settings->addAction(tr("Network..."), this, [this] { openSettings(SettingsDialog::kNetworkTab); });
    settings->addAction(tr("Countdown..."), this, [this] { openSettings(SettingsDialog::kCountdownTab); });
    settings->addAction(tr("Shortcuts..."), this, [this] { openSettings(SettingsDialog::kShortcutsTab); });

    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check for updates..."), this, [this] { checkForUpdates(true); });
    help->addAction(tr("OSC commands (Companion)..."), this, &MainWindow::showOscHelp);
    help->addSeparator();
    help->addAction(tr("About VIDEOHOST PLAYER"), this, [this] { openSettings(SettingsDialog::kAboutTab); });
}

void MainWindow::updatePerf() {
    // Rodapé: CPU e RAM do PC, VRAM e fps do motor. Fica laranja quando está perto do limite.
    QStringList parts, tip;
    bool warn = false;
    double cpu = 0.0;
    if (sysStats_.cpuPercent(cpu)) {
        parts << tr("CPU %1%").arg(qRound(cpu));
        warn |= cpu > 90.0;
    }
    double ramPct = 0.0, ramUsed = 0.0, ramTotal = 0.0;
    if (sysStats_.ram(ramPct, ramUsed, ramTotal)) {
        parts << tr("RAM %1%").arg(qRound(ramPct));
        tip << tr("RAM: %1 of %2 GB in use on this PC").arg(ramUsed, 0, 'f', 1).arg(ramTotal, 0, 'f', 1);
        warn |= ramPct > 90.0;
    }
    if (engine_->isConnected() && engine_->hasStatus()) {
        const auto& s = engine_->lastStatus();
        if (!s.gpuName.empty()) tip << tr("Graphics card: %1").arg(qs(s.gpuName));
        if (s.vramBudgetMb > 0.0) {
            const double pct = 100.0 * s.vramUsedMb / s.vramBudgetMb;
            parts << tr("VRAM %1%").arg(qRound(pct));
            tip << tr("VRAM: %1 MB used by the engine, %2 MB available to it").arg(qRound(s.vramUsedMb)).arg(qRound(s.vramBudgetMb));
            warn |= pct > 90.0;
        }
        QString fps = tr("%1 fps").arg(s.fps, 0, 'f', 0);
        if (s.renderMs > 0.05) {
            const double capacity = 1000.0 / s.renderMs;
            fps += QStringLiteral(" ") + tr("(max ≈ %1)").arg(capacity >= 1000.0 ? QStringLiteral("1000+") : QString::number(qRound(capacity)));
            tip << tr("Output: %1 fps. Each frame takes %2 ms to prepare, so the engine could render about %3 fps.")
                       .arg(s.fps, 0, 'f', 1)
                       .arg(s.renderMs, 0, 'f', 2)
                       .arg(qRound(capacity));
            warn |= s.fps > 1.0 && s.renderMs > 800.0 / s.fps; // menos de 20 % de folga
        }
        parts << fps;
    }
    if (osc_ && osc_->isListening()) parts.prepend(QStringLiteral("OSC %1").arg(osc_->config().port));
    if (tally_ && tally_->config().mode != TallyClient::Mode::Off) {
        const QString who = tally_->config().mode == TallyClient::Mode::Vmix ? QStringLiteral("vMix") : QStringLiteral("ATEM");
        QString t = who + QStringLiteral(" ") + QString::number(tally_->config().input) + QStringLiteral(": ");
        if (!tally_->isConnected()) {
            t += tr("not connected");
            tip << who + QStringLiteral(": ") + (tally_->lastError().isEmpty() ? tr("connecting...") : tally_->lastError());
            warn = true;
        } else {
            t += tally_->onAir() ? tr("ON AIR") : tr("ready");
        }
        parts.prepend(t);
    }
    perfLabel_->setText(parts.join(QStringLiteral("  ·  ")));
    perfLabel_->setToolTip(tip.join(QStringLiteral("\n")));
    perfLabel_->setStyleSheet(warn ? QStringLiteral("color: #e0a030; padding: 0 8px;") : QStringLiteral("color: #8a8a8a; padding: 0 8px;"));
}

// ------------------------------------------------------------------ misturador (vMix / ATEM)

void MainWindow::applyTally() {
    TallyClient::Config c;
    c.mode = static_cast<TallyClient::Mode>(std::clamp(prefs::tallyMode(), 0, 2));
    c.host = prefs::tallyHost();
    c.input = std::clamp(prefs::tallyInput(), 0, 9999);
    tally_->configure(c);
    updatePerf();
}

void MainWindow::updateArmed() {
    playlist_->setArmed(armedCueId_.empty() ? -1 : project_.cueIndex(armedCueId_));
}

void MainWindow::fireArmedCue() {
    const int i = armedCueId_.empty() ? -1 : project_.cueIndex(armedCueId_);
    if (i < 0 || !engine_->isConnected()) return;
    playCue(i);
    if (tally_->onAir()) infoLabel_->setText(tr("Cue %1 started by the switcher (input on air)").arg(cueOscId(i)));
    // Enquanto toca nada fica armado (um novo corte para o player não a reinicia).
    // Quando acabar, a seguinte fica em CUE (opção) — ver followTallyPlayback.
    watchedCueId_ = project_.cues[static_cast<std::size_t>(i)].id;
    watchedSeen_ = false;
    watchedSince_.start();
    armedCueId_.clear();
    updateArmed();
}

void MainWindow::followTallyPlayback(const protocol::EngineStatus& s) {
    if (watchedCueId_.empty()) return;
    const bool running = s.state == "playing" || s.state == "paused" || s.state == "loading";
    const std::string cur = running && playingCue_ >= 0 && playingCue_ < static_cast<int>(project_.cues.size())
                                ? project_.cues[static_cast<std::size_t>(playingCue_)].id
                                : std::string();
    if (!watchedSeen_) {
        if (cur == watchedCueId_) watchedSeen_ = true;
        else if (watchedSince_.elapsed() > 5000) watchedCueId_.clear(); // nunca chegou a tocar
        return;
    }
    if (cur == watchedCueId_) return;
    if (!cur.empty()) {
        // Seguiu sozinha para outra cue (ex. dentro de um bloco): passa a vigiar essa.
        watchedCueId_ = cur;
        return;
    }
    // Acabou (preto ou parada no fim): a cue seguinte fica em CUE, se o operador não armou outra.
    const int i = project_.cueIndex(watchedCueId_);
    watchedCueId_.clear();
    const int n = static_cast<int>(project_.cues.size());
    if (prefs::tallyArmNext() && armedCueId_.empty() && i >= 0 && i + 1 < n) {
        armedCueId_ = project_.cues[static_cast<std::size_t>(i + 1)].id;
        updateArmed();
    }
}

// ------------------------------------------------------------------ OSC (Companion)

void MainWindow::applyOsc() {
    OscServer::Config c;
    c.enabled = prefs::oscEnabled();
    c.bindAddress = prefs::oscBindAddress();
    c.port = static_cast<quint16>(std::clamp(prefs::oscPort(), 1024, 65535));
    c.feedbackHost = prefs::oscFeedbackHost();
    c.feedbackPort = static_cast<quint16>(std::clamp(prefs::oscFeedbackPort(), 1024, 65535));
    c.onlyFromFeedbackHost = prefs::oscOnlyFromFeedbackHost();
    QString err;
    if (!osc_->configure(c, &err)) infoLabel_->setText(tr("OSC: could not open port %1 (%2)").arg(c.port).arg(err));
    if (osc_->isListening()) {
        oscPlaylistSignature_.clear();
        sendOscState(true);
        oscTimer_.start();
    } else {
        oscTimer_.stop();
    }
    updatePerf();
}

QString MainWindow::cueOscId(int index) const {
    if (index < 0 || index >= static_cast<int>(project_.cues.size())) return QStringLiteral("-");
    const Cue& q = project_.cues[static_cast<std::size_t>(index)];
    // Como o módulo do Companion: maiúsculas, no máximo 6 caracteres.
    return q.label.empty() ? QString::number(index + 1) : qs(q.label).toUpper().left(6);
}

int MainWindow::cueByOscId(const QString& raw) const {
    const QString id = raw.trimmed().toUpper();
    if (id.isEmpty()) return -1;
    for (int i = 0; i < static_cast<int>(project_.cues.size()); ++i)
        if (cueOscId(i) == id) return i;
    bool ok = false;
    const int n = id.toInt(&ok);
    return ok && n >= 1 && n <= static_cast<int>(project_.cues.size()) ? n - 1 : -1;
}

void MainWindow::onOscMessage(const osc::Message& m) {
    // Protocolo do Mitti: /mitti/<comando> [valor]. Também aceita /videohost/<comando>.
    QString addr = qs(m.address);
    if (addr.startsWith(QStringLiteral("/mitti/"))) addr = addr.mid(7);
    else if (addr.startsWith(QStringLiteral("/videohost/"))) addr = addr.mid(11);
    else return;
    const QString arg = qs(m.stringArg());
    double num = 0.0;
    const bool hasNum = m.numberArg(num);
    const bool connected = engine_->isConnected();
    auto transport = [this](protocol::TransportAction a) {
        protocol::Command c;
        c.type = protocol::CommandType::Transport;
        c.action = a;
        engine_->send(c);
    };
    const QString state = engine_->hasStatus() ? qs(engine_->lastStatus().state) : QString();
    const bool onAir = state == QStringLiteral("playing") || state == QStringLiteral("paused") ||
                       state == QStringLiteral("loading") || state == QStringLiteral("ended");

    if (addr == QStringLiteral("ping")) {
        osc_->send(QStringLiteral("/mitti/pong"), QString(), true);
    } else if (addr == QStringLiteral("resendOSCFeedback")) {
        oscPlaylistSignature_.clear();
        sendOscState(true);
    } else if (!connected) {
        return; // comandos de reprodução precisam do motor
    } else if (addr == QStringLiteral("togglePlay")) {
        playPause();
    } else if (addr == QStringLiteral("play")) {
        if (state == QStringLiteral("paused")) transport(protocol::TransportAction::Resume);
        else if (!onAir || state == QStringLiteral("ended")) playPause();
    } else if (addr == QStringLiteral("stop") || addr == QStringLiteral("pause")) {
        transport(protocol::TransportAction::Pause); // no Mitti "stop" = pausa
    } else if (addr == QStringLiteral("panic") || addr == QStringLiteral("stopAll")) {
        stopPlayback();
    } else if (addr == QStringLiteral("rewind")) {
        if (playingCue_ >= 0) seekTo(project_.cues[static_cast<std::size_t>(playingCue_)].inPoint);
    } else if (addr == QStringLiteral("jumpToNextCue")) {
        nextCue();
    } else if (addr == QStringLiteral("jumpToPrevCue")) {
        previousCue();
    } else if (addr == QStringLiteral("selectNextCue") || addr == QStringLiteral("selectPrevCue")) {
        const int n = static_cast<int>(project_.cues.size());
        if (n > 0) playlist_->selectIndex(std::clamp(selectedCue() + (addr == QStringLiteral("selectNextCue") ? 1 : -1), 0, n - 1));
    } else if (addr == QStringLiteral("jumpToSelectedCue") || addr == QStringLiteral("playSelectedCue") ||
               addr == QStringLiteral("playSelectedCueForceCut")) {
        if (selectedCue() >= 0) playCue(selectedCue(), addr.endsWith(QStringLiteral("ForceCut")));
    } else if (addr == QStringLiteral("playCueWithCueID") || addr == QStringLiteral("playCueWithCueIDForceCut")) {
        const int i = cueByOscId(arg);
        if (i >= 0) playCue(i, addr.endsWith(QStringLiteral("ForceCut")));
    } else if (addr == QStringLiteral("playCueAtIndex") || addr == QStringLiteral("playCueAtIndexForceCut")) {
        if (hasNum && num >= 1 && num <= static_cast<double>(project_.cues.size()))
            playCue(static_cast<int>(num) - 1, addr.endsWith(QStringLiteral("ForceCut")));
    } else if (addr == QStringLiteral("playCueWithName") || addr == QStringLiteral("playCueWithNameForceCut") ||
               addr == QStringLiteral("jumpToCueWithName")) {
        for (int i = 0; i < static_cast<int>(project_.cues.size()); ++i)
            if (qs(project_.cues[static_cast<std::size_t>(i)].displayName()).compare(arg, Qt::CaseInsensitive) == 0) {
                playCue(i, addr.endsWith(QStringLiteral("ForceCut")));
                break;
            }
    } else if (addr == QStringLiteral("goto30") || addr == QStringLiteral("goto20") || addr == QStringLiteral("goto10")) {
        gotoRemaining(addr.mid(4).toDouble());
    } else if (addr == QStringLiteral("gotoRemaining") && hasNum) {
        gotoRemaining(std::clamp(num, 0.0, 36000.0));
    } else if (addr == QStringLiteral("mainFader") && hasNum) {
        // 0..1 como no Mitti (1 = 0 dB).
        const double db = num <= 0.001 ? -60.0 : std::clamp(20.0 * std::log10(num), -60.0, 12.0);
        master_->setValue(static_cast<int>(std::lround(db * 10.0)));
    } else if (addr == QStringLiteral("testPattern") || addr == QStringLiteral("testCard")) {
        testBtn_->setChecked(hasNum ? num != 0.0 : !testBtn_->isChecked());
    } else if (addr.endsWith(QStringLiteral("/jump")) || addr.endsWith(QStringLiteral("/play"))) {
        const int i = cueByOscId(addr.section(QLatin1Char('/'), 0, 0));
        if (i >= 0) playCue(i);
    } else if (addr.endsWith(QStringLiteral("/select"))) {
        const int i = cueByOscId(addr.section(QLatin1Char('/'), 0, 0));
        if (i >= 0) playlist_->selectIndex(i);
    }
    sendOscState(false);
}

void MainWindow::sendOscState(bool force) {
    if (!osc_ || !osc_->isListening()) return;
    auto hms = [](double s, bool up) {
        s = std::max(0.0, s);
        const qint64 t = up ? static_cast<qint64>(std::ceil(s - 1e-6)) : static_cast<qint64>(std::floor(s));
        return QStringLiteral("%1:%2:%3")
            .arg(t / 3600, 2, 10, QLatin1Char('0'))
            .arg((t / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(t % 60, 2, 10, QLatin1Char('0'));
    };
    auto name = [this](int i) {
        return i >= 0 && i < static_cast<int>(project_.cues.size()) ? qs(project_.cues[static_cast<std::size_t>(i)].displayName())
                                                                     : QStringLiteral("-");
    };
    // Lista de cues (só quando muda): dá os botões "Play Cue <ID>" do Companion com o nome.
    QString sig;
    for (int i = 0; i < static_cast<int>(project_.cues.size()); ++i) sig += cueOscId(i) + QLatin1Char('\x1f') + name(i) + QLatin1Char('\x1e');
    if (force || sig != oscPlaylistSignature_) {
        oscPlaylistSignature_ = sig;
        for (int i = 0; i < static_cast<int>(project_.cues.size()); ++i)
            osc_->send(QStringLiteral("/mitti/%1/cueName").arg(cueOscId(i)), name(i), true);
        force = true;
    }
    const auto& s = engine_->lastStatus();
    const bool hasStatus = engine_->isConnected() && engine_->hasStatus();
    const int cur = hasStatus && s.state != "stopped" ? playingCue_ : -1;
    const int base = cur >= 0 ? cur : selectedCue();
    osc_->send(QStringLiteral("/mitti/togglePlay"), hasStatus && s.state == "playing" ? 1 : 0, force);
    osc_->send(QStringLiteral("/mitti/currentCueID"), cur >= 0 ? cueOscId(cur) : QStringLiteral("-"), force);
    osc_->send(QStringLiteral("/mitti/currentCueName"), cur >= 0 ? name(cur) : QStringLiteral("-"), force);
    osc_->send(QStringLiteral("/mitti/previousCueName"), name(base - 1), force);
    osc_->send(QStringLiteral("/mitti/nextCueName"), name(cur >= 0 ? project_.nextCue(cur) : base + 1), force);
    osc_->send(QStringLiteral("/mitti/selectedCueID"), selectedCue() >= 0 ? cueOscId(selectedCue()) : QStringLiteral("-"), force);
    osc_->send(QStringLiteral("/mitti/selectedCueName"), name(selectedCue()), force);
    const Countdown c = currentCountdown();
    if (cur >= 0 && c.active) {
        osc_->send(QStringLiteral("/mitti/cueTimeLeft"), QStringLiteral("-") + hms(c.remaining, true), force);
        osc_->send(QStringLiteral("/mitti/cueTimeElapsed"), hms(c.elapsed, false), force);
        osc_->send(QStringLiteral("/mitti/currentCueTRT"), hms(c.duration, true), force);
    }
    osc_->send(QStringLiteral("/mitti/toggleVideoOutputs"), testBtn_->isChecked() ? 0 : 1, force);
}

void MainWindow::showOscHelp() {
    const QString ip = prefs::oscBindAddress().isEmpty() ? tr("the IP of this PC") : prefs::oscBindAddress();
    QMessageBox box(this);
    box.setWindowTitle(tr("OSC commands (Companion)"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<h3>Bitfocus Companion</h3>"
                   "<ol>"
                   "<li>Here: Settings → Network → <b>Allow control by OSC</b>, choose the network card, and write the "
                   "<b>Companion IP</b> (127.0.0.1 if Companion runs on this PC).</li>"
                   "<li>In Companion: Connections → add <b>Imimot Mitti</b>. Mitti Connection: <b>Manual</b>, IP "
                   "address: <b>%1</b>. Feedback port: <b>%2</b>.</li>"
                   "<li>Use the module's presets: Play / Pause, Play Cue by ID (one button per cue, with its name), "
                   "GOTO 30/20/10, previous / next, time remaining <code>$(mitti:cueTimeLeft)</code>...</li>"
                   "</ol>"
                   "<p>The cue ID is the number in the playlist (or the text you wrote in ID).</p>"
                   "<h3>Commands (UDP port %3)</h3>"
                   "<table cellspacing=4>"
                   "<tr><td><code>/mitti/play</code>, <code>/mitti/togglePlay</code>, <code>/mitti/stop</code> (pause)</td></tr>"
                   "<tr><td><code>/mitti/panic</code> — stop everything (black)</td></tr>"
                   "<tr><td><code>/mitti/playCueWithCueID \"3\"</code>, <code>/mitti/playCueAtIndex 3</code> "
                   "(add <code>ForceCut</code> for no transition)</td></tr>"
                   "<tr><td><code>/mitti/jumpToNextCue</code>, <code>/mitti/jumpToPrevCue</code>, "
                   "<code>/mitti/goto30</code> / <code>goto20</code> / <code>goto10</code></td></tr>"
                   "<tr><td><code>/mitti/mainFader 0..1</code> — master volume</td></tr>"
                   "<tr><td><code>/mitti/testPattern</code> (toggle, or 1 / 0) — test card. Not in the Mitti module: "
                   "send it with a <b>Generic OSC</b> connection.</td></tr>"
                   "</table>"
                   "<p>State sent to Companion: <code>/mitti/cueTimeLeft</code> (-HH:MM:SS), "
                   "<code>cueTimeElapsed</code>, <code>currentCueID</code>, <code>currentCueName</code>, "
                   "<code>togglePlay</code> (1 = playing)...</p>")
                    .arg(ip)
                    .arg(prefs::oscFeedbackPort())
                    .arg(prefs::oscPort()) +
                tr("<h3>Most used buttons (Companion → Buttons → Presets → Imimot Mitti)</h3>"
                   "<table cellspacing=4>"
                   "<tr><td><b>Cues → Play Cue 1, 2, 3...</b></td><td>one button per video, with its name</td></tr>"
                   "<tr><td><b>Transport → Pause / Resume</b></td><td>GO / pause (like Space)</td></tr>"
                   "<tr><td><b>Transport → Play Selected Cue</b></td><td>plays the selected video</td></tr>"
                   "<tr><td><b>Jump &amp; Select → Jump to Next / Previous Cue</b></td><td>next / previous video</td></tr>"
                   "<tr><td><b>Playhead → Goto 30 / 20 / 10</b></td><td>last seconds of the video</td></tr>"
                   "<tr><td><b>Playhead → Rewind</b></td><td>back to the start of the video</td></tr>"
                   "<tr><td><b>Emergency → Panic</b></td><td>stops everything (black)</td></tr>"
                   "<tr><td><b>Cue Status → Cue Time Remaining</b></td><td>countdown on the button (orange / red near the end)</td></tr>"
                   "<tr><td><b>Cue Status → Current Cue / Next Cue</b></td><td>name of the video on air / the next one</td></tr>"
                   "</table>"
                   "<p>Suggested Stream Deck page: top row Play Cue 1–5, middle row Previous · Pause/Resume · "
                   "Next · Panic, bottom row Time Remaining · Goto 30 · Goto 10 · Test card (Generic OSC).</p>"));
    box.exec();
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
    // IDs numéricos antigos (escritos à mão) passam a ser a posição na playlist.
    for (auto& q : project_.cues) {
        bool numeric = false;
        qs(q.label).trimmed().toInt(&numeric);
        if (numeric) q.label.clear();
    }
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
        QMessageBox::question(this, tr("Quit"), tr("Quit VIDEOHOST PLAYER? The engine and the show keep running.")) !=
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
    updateArmed();
    playlist_->setPlaying(playingCue_, playlist_->playingProgress(), playlist_->playingState());
}

void MainWindow::updateSelection() {
    const int i = selectedCue();
    if (i < 0) {
        inspector_->setCue(nullptr, -1, &project_, {}, {}, prefs::normalizeTarget());
        waveform_->setData({}, 0.0, 0.0, false, true);
        waveform_->setGain(0.0, QString());
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
    // A onda mostra o som como vai sair (volume + normalização) e diz o que a normalização faz.
    const double target = prefs::normalizeTarget();
    QString badge;
    if (!q.normalize) {
        badge = tr("Normalization off");
    } else if (!q.loudness) {
        badge = audio.ready ? QString() : tr("Normalization: measuring...");
    } else {
        const double g = normalizationGainDb(*q.loudness, q.peakDb, target);
        const double wanted = std::clamp(target - *q.loudness, -20.0, 20.0);
        badge = tr("Normalized %1%2 dB (%3 → %4 LUFS)")
                    .arg(g >= 0.0 ? QStringLiteral("+") : QString())
                    .arg(g, 0, 'f', 1)
                    .arg(*q.loudness, 0, 'f', 1)
                    .arg(target, 0, 'f', 0);
        if (wanted - g > 0.5) badge += QStringLiteral("  ·  ") + tr("limited by the peak");
    }
    waveform_->setGain(q.gainDb(target), badge);
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
    if (const QString blk = playlist_->selectedBlock(); !blk.isEmpty()) {
        // Bloco selecionado: os vídeos entram no fim desse bloco.
        int last = -1;
        for (int k = 0; k < static_cast<int>(project_.cues.size()); ++k)
            if (qs(project_.cues[static_cast<std::size_t>(k)].block) == blk) last = k;
        addFiles(last >= 0 ? last + 1 : static_cast<int>(project_.cues.size()), files, blk);
        return;
    }
    addFiles(sel >= 0 ? sel + 1 : static_cast<int>(project_.cues.size()), files,
             sel >= 0 ? qs(project_.cues[static_cast<std::size_t>(sel)].block) : QString());
}

void MainWindow::addFiles(int row, const QStringList& paths, const QString& block) {
    if (showMode_ || paths.isEmpty()) return;
    const std::string blk = project_.findBlock(block.toStdString()) ? block.toStdString() : std::string();
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
        q.block = blk;
        // Num bloco, cada vídeo segue para o próximo (o fim do bloco decide se pára).
        if (!blk.empty()) q.pauseAtEnd = false;
        project_.cues.insert(project_.cues.begin() + row + inserted, q);
        media_->info(qs(q.path));
        media_->audio(qs(q.path));
        ++inserted;
    }
    if (inserted == 0) return;
    const std::string firstId = project_.cues[static_cast<std::size_t>(row)].id;
    project_.normalizeBlocks();
    if (const Block* b = project_.findBlock(blk); b && b->collapsed) project_.findBlock(blk)->collapsed = false;
    refreshPlaylist();
    playlist_->selectIndex(project_.cueIndex(firstId));
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
    // Fica no bloco da cue que estava nesse lugar.
    q.block = project_.cues[static_cast<std::size_t>(to)].block;
    const std::string id = q.id;
    project_.cues.erase(project_.cues.begin() + from);
    project_.cues.insert(project_.cues.begin() + to, std::move(q));
    project_.normalizeBlocks();
    refreshPlaylist();
    playlist_->selectIndex(project_.cueIndex(id));
    markChanged();
}

void MainWindow::dropCue(int from, int before, const QString& block) {
    const int n = static_cast<int>(project_.cues.size());
    if (showMode_ || from < 0 || from >= n) return;
    before = std::clamp(before, 0, n);
    Cue q = std::move(project_.cues[static_cast<std::size_t>(from)]);
    const std::string blk = project_.findBlock(block.toStdString()) ? block.toStdString() : std::string();
    const bool same = q.block == blk && (before == from || before == from + 1);
    if (same) {
        project_.cues[static_cast<std::size_t>(from)] = std::move(q);
        return;
    }
    q.block = blk;
    const std::string id = q.id;
    project_.cues.erase(project_.cues.begin() + from);
    const int at = before > from ? before - 1 : before;
    project_.cues.insert(project_.cues.begin() + at, std::move(q));
    project_.normalizeBlocks();
    refreshPlaylist();
    playlist_->selectIndex(project_.cueIndex(id));
    markChanged();
}

void MainWindow::newBlock(int withCue) {
    if (showMode_) return;
    Block b;
    b.id = project_.newBlockId();
    b.name = tr("Block %1").arg(project_.blocks.size() + 1).toStdString();
    project_.blocks.push_back(b);
    if (withCue >= 0 && withCue < static_cast<int>(project_.cues.size()))
        project_.cues[static_cast<std::size_t>(withCue)].block = b.id;
    project_.normalizeBlocks();
    refreshPlaylist();
    playlist_->selectBlock(qs(b.id));
    markChanged();
}

void MainWindow::toggleBlock(const QString& id) {
    Block* b = project_.findBlock(id.toStdString());
    if (!b) return;
    b->collapsed = !b->collapsed;
    refreshPlaylist();
    markChanged();
}

void MainWindow::showBlockMenu(const QString& id, const QPoint& globalPos) {
    Block* b = project_.findBlock(id.toStdString());
    if (!b) return;
    QMenu menu(this);
    const int first = project_.firstInBlock(b->id);
    auto* play = menu.addAction(tr("Play the block"), this, [this, first] { playCue(first); });
    play->setEnabled(first >= 0);
    menu.addAction(b->collapsed ? tr("Show the videos") : tr("Hide the videos"), this, [this, id] { toggleBlock(id); });
    menu.addSeparator();
    QList<QAction*> edits;
    edits << menu.addAction(tr("Rename..."), this, [this, id] {
        Block* blk = project_.findBlock(id.toStdString());
        if (!blk) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Block"), tr("Name:"), QLineEdit::Normal, qs(blk->name), &ok);
        blk = project_.findBlock(id.toStdString());
        if (!ok || !blk) return;
        blk->name = name.trimmed().left(200).toStdString();
        refreshPlaylist();
        markChanged();
    });
    auto* loop = menu.addAction(tr("Loop this block"), this, [this, id](bool on) {
        if (Block* blk = project_.findBlock(id.toStdString())) blk->loop = on;
        refreshPlaylist();
        markChanged();
    });
    loop->setCheckable(true);
    loop->setChecked(b->loop);
    edits << loop;
    auto* endMenu = menu.addMenu(tr("At the end of the block"));
    endMenu->setEnabled(!b->loop);
    auto* group = new QActionGroup(endMenu);
    const std::pair<BlockEnd, QString> ends[] = {{BlockEnd::Pause, tr("Pause on the last frame (Space continues)")},
                                                 {BlockEnd::Stop, tr("Stop (black)")},
                                                 {BlockEnd::Continue, tr("Continue to the next video")}};
    for (const auto& [e, label] : ends) {
        auto* a = endMenu->addAction(label, this, [this, id, e = e] {
            if (Block* blk = project_.findBlock(id.toStdString())) blk->end = e;
            refreshPlaylist();
            markChanged();
        });
        a->setCheckable(true);
        a->setChecked(b->end == e);
        group->addAction(a);
    }
    edits << endMenu->menuAction();
    menu.addSeparator();
    edits << menu.addAction(tr("Remove the block (keep the videos)"), this, [this, id] {
        const std::string bid = id.toStdString();
        for (auto& q : project_.cues)
            if (q.block == bid) q.block.clear();
        auto& v = project_.blocks;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Block& x) { return x.id == bid; }), v.end());
        refreshPlaylist();
        markChanged();
    });
    edits << menu.addAction(tr("Delete the block and its videos"), this, [this, id] {
        const std::string bid = id.toStdString();
        int n = 0;
        for (const auto& q : project_.cues) n += q.block == bid;
        if (n > 0 && QMessageBox::question(this, tr("Block"), tr("Delete the block and its %1 video(s) from the playlist?").arg(n)) !=
                         QMessageBox::Yes)
            return;
        std::set<std::string> gone;
        for (const auto& q : project_.cues)
            if (q.block == bid) gone.insert(q.id);
        project_.cues.erase(std::remove_if(project_.cues.begin(), project_.cues.end(), [&](const Cue& q) { return q.block == bid; }),
                            project_.cues.end());
        for (auto& q : project_.cues)
            if (gone.count(q.gotoCue)) q.gotoCue.clear();
        auto& v = project_.blocks;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Block& x) { return x.id == bid; }), v.end());
        refreshPlaylist();
        markChanged();
    });
    for (auto* a : edits) a->setEnabled(a->isEnabled() && !showMode_);
    menu.exec(globalPos);
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
    Cue c = cue;
    // O ID é a posição na playlist: escrever um número move a cue para esse lugar
    // (as outras renumeram-se sozinhas). Texto (ex. "INTRO") fica como etiqueta.
    bool numeric = false;
    const int wanted = qs(c.label).trimmed().toInt(&numeric);
    if (numeric) c.label.clear();
    project_.cues[static_cast<std::size_t>(i)] = c;
    const int n = static_cast<int>(project_.cues.size());
    if (numeric && wanted >= 1 && std::clamp(wanted - 1, 0, n - 1) != i) {
        moveCue(i, std::clamp(wanted - 1, 0, n - 1));
        updateSelection();
        return;
    }
    playlist_->refresh();
    waveform_->setInOut(c.inPoint, c.outPoint);
    if (numeric) updateSelection(); // o campo ID mostra outra vez o número da posição
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
    edit << menu.addAction(tr("New block"), this, [this] { newBlock(-1); });
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
        // Blocos
        auto* blocks = menu.addMenu(tr("Block"));
        edit << blocks->menuAction();
        blocks->addAction(tr("New block with this video"), this, [this, index] { newBlock(index); });
        const std::string cur = project_.cues[static_cast<std::size_t>(index)].block;
        if (!project_.blocks.empty()) blocks->addSeparator();
        for (const auto& b : project_.blocks) {
            const std::string bid = b.id;
            auto* a = blocks->addAction(b.name.empty() ? qs(b.id) : qs(b.name), this, [this, index, bid] {
                // Vai para o fim do bloco (bloco vazio: fim da lista).
                int last = -1;
                for (int k = 0; k < static_cast<int>(project_.cues.size()); ++k)
                    if (project_.cues[static_cast<std::size_t>(k)].block == bid) last = k;
                dropCue(index, last >= 0 ? last + 1 : static_cast<int>(project_.cues.size()), qs(bid));
            });
            a->setCheckable(true);
            a->setChecked(cur == bid);
        }
        if (!cur.empty()) blocks->addAction(tr("Out of the block"), this, [this, index] {
            int last = -1;
            const std::string bid = project_.cues[static_cast<std::size_t>(index)].block;
            for (int k = 0; k < static_cast<int>(project_.cues.size()); ++k)
                if (project_.cues[static_cast<std::size_t>(k)].block == bid) last = k;
            dropCue(index, last + 1, QString());
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

void MainWindow::playCue(int index, bool forceCut) {
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
    c.cut = forceCut || (prefs::shiftSkipsTransitions() && (QApplication::keyboardModifiers() & Qt::ShiftModifier));
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
    // Parado com uma cue em CUE (vermelha): ESPAÇO toca essa (e, no fim, a seguinte fica em CUE).
    if (!armedCueId_.empty() && project_.cueIndex(armedCueId_) >= 0) {
        fireArmedCue();
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
    applyOsc();
    applyTally();
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
        if (meter_) static_cast<LevelMeter*>(meter_)->setLevels(0.0, 0.0);
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
    followTallyPlayback(s);
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
    preview_->setMediaFps(playingCue_ >= 0 && playingCue_ < static_cast<int>(project_.cues.size())
                              ? media_->info(qs(project_.cues[static_cast<std::size_t>(playingCue_)].path)).fps
                              : 0.0);

    QStringList problems;
    for (const auto& o : s.outputs)
        if (!o.ok || !o.error.empty()) problems << tr("Output %1: %2").arg(qs(o.id), qs(o.error));
    if (!s.error.empty()) problems << qs(s.error);
    if (!s.audioError.empty()) problems << tr("Audio: %1").arg(qs(s.audioError));
    for (const auto& w : s.warnings) problems << qs(w);

    QStringList info; // os fps estão no rodapé, à direita (updatePerf)
    if (s.audioOk) info << QStringLiteral("🔊 ") + qs(s.audioDevice);
    if (s.recovered) info << tr("Engine was restarted and the show resumed");
    if (!problems.isEmpty()) info << QStringLiteral("⚠ ") + problems.join(QStringLiteral("; "));
    infoLabel_->setText(info.join(QStringLiteral("  |  ")));
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
    // Mudança de língua com "reabrir já": fecha (pode ser cancelado, ex. projecto por gravar)
    // e o main() volta a abrir o Studio depois de esta janela desaparecer.
    if (qApp->property("vthostRestart").toBool() && !close()) qApp->setProperty("vthostRestart", false);
    // Outra placa gráfica: o motor reinicia e retoma o show (o watchdog relança-o com recuperação).
    if (qApp->property("vthostRestartEngine").toBool()) {
        qApp->setProperty("vthostRestartEngine", false);
        restartEngine();
    }
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
                                                      tr("VIDEOHOST PLAYER projects (*.vthost)"));
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
                                                tr("VIDEOHOST PLAYER projects (*.vthost)"));
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

void MainWindow::restartEngine() {
    if (!engine_->isConnected()) {
        startEngine(); // parado: arranca já com a placa nova
        return;
    }
    protocol::Command c;
    c.type = protocol::CommandType::Restart;
    engine_->send(c, [this](const protocol::ParsedResponse& r) {
        if (!r.ok) QMessageBox::warning(this, tr("Engine"), qs(r.error));
        else infoLabel_->setText(tr("Restarting the engine..."));
    });
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
    const QString bind = prefs::countdownBindAddress();
    if (!countdownServer_->isRunning() || countdownServer_->port() != port || countdownServer_->bindAddress() != bind) {
        QString err;
        if (!countdownServer_->start(port, bind, &err)) {
            webLabel_->setText(bind.isEmpty() ? tr("Countdown web: port %1 unavailable").arg(port)
                                              : tr("Countdown web: network card %1 not available").arg(bind));
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
        auto* hint = new QLabel(tr("If Windows asks, allow VIDEOHOST PLAYER on private networks. "
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
    QString t = QStringLiteral("VIDEOHOST PLAYER - %1").arg(qs(project_.name));
    if (dirty_) t += QStringLiteral(" *");
    if (showMode_) t += QStringLiteral("  [") + tr("SHOW MODE") + QStringLiteral("]");
    setWindowTitle(t);
}
