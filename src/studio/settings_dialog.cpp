#include "settings_dialog.h"

#include <QApplication>

#include "app_info.h"
#include "engine_client.h"
#include "prefs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
const QString kLanguageKey = QStringLiteral("language");
const char* kAutoUpdateKey = "updates/checkOnStartup";

QString currentLanguage() {
    QString lang = QSettings().value(kLanguageKey).toString();
    if (lang.isEmpty())
        lang = QLocale::system().language() == QLocale::Portuguese ? QStringLiteral("pt_PT") : QStringLiteral("en");
    return lang;
}

QString link(const QString& url, const QString& text) {
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(url, text.toHtmlEscaped());
}

QLabel* note(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: gray;"));
    return l;
}
} // namespace

bool SettingsDialog::checkUpdatesOnStartup() { return prefs::get(kAutoUpdateKey, true); }

SettingsDialog::SettingsDialog(QWidget* parent, int initialTab, EngineClient* engine) : QDialog(parent) {
    setWindowTitle(tr("Preferences"));
    setMinimumSize(620, 460);
    auto* tabs = new QTabWidget(this);

    // Ajudantes: cria o controlo, carrega o valor e regista como o gravar.
    auto check = [this](QWidget* parent, const QString& text, const char* key, bool def) {
        auto* c = new QCheckBox(text, parent);
        c->setChecked(prefs::get(key, def));
        savers_.push_back([c, key] { prefs::set(key, c->isChecked()); });
        return c;
    };
    auto dspin = [this](QWidget* parent, const char* key, double def, double min, double max, const QString& suffix) {
        auto* s = new QDoubleSpinBox(parent);
        s->setRange(min, max);
        s->setDecimals(2);
        s->setSuffix(suffix);
        s->setValue(prefs::get(key, def));
        savers_.push_back([s, key] { prefs::set(key, s->value()); });
        return s;
    };
    auto combo = [this](QWidget* parent, const char* key, const QVariant& def,
                        const std::vector<std::pair<QString, QVariant>>& items) {
        auto* c = new QComboBox(parent);
        for (const auto& [label, value] : items) c->addItem(label, value);
        // Compara como texto: no Windows o QSettings (registo) devolve "true"/"30" como strings.
        const QString cur = QSettings().value(QString::fromLatin1(key), def).toString();
        int idx = 0;
        for (int i = 0; i < c->count(); ++i)
            if (c->itemData(i).toString() == cur) idx = i;
        c->setCurrentIndex(idx);
        savers_.push_back([c, key] { QSettings().setValue(QString::fromLatin1(key), c->currentData()); });
        return c;
    };

    // ---- Geral
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        language_ = new QComboBox(page);
        language_->addItem(QStringLiteral("Português"), QStringLiteral("pt_PT"));
        language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
        initialLanguage_ = currentLanguage();
        language_->setCurrentIndex(std::max(0, language_->findData(initialLanguage_)));
        f->addRow(tr("Language:"), language_);
        f->addRow(QString(), check(page, tr("Open the last project at startup"), "general/openLastProject", true));
        f->addRow(QString(), check(page, tr("Start the engine when VIDEOHOST PLAYER opens"), "general/autoStartEngine", true));
        f->addRow(QString(), check(page, tr("Ask before quitting"), "general/confirmQuit", true));
        f->addRow(QString(), note(tr("Closing VIDEOHOST PLAYER never stops the engine: the show keeps running."), page));
        tabs->addTab(page, tr("General"));
    }

    // ---- Playlist
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        f->addRow(QString(), check(page, tr("Select the cue on air automatically"), "playlist/selectionFollows", true));
        f->addRow(QString(), check(page, tr("Hold SHIFT to play without transition (cut)"), "playlist/shiftCut", true));
        f->addRow(QString(), check(page, tr("New cues pause at the end (hold the last frame)"), "playlist/newPauseAtEnd", false));
        f->addRow(tr("Duration of images added to the playlist:"),
                  dspin(page, "playlist/imageDuration", 10.0, 0.1, 3600.0, tr(" s")));
        f->addRow(tr("Fade time when clicking a fade icon:"), dspin(page, "playlist/defaultFade", 1.0, 0.1, 60.0, tr(" s")));
        f->addRow(QString(), note(tr("SPACE plays/pauses. ENTER or double-click plays the selected cue. Without \"pause at "
                                     "end\", the playlist continues to the next cue (or to the \"Goto\" cue)."),
                                  page));
        tabs->addTab(page, tr("Playlist"));
    }

    // ---- Áudio
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        auto* device = new QComboBox(page);
        device->addItem(tr("Windows default sound card"), QString());
        const QString current = prefs::audioDevice();
        if (!current.isEmpty()) device->addItem(tr("Saved device"), current);
        device->setCurrentIndex(device->count() - 1);
        savers_.push_back([device] { prefs::set("audio/device", device->currentData().toString()); });
        if (engine && engine->isConnected()) {
            vthost::protocol::Command c;
            c.type = vthost::protocol::CommandType::ListAudioDevices;
            QPointer<QComboBox> box(device);
            engine->send(c, [box, current](const vthost::protocol::ParsedResponse& r) {
                if (!box || !r.ok || !r.audioDevices) return;
                box->clear();
                box->addItem(SettingsDialog::tr("Windows default sound card"), QString());
                for (const auto& d : *r.audioDevices)
                    box->addItem(QString::fromStdString(d.name) + (d.isDefault ? SettingsDialog::tr("  (default)") : QString()),
                                   QString::fromStdString(d.id));
                box->setCurrentIndex(std::max(0, box->findData(current)));
            });
        }
        f->addRow(tr("Sound card:"), device);
        f->addRow(tr("Normalization target:"), dspin(page, "audio/normalizeTarget", -16.0, -40.0, -5.0, tr(" LUFS")));
        f->addRow(QString(), note(tr("Each cue is analysed when added (loudness in LUFS, EBU R128). With \"Normalize "
                                     "loudness\" on, quieter and louder videos are brought to the same level, without "
                                     "letting peaks go above -1 dBFS. -16 LUFS suits most events; -23 LUFS is the "
                                     "broadcast standard."),
                                  page));
        tabs->addTab(page, tr("Audio"));
    }

    // ---- Motor e saídas
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        f->addRow(QString(), check(page, tr("Synchronize with the screen refresh (V-Sync)"), "engine/vsync", true));
        f->addRow(tr("Workspace preview frame rate:"),
                  combo(page, "engine/previewFps", 30,
                        {{tr("15 fps (lighter)"), 15}, {tr("30 fps"), 30}, {tr("60 fps"), 60}, {tr("Off"), 0}}));
        f->addRow(tr("Engine priority:"),
                  combo(page, "engine/priority", QStringLiteral("above_normal"),
                        {{tr("Normal"), QStringLiteral("normal")},
                         {tr("Above normal (recommended)"), QStringLiteral("above_normal")},
                         {tr("High"), QStringLiteral("high")}}));
        f->addRow(QString(), note(tr("The mouse cursor is always hidden on fullscreen outputs, and Windows sleep and "
                                     "screensaver are blocked while the engine runs."),
                                  page));
        tabs->addTab(page, tr("Engine && outputs"));
    }

    // ---- Show Mode
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        f->addRow(QString(), check(page, tr("Ask for confirmation before leaving Show Mode"), "show/confirmLeave", true));
        f->addRow(QString(), note(tr("In Show Mode editing is locked, the engine cannot be stopped and updates are not "
                                     "installed. Playback controls (play, pause, next, GOTO) keep working."),
                                  page));
        tabs->addTab(page, tr("Show Mode"));
    }

    // ---- Atualizações
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        f->addRow(QString(), check(page, tr("Check for updates automatically (at startup and every 30 minutes)"), kAutoUpdateKey, true));
        auto* checkNow = new QPushButton(tr("Check for updates now"), page);
        connect(checkNow, &QPushButton::clicked, this, &SettingsDialog::checkForUpdatesRequested);
        f->addRow(QString(), checkNow);
        f->addRow(QString(), note(tr("Updates are never installed while Show Mode is on."), page));
        tabs->addTab(page, tr("Updates"));
    }

    // ---- Countdown (preview, janela de countdown e página web)
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        f->addRow(tr("Orange when less than:"), dspin(page, "countdown/warnSeconds", 30.0, 0.0, 3600.0, tr(" s")));
        f->addRow(tr("Red when less than:"), dspin(page, "countdown/dangerSeconds", 10.0, 0.0, 3600.0, tr(" s")));
        f->addRow(QString(), note(tr("The countdown shows the time left of the cue on air (all repetitions). It appears "
                                     "on the preview, in the countdown window (Window menu) and on the web page "
                                     "(Preferences → Network)."),
                                  page));
        tabs->addTab(page, tr("Countdown"));
    }

    // ---- Rede: placa de rede e serviços na rede (página do countdown; no futuro SMPTE, Companion...)
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        // Placa de rede: os serviços só ficam acessíveis na rede dessa placa (ex. a rede do
        // público/técnicos), nunca nas outras redes do PC (sistemas de controlo, etc.).
        auto nicCombo = [this, page](const QString& savedNic) {
            auto* nic = new QComboBox(page);
            nic->addItem(tr("All network cards"), QString());
            bool found = savedNic.isEmpty();
            for (const QNetworkInterface& ni : QNetworkInterface::allInterfaces()) {
                if (!(ni.flags() & QNetworkInterface::IsUp) || (ni.flags() & QNetworkInterface::IsLoopBack)) continue;
                for (const QNetworkAddressEntry& e : ni.addressEntries()) {
                    const QHostAddress a = e.ip();
                    if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLinkLocal()) continue;
                    nic->addItem(QStringLiteral("%1 — %2").arg(ni.humanReadableName(), a.toString()), a.toString());
                    found = found || a.toString() == savedNic;
                }
            }
            if (!found) nic->addItem(tr("%1 (not connected now)").arg(savedNic), savedNic);
            nic->setCurrentIndex(std::max(0, nic->findData(savedNic)));
            return nic;
        };
        auto* nic = nicCombo(prefs::countdownBindAddress());
        savers_.push_back([nic] { prefs::set("countdown/bindAddress", nic->currentData().toString()); });
        f->addRow(tr("Network card:"), nic);
        f->addRow(QString(), note(tr("Only this network card is used: people on the other networks of this PC "
                                     "(e.g. a control system) cannot reach VIDEOHOST PLAYER."),
                                  page));
        auto* web = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Countdown web page")), page);
        f->addRow(web);
        f->addRow(QString(), check(page, tr("Show the countdown on a web page for the local network"),
                                   "countdown/webEnabled", true));
        auto* port = new QSpinBox(page);
        port->setRange(1024, 65535);
        port->setValue(prefs::countdownWebPort());
        savers_.push_back([port] { prefs::set("countdown/webPort", port->value()); });
        f->addRow(tr("Port:"), port);
        f->addRow(QString(), note(tr("Open Window → Countdown on the network to see the address (e.g. "
                                     "http://192.168.1.20:8088). The page is read-only: it cannot control the show."),
                                  page));

        // OSC para o Bitfocus Companion (protocolo do Mitti: o módulo "Imimot Mitti" funciona).
        f->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Bitfocus Companion (OSC)")), page));
        f->addRow(QString(), check(page, tr("Allow control by OSC (Companion, Stream Deck...)"), "osc/enabled", false));
        auto* oscNic = nicCombo(prefs::oscBindAddress());
        savers_.push_back([oscNic] { prefs::set("osc/bindAddress", oscNic->currentData().toString()); });
        f->addRow(tr("Network card (OSC):"), oscNic);
        auto* oscPort = new QSpinBox(page);
        oscPort->setRange(1024, 65535);
        oscPort->setValue(prefs::oscPort());
        savers_.push_back([oscPort] { prefs::set("osc/port", oscPort->value()); });
        f->addRow(tr("Receive on port:"), oscPort);
        auto* fbHost = new QLineEdit(prefs::oscFeedbackHost(), page);
        fbHost->setPlaceholderText(tr("e.g. 192.168.1.50 (127.0.0.1 if Companion runs on this PC)"));
        savers_.push_back([fbHost] { prefs::set("osc/feedbackHost", fbHost->text().trimmed()); });
        f->addRow(tr("Companion IP:"), fbHost);
        auto* fbPort = new QSpinBox(page);
        fbPort->setRange(1024, 65535);
        fbPort->setValue(prefs::oscFeedbackPort());
        savers_.push_back([fbPort] { prefs::set("osc/feedbackPort", fbPort->value()); });
        f->addRow(tr("Send state to port:"), fbPort);
        f->addRow(QString(), check(page, tr("Accept commands only from the Companion IP"), "osc/onlyFromFeedbackHost", true));
        f->addRow(QString(), note(tr("In Companion add the connection \"Imimot Mitti\" with the IP of this PC (Manual) "
                                     "and feedback port 51001. Keep the receive port at 51000: the Mitti module always sends there. "
                                     "Its presets work: play, pause, play cue by ID, GOTO, time remaining... "
                                     "Help → OSC commands lists everything."),
                                  page));
        tabs->addTab(page, tr("Network"));
    }

    // ---- Atalhos (só leitura): todos os atalhos do VIDEOHOST PLAYER.
    {
        auto* page = new QWidget(tabs);
        auto* vl = new QVBoxLayout(page);
        auto* tree = new QTreeWidget(page);
        tree->setColumnCount(2);
        tree->setHeaderLabels({tr("Action"), tr("Shortcut")});
        tree->setRootIsDecorated(true);
        tree->setSelectionMode(QAbstractItemView::NoSelection);
        tree->setFocusPolicy(Qt::NoFocus);
        auto keys = [](const QKeySequence& k) { return k.toString(QKeySequence::NativeText); };
        auto group = [tree](const QString& name) {
            auto* g = new QTreeWidgetItem(tree, {name});
            QFont f = g->font(0);
            f.setBold(true);
            g->setFont(0, f);
            g->setFirstColumnSpanned(true);
            g->setExpanded(true);
            return g;
        };
        auto row = [](QTreeWidgetItem* g, const QString& action, const QString& shortcut) {
            new QTreeWidgetItem(g, {action, shortcut});
        };
        const QString shift = QStringLiteral("Shift");

        auto* gShow = group(tr("Playback"));
        row(gShow, tr("Play / pause (stopped: starts the selected cue)"), keys(QKeySequence(Qt::Key_Space)));
        row(gShow, tr("Play the selected cue"), tr("Enter / double-click"));
        row(gShow, tr("Play without transition (cut)"), tr("%1 + Enter / double-click").arg(shift));
        row(gShow, tr("Stop (black)"), keys(QKeySequence(Qt::Key_Escape)));
        row(gShow, tr("Next cue"), keys(QKeySequence(Qt::CTRL | Qt::Key_Right)));
        row(gShow, tr("Previous cue"), keys(QKeySequence(Qt::CTRL | Qt::Key_Left)));
        row(gShow, tr("Go to the last 30 / 20 / 10 seconds"),
            keys(QKeySequence(Qt::CTRL | Qt::Key_3)) + QStringLiteral(" / ") + keys(QKeySequence(Qt::CTRL | Qt::Key_2)) +
                QStringLiteral(" / ") + keys(QKeySequence(Qt::CTRL | Qt::Key_1)));
        row(gShow, tr("Loop the playlist"), keys(QKeySequence(Qt::CTRL | Qt::Key_R)));
        row(gShow, tr("Show Mode (lock editing)"), keys(QKeySequence(Qt::CTRL | Qt::Key_L)));
        row(gShow, tr("Countdown window (Window menu)"), keys(QKeySequence(Qt::CTRL | Qt::Key_T)));
        row(gShow, tr("Countdown window full screen"), tr("Double-click or %1").arg(keys(QKeySequence(Qt::Key_F11))));

        auto* gFile = group(tr("File"));
        row(gFile, tr("New project"), keys(QKeySequence(QKeySequence::New)));
        row(gFile, tr("Open project"), keys(QKeySequence(QKeySequence::Open)));
        row(gFile, tr("Save"), keys(QKeySequence(QKeySequence::Save)));
        row(gFile, tr("Save as"), keys(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)));
        row(gFile, tr("Add media"), keys(QKeySequence(Qt::CTRL | Qt::Key_I)));
        row(gFile, tr("Preferences"), keys(QKeySequence(Qt::CTRL | Qt::Key_Comma)));
        row(gFile, tr("Quit"), keys(QKeySequence(Qt::CTRL | Qt::Key_Q)));

        auto* gEdit = group(tr("Edit"));
        row(gEdit, tr("Undo"), keys(QKeySequence(QKeySequence::Undo)));
        row(gEdit, tr("Redo"), keys(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)) + QStringLiteral("  /  ") +
                                   keys(QKeySequence(Qt::CTRL | Qt::Key_Y)));
        row(gEdit, tr("Delete the selected cue"), keys(QKeySequence(Qt::Key_Delete)));
        row(gEdit, tr("Duplicate the selected cue"), keys(QKeySequence(Qt::CTRL | Qt::Key_D)));
        row(gEdit, tr("Outputs"), keys(QKeySequence(Qt::CTRL | Qt::Key_Semicolon)));
        row(gEdit, tr("Reorder cues"), tr("Drag in the playlist"));
        row(gEdit, tr("Add files"), tr("Drag from Windows Explorer into the playlist"));
        row(gEdit, tr("Set In / Out"), tr("Drag the white markers on the waveform"));

        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        vl->addWidget(tree);
        tabs->addTab(page, tr("Shortcuts"));
    }

    // ---- Sobre (última secção)
    {
        auto* about = new QWidget(tabs);
        auto* al = new QVBoxLayout(about);
        auto* title = new QLabel(about);
        title->setPixmap(QPixmap(QStringLiteral(":/branding/vthost-logo.png")).scaledToWidth(380, Qt::SmoothTransformation));
        auto* appName = new QLabel(appinfo::kName, about);
        appName->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold; letter-spacing: 2px;"));
        auto* version = new QLabel(tr("Version %1").arg(appinfo::kVersion), about);
        auto* desc = new QLabel(tr("Media server and video player for live shows."), about);
        auto* author = new QLabel(tr("Created by %1").arg(QStringLiteral("<b>%1</b>").arg(appinfo::kAuthor)), about);
        auto* links = new QLabel(QStringLiteral("GitHub: %1<br>%2: %3")
                                     .arg(link(appinfo::kAuthorUrl, QStringLiteral("github.com/NunesDanielPT")),
                                          tr("Project"), link(appinfo::kRepoUrl, appinfo::kUpdateRepo)),
                                 about);
        links->setOpenExternalLinks(true);
        links->setTextInteractionFlags(Qt::TextBrowserInteraction);
        auto* copyright =
            new QLabel(tr("© %1 %2. All rights reserved.").arg(QStringLiteral("2026"), appinfo::kAuthor), about);
        auto* thirdParty = new QLabel(
            tr("Uses %1 (LGPL), %2 (LGPL) and %3 (MIT).")
                .arg(link(QStringLiteral("https://ffmpeg.org"), QStringLiteral("FFmpeg")),
                     link(QStringLiteral("https://www.qt.io"), QStringLiteral("Qt")),
                     link(QStringLiteral("https://github.com/nlohmann/json"), QStringLiteral("nlohmann/json"))),
            about);
        thirdParty->setOpenExternalLinks(true);
        thirdParty->setWordWrap(true);
        thirdParty->setStyleSheet(QStringLiteral("color: gray;"));
        for (QLabel* l : {title, version, desc, author, links, copyright, thirdParty}) l->setAlignment(Qt::AlignHCenter);
        al->addStretch();
        al->addWidget(title);
        al->addWidget(appName);
        al->addWidget(version);
        al->addSpacing(6);
        al->addWidget(desc);
        al->addSpacing(12);
        al->addWidget(author);
        al->addWidget(links);
        al->addSpacing(12);
        al->addWidget(copyright);
        al->addWidget(thirdParty);
        al->addStretch();
        tabs->addTab(about, tr("About"));
    }

    tabs->setCurrentIndex(initialTab < 0 ? tabs->count() - 1 : std::min(initialTab, tabs->count() - 1));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

void SettingsDialog::apply() {
    for (const auto& save : savers_) save();
    const QString lang = language_->currentData().toString();
    if (lang != initialLanguage_) {
        QSettings().setValue(kLanguageKey, lang);
        // A interface só muda de língua ao reabrir: oferece reabrir já (o motor e o show continuam).
        if (QMessageBox::question(this, tr("Language"),
                                  tr("The language changes when VIDEOHOST PLAYER opens again. Restart it now? "
                                     "The engine and the show keep running."),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
            qApp->setProperty("vthostRestart", true);
    }
    emit preferencesChanged();
}
