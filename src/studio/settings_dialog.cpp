#include "settings_dialog.h"

#include "app_info.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
const QString kLanguageKey = QStringLiteral("language");
const QString kAutoUpdateKey = QStringLiteral("updates/checkOnStartup");

QString currentLanguage() {
    QString lang = QSettings().value(kLanguageKey).toString();
    if (lang.isEmpty())
        lang = QLocale::system().language() == QLocale::Portuguese ? QStringLiteral("pt_PT") : QStringLiteral("en");
    return lang;
}

QString link(const QString& url, const QString& text) {
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(url, text.toHtmlEscaped());
}
} // namespace

bool SettingsDialog::checkUpdatesOnStartup() { return QSettings().value(kAutoUpdateKey, true).toBool(); }

SettingsDialog::SettingsDialog(QWidget* parent, int initialTab) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(520);
    auto* tabs = new QTabWidget(this);

    // ---- Geral
    auto* general = new QWidget(tabs);
    auto* form = new QFormLayout(general);
    language_ = new QComboBox(general);
    language_->addItem(QStringLiteral("Português"), QStringLiteral("pt_PT"));
    language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    initialLanguage_ = currentLanguage();
    language_->setCurrentIndex(qMax(0, language_->findData(initialLanguage_)));
    form->addRow(tr("Language:"), language_);

    autoUpdate_ = new QCheckBox(tr("Check for updates when VTHOST Studio starts"), general);
    autoUpdate_->setChecked(checkUpdatesOnStartup());
    form->addRow(tr("Updates:"), autoUpdate_);
    auto* checkNow = new QPushButton(tr("Check for updates now"), general);
    connect(checkNow, &QPushButton::clicked, this, &SettingsDialog::checkForUpdatesRequested);
    form->addRow(QString(), checkNow);
    auto* note = new QLabel(tr("Updates are never installed while Show Mode is on."), general);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray;"));
    form->addRow(QString(), note);
    tabs->addTab(general, tr("General"));

    // ---- Sobre (última secção)
    auto* about = new QWidget(tabs);
    auto* al = new QVBoxLayout(about);
    auto* title = new QLabel(QStringLiteral("<span style=\"font-size:28px; font-weight:bold;\">VTHOST</span>"), about);
    auto* version = new QLabel(tr("Version %1").arg(appinfo::kVersion), about);
    auto* desc = new QLabel(tr("Media server and video player for live shows."), about);
    auto* author = new QLabel(tr("Created by %1").arg(QStringLiteral("<b>%1</b>").arg(appinfo::kAuthor)), about);
    auto* links = new QLabel(QStringLiteral("GitHub: %1<br>%2: %3")
                                 .arg(link(appinfo::kAuthorUrl, QStringLiteral("github.com/NunesDanielPT")),
                                      tr("Project"), link(appinfo::kRepoUrl, appinfo::kUpdateRepo)),
                             about);
    links->setOpenExternalLinks(true);
    links->setTextInteractionFlags(Qt::TextBrowserInteraction);
    auto* copyright = new QLabel(tr("© %1 %2. All rights reserved.").arg(QStringLiteral("2026"), appinfo::kAuthor), about);
    auto* thirdParty = new QLabel(
        tr("Uses %1 (LGPL), %2 (LGPL) and %3 (MIT).")
            .arg(link(QStringLiteral("https://ffmpeg.org"), QStringLiteral("FFmpeg")),
                 link(QStringLiteral("https://www.qt.io"), QStringLiteral("Qt")),
                 link(QStringLiteral("https://github.com/nlohmann/json"), QStringLiteral("nlohmann/json"))),
        about);
    thirdParty->setOpenExternalLinks(true);
    thirdParty->setWordWrap(true);
    thirdParty->setStyleSheet(QStringLiteral("color: gray;"));
    for (QLabel* l : {title, version, desc, author, links, copyright}) l->setAlignment(Qt::AlignHCenter);
    thirdParty->setAlignment(Qt::AlignHCenter);
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

    tabs->setCurrentIndex(qBound(0, initialTab, tabs->count() - 1));

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
    QSettings s;
    s.setValue(kAutoUpdateKey, autoUpdate_->isChecked());
    const QString lang = language_->currentData().toString();
    if (lang != initialLanguage_) {
        s.setValue(kLanguageKey, lang);
        QMessageBox::information(this, tr("Language"),
                                 tr("The language will change the next time VTHOST Studio starts."));
    }
}
