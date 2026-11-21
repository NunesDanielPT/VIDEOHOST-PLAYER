#include "settings_dialog.h"

#include "app_info.h"
#include "prefs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
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

SettingsDialog::SettingsDialog(QWidget* parent, int initialTab) : QDialog(parent) {
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
        f->addRow(QString(), check(page, tr("Start the engine when VTHOST Studio opens"), "general/autoStartEngine", false));
        f->addRow(QString(), check(page, tr("Ask before quitting"), "general/confirmQuit", true));
        f->addRow(QString(), note(tr("Closing VTHOST Studio never stops the engine: the show keeps running."), page));
        tabs->addTab(page, tr("General"));
    }

    // ---- Workspace
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        f->addRow(tr("Workspace origin:"),
                  combo(page, "workspace/originTopLeft", false,
                        {{tr("Center of the canvas"), false}, {tr("Top-left corner"), true}}));
        f->addRow(QString(), check(page, tr("Show the outline of the selected layer"), "workspace/showLayerOutline", true));
        auto* depth = new QComboBox(page);
        depth->addItem(tr("8 bits"));
        depth->addItem(tr("10 bits HDR (coming soon)"));
        depth->setEnabled(false);
        f->addRow(tr("Color depth:"), depth);
        auto* aa = new QComboBox(page);
        aa->addItem(tr("Linear filtering"));
        aa->setEnabled(false);
        f->addRow(tr("Anti-aliasing:"), aa);
        tabs->addTab(page, tr("Workspace"));
    }

    // ---- Board (Dashboard)
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        f->addRow(QString(), check(page, tr("Show the column index"), "board/showColumnIndex", true));
        f->addRow(tr("At the end of the board:"),
                  combo(page, "board/loopBoard", false,
                        {{tr("Stay on the last column"), false}, {tr("Go back to the first column"), true}}));
        f->addRow(QString(), check(page, tr("Hold SHIFT to skip transitions"), "board/shiftSkipsTransitions", true));
        f->addRow(tr("Default transition for new columns:"),
                  dspin(page, "board/defaultFade", 1.0, 0.0, 60.0, QStringLiteral(" s")));
        f->addRow(QString(), note(tr("SPACE launches the next column, ALT+SPACE the previous one."), page));
        tabs->addTab(page, tr("Board"));
    }

    // ---- Timeline
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        f->addRow(QString(), check(page, tr("Snap media to useful timings (hold SHIFT to invert)"), "timeline/snap", true));
        f->addRow(QString(), check(page, tr("Follow the playhead while playing"), "timeline/followPlayhead", true));
        f->addRow(tr("Duration of images dropped on the timeline:"),
                  dspin(page, "timeline/imageDuration", 10.0, 0.1, 3600.0, QStringLiteral(" s")));
        f->addRow(QString(), note(tr("SPACE plays or stops the timeline. CTRL+SHIFT+X cuts the selected media."), page));
        tabs->addTab(page, tr("Timeline"));
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
                                     "installed. Live controls (GO, opacity, visibility, properties) keep working."),
                                  page));
        tabs->addTab(page, tr("Show Mode"));
    }

    // ---- Atualizações
    {
        auto* page = new QWidget(tabs);
        auto* f = new QFormLayout(page);
        f->addRow(QString(), check(page, tr("Check for updates when VTHOST Studio starts"), kAutoUpdateKey, true));
        auto* checkNow = new QPushButton(tr("Check for updates now"), page);
        connect(checkNow, &QPushButton::clicked, this, &SettingsDialog::checkForUpdatesRequested);
        f->addRow(QString(), checkNow);
        f->addRow(QString(), note(tr("Updates are never installed while Show Mode is on."), page));
        tabs->addTab(page, tr("Updates"));
    }

    // ---- Sobre (última secção)
    {
        auto* about = new QWidget(tabs);
        auto* al = new QVBoxLayout(about);
        auto* title = new QLabel(about);
        title->setPixmap(QPixmap(QStringLiteral(":/branding/vthost-logo.png")).scaledToWidth(380, Qt::SmoothTransformation));
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
        QMessageBox::information(this, tr("Language"),
                                 tr("The language will change the next time VTHOST Studio starts."));
    }
    emit preferencesChanged();
}
