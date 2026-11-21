#include "update_dialog.h"

#include "app_info.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QDate>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog(const UpdateInfo& info, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Update available"));
    auto* l = new QVBoxLayout(this);
    auto* title = new QLabel(tr("New version %1 available").arg(info.version), this);
    title->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: bold;"));
    l->addWidget(title);
    auto* sub = new QLabel(tr("You have version %1. What changes:").arg(appinfo::kVersion), this);
    sub->setStyleSheet(QStringLiteral("color: #aaa;"));
    l->addWidget(sub);
    auto* notes = new QTextBrowser(this);
    notes->setOpenExternalLinks(true);
    notes->setHtml(notesHtml(info.changes));
    notes->setMinimumHeight(220);
    l->addWidget(notes, 1);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    l->addWidget(progress_);
    status_ = new QLabel(tr("The update takes a minute or two. The engine stops while it installs and VIDEOHOST PLAYER "
                            "opens again at the end. Saved projects are not affected."),
                         this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: #aaa;"));
    l->addWidget(status_);
    auto* row = new QHBoxLayout;
    row->addStretch();
    later_ = new QPushButton(tr("Later"), this);
    primary_ = new QPushButton(tr("Update now"), this);
    primary_->setDefault(true);
    primary_->setStyleSheet(QStringLiteral("QPushButton { background: #2a7fd4; color: white; padding: 5px 16px; border-radius: 3px; }"
                                           "QPushButton:disabled { background: #335; color: #999; }"));
    row->addWidget(later_);
    row->addWidget(primary_);
    l->addLayout(row);
    connect(later_, &QPushButton::clicked, this, &QDialog::reject);
    connect(primary_, &QPushButton::clicked, this, [this] {
        if (ready_) {
            emit installNowRequested();
            return;
        }
        primary_->setEnabled(false);
        status_->setText(tr("Downloading..."));
        emit updateNowRequested();
    });
    resize(560, 460);
}

void UpdateDialog::setProgress(qint64 received, qint64 total) {
    if (total <= 0) return;
    const int pct = static_cast<int>(received * 100 / total);
    progress_->setValue(pct);
    status_->setText(tr("Downloading... %1%").arg(pct));
}

void UpdateDialog::setFailed(const QString& error) {
    status_->setText(error);
    status_->setStyleSheet(QStringLiteral("color: #e66;"));
    primary_->setEnabled(true);
    primary_->setText(tr("Try again"));
}

void UpdateDialog::setReady() {
    ready_ = true;
    progress_->setValue(100);
    status_->setStyleSheet(QStringLiteral("color: #aaa;"));
    status_->setText(tr("Downloaded and verified. VIDEOHOST PLAYER closes, installs and opens again."));
    primary_->setEnabled(true);
    primary_->setText(tr("Restart and install now"));
}

QString UpdateDialog::notesHtml(const QList<ReleaseNotes>& changes) {
    QString html = QStringLiteral("<style>h3{margin:10px 0 2px 0;} .d{color:#888;font-weight:normal;} li{margin:2px 0;}</style>");
    int shown = 0;
    for (const ReleaseNotes& n : changes) {
        QStringList lines = n.body.split(QLatin1Char('\n'));
        QString titleLine;
        QStringList items;
        QString paragraph;
        for (QString line : lines) {
            line = line.trimmed();
            // Notas automáticas antigas do GitHub (só um link): não interessam aqui.
            if (line.isEmpty() || line.startsWith(QStringLiteral("**Full Changelog**")) || line.startsWith(QStringLiteral("## What")))
                continue;
            if (line.startsWith(QStringLiteral("- ")) || line.startsWith(QStringLiteral("* "))) {
                items << line.mid(2).toHtmlEscaped();
            } else if (line.startsWith(QStringLiteral("  ")) && !items.isEmpty()) {
                items.last() += QLatin1Char(' ') + line.toHtmlEscaped();
            } else if (titleLine.isEmpty()) {
                // "0.7.1: título" -> "título"
                titleLine = line.section(QLatin1Char(':'), line.startsWith(n.version) ? 1 : 0).trimmed().toHtmlEscaped();
            } else if (!items.isEmpty()) {
                items.last() += QLatin1Char(' ') + line.toHtmlEscaped(); // continuação de um item
            } else {
                paragraph += line.toHtmlEscaped() + QLatin1Char(' ');
            }
        }
        QString date;
        if (!n.date.isEmpty()) date = QLocale().toString(QDate::fromString(n.date, Qt::ISODate), QLocale::ShortFormat);
        html += QStringLiteral("<h3>%1 <span class='d'>v%2%3</span></h3>")
                    .arg(titleLine.isEmpty() ? tr("Version %1").arg(n.version) : titleLine, n.version,
                         date.isEmpty() ? QString() : QStringLiteral(" · ") + date);
        if (!paragraph.isEmpty()) html += QStringLiteral("<p>%1</p>").arg(paragraph);
        if (!items.isEmpty()) html += QStringLiteral("<ul><li>") + items.join(QStringLiteral("</li><li>")) + QStringLiteral("</li></ul>");
        if (items.isEmpty() && paragraph.isEmpty()) html += QStringLiteral("<p style='color:#888'>%1</p>").arg(tr("Improvements and fixes."));
        ++shown;
    }
    if (shown == 0) html += QStringLiteral("<p style='color:#888'>%1</p>").arg(tr("No notes to show."));
    return html;
}
