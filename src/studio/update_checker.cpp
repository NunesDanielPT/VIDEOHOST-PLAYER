#include "update_checker.h"

#include <algorithm>
#include <optional>

#include "app_info.h"
#include "vthost/core/version.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr qint64 kMaxInstallerBytes = 2LL * 1024 * 1024 * 1024;
constexpr int kApiTimeoutMs = 15000;
constexpr int kDownloadStallTimeoutMs = 60000;

bool isTrustedUrl(const QUrl& url) {
    // Só HTTPS e só o GitHub (os downloads são redireccionados para *.githubusercontent.com).
    if (url.scheme() != QStringLiteral("https")) return false;
    const QString host = url.host().toLower();
    return host == QStringLiteral("github.com") || host == QStringLiteral("api.github.com") ||
           host.endsWith(QStringLiteral(".githubusercontent.com"));
}

QString extractSha256(const QString& text) {
    static const QRegularExpression re(QStringLiteral("\\b([0-9a-fA-F]{64})\\b"));
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}
} // namespace

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {
    net_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QNetworkRequest UpdateChecker::makeRequest(const QUrl& url) const {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VIDEOHOST-PLAYER/") + appinfo::kVersion);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

void UpdateChecker::check() {
    if (checking_) return;
    checking_ = true;
    useLegacyRepo_ = false;
    request();
}

void UpdateChecker::request() {
    QNetworkRequest req = makeRequest(QUrl(appinfo::releasesApi(useLegacyRepo_ ? appinfo::kLegacyRepo : appinfo::kUpdateRepo)));
    req.setTransferTimeout(kApiTimeoutMs);
    QNetworkReply* reply = net_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onCheckFinished(reply); });
}

void UpdateChecker::onCheckFinished(QNetworkReply* reply) {
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 404 && !useLegacyRepo_) {
        useLegacyRepo_ = true; // repositório com o nome novo ainda não existe: tenta o antigo
        request();
        return;
    }
    checking_ = false;
    if (status == 404) {
        emit upToDate(QString()); // ainda não há releases publicadas
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(tr("Could not contact GitHub: %1").arg(reply->errorString()));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->read(8 * 1024 * 1024));
    if (!doc.isArray()) {
        emit checkFailed(tr("Unexpected answer from GitHub."));
        return;
    }
    // A release mais recente (ignora rascunhos, pré-versões e tags inválidas) e as
    // novidades de todas as versões acima da instalada.
    QJsonObject rel;
    std::optional<vthost::Version> latest;
    QList<ReleaseNotes> changes;
    for (const auto& v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("draft")).toBool() || o.value(QStringLiteral("prerelease")).toBool()) continue;
        const QString t = o.value(QStringLiteral("tag_name")).toString();
        const auto ver = vthost::parseVersion(t.toStdString());
        if (!ver) continue;
        if (!latest || *latest < *ver) {
            latest = ver;
            rel = o;
        }
        if (vthost::isNewerVersion(t.toStdString(), appinfo::kVersion.toStdString())) {
            ReleaseNotes n;
            n.version = QString::fromStdString(ver->toString());
            n.date = o.value(QStringLiteral("published_at")).toString().left(10);
            n.body = o.value(QStringLiteral("body")).toString().left(20000);
            changes << n;
        }
    }
    if (!latest) {
        emit upToDate(QString()); // ainda não há releases publicadas
        return;
    }
    std::sort(changes.begin(), changes.end(), [](const ReleaseNotes& a, const ReleaseNotes& b) {
        return vthost::isNewerVersion(a.version.toStdString(), b.version.toStdString());
    });
    const QString tag = rel.value(QStringLiteral("tag_name")).toString();
    const QString latestStr = QString::fromStdString(latest->toString());
    if (!vthost::isNewerVersion(tag.toStdString(), appinfo::kVersion.toStdString())) {
        emit upToDate(latestStr);
        return;
    }

    UpdateInfo info;
    info.version = latestStr;
    info.changes = changes;
    info.notes = rel.value(QStringLiteral("body")).toString().left(20000);
    info.pageUrl = rel.value(QStringLiteral("html_url")).toString();
    const QJsonArray assets = rel.value(QStringLiteral("assets")).toArray();
    // Instalador com o nome novo; senão o antigo (releases anteriores à mudança de nome).
    bool hasNewName = false;
    for (const auto& a : assets)
        if (a.toObject().value(QStringLiteral("name")).toString().startsWith(appinfo::kInstallerPrefix)) hasNewName = true;
    const QString prefix = hasNewName ? appinfo::kInstallerPrefix : appinfo::kLegacyInstallerPrefix;
    for (const auto& a : assets) {
        const QJsonObject o = a.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        const QUrl url(o.value(QStringLiteral("browser_download_url")).toString());
        if (name.startsWith(prefix) && name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            info.assetName = name;
            info.assetUrl = url;
            info.assetSize = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble());
            const QString digest = o.value(QStringLiteral("digest")).toString();
            if (digest.startsWith(QStringLiteral("sha256:"))) info.sha256 = extractSha256(digest.mid(7));
        }
    }
    for (const auto& a : assets) {
        const QJsonObject o = a.toObject();
        if (!info.assetName.isEmpty() && o.value(QStringLiteral("name")).toString() == info.assetName + QStringLiteral(".sha256"))
            info.sha256Url = QUrl(o.value(QStringLiteral("browser_download_url")).toString());
    }
    if (info.assetName.isEmpty() || !isTrustedUrl(info.assetUrl)) {
        emit checkFailed(tr("Version %1 exists but has no installer yet.").arg(latestStr));
        return;
    }
    emit updateAvailable(info);
}

void UpdateChecker::download(const UpdateInfo& info) {
    if (downloadReply_) return;
    pending_ = info;
    if (pending_.sha256.isEmpty()) {
        if (!pending_.sha256Url.isValid() || !isTrustedUrl(pending_.sha256Url)) {
            emit downloadFailed(tr("The release has no checksum; the update was not installed for safety."));
            return;
        }
        QNetworkRequest req = makeRequest(pending_.sha256Url);
        req.setTransferTimeout(kApiTimeoutMs);
        QNetworkReply* r = net_.get(req);
        downloadReply_ = r;
        connect(r, &QNetworkReply::finished, this, [this, r] {
            r->deleteLater();
            downloadReply_ = nullptr;
            if (r->error() != QNetworkReply::NoError) {
                emit downloadFailed(tr("Could not get the checksum: %1").arg(r->errorString()));
                return;
            }
            pending_.sha256 = extractSha256(QString::fromLatin1(r->read(4096)));
            if (pending_.sha256.isEmpty()) {
                emit downloadFailed(tr("Invalid checksum file; the update was not installed for safety."));
                return;
            }
            startInstallerDownload();
        });
        return;
    }
    startInstallerDownload();
}

void UpdateChecker::startInstallerDownload() {
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("VIDEOHOST-PLAYER-update"));
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    file_.setFileName(QDir(dir).filePath(pending_.assetName));
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFailed(tr("Could not write to the temporary folder."));
        return;
    }

    QNetworkRequest req = makeRequest(pending_.assetUrl);
    req.setRawHeader("Accept", "application/octet-stream");
    req.setTransferTimeout(kDownloadStallTimeoutMs);
    QNetworkReply* r = net_.get(req);
    downloadReply_ = r;
    connect(r, &QNetworkReply::readyRead, this, [this, r] {
        file_.write(r->readAll());
        if (file_.size() > kMaxInstallerBytes) failDownload(tr("Installer is larger than expected."));
    });
    connect(r, &QNetworkReply::downloadProgress, this, [this](qint64 rec, qint64 total) {
        emit downloadProgress(rec, total > 0 ? total : pending_.assetSize);
    });
    connect(r, &QNetworkReply::finished, this, &UpdateChecker::finishDownload);
}

void UpdateChecker::finishDownload() {
    QNetworkReply* r = downloadReply_;
    if (!r) return;
    downloadReply_ = nullptr;
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) {
        failDownload(r->error() == QNetworkReply::OperationCanceledError ? tr("Download cancelled.")
                                                                          : tr("Download failed: %1").arg(r->errorString()));
        return;
    }
    if (!isTrustedUrl(r->url())) {
        failDownload(tr("The download was redirected to an unknown server."));
        return;
    }
    file_.write(r->readAll());
    file_.close();

    if (!file_.open(QIODevice::ReadOnly)) {
        failDownload(tr("Could not read the downloaded installer."));
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file_);
    file_.close();
    const QString actual = QString::fromLatin1(hash.result().toHex());
    if (actual.compare(pending_.sha256, Qt::CaseInsensitive) != 0) {
        file_.remove();
        emit downloadFailed(tr("The downloaded installer is corrupted (checksum mismatch). Try again."));
        return;
    }
    emit downloadReady(file_.fileName());
}

void UpdateChecker::failDownload(const QString& error) {
    if (downloadReply_) {
        QNetworkReply* r = downloadReply_;
        downloadReply_ = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    if (file_.isOpen()) file_.close();
    file_.remove();
    emit downloadFailed(error);
}

void UpdateChecker::cancelDownload() {
    if (downloadReply_) downloadReply_->abort(); // finishDownload trata o resto
}
