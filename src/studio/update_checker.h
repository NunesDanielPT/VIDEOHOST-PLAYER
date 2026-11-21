#pragma once
// Atualizações a partir das GitHub Releases.
//
// 1. check(): lê as últimas releases; se houver uma mais recente que esta versão,
//    emite updateAvailable com o instalador (VTHOST-Setup-x.y.z.exe) e as
//    novidades de todas as versões que faltam.
// 2. download(): descarrega o instalador para a pasta temporária e confirma o
//    SHA-256 (o "digest" que o GitHub publica para cada ficheiro, ou o ficheiro
//    .sha256 da release). Sem hash confirmado, o instalador nunca é executado.

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

// Novidades de uma versão (texto da release no GitHub).
struct ReleaseNotes {
    QString version;
    QString date;  // "2026-11-21"
    QString body;  // 1.ª linha = título, linhas "- ..." = itens
};

struct UpdateInfo {
    QString version;     // "0.2.0"
    QString notes;       // texto da release (markdown)
    QString pageUrl;     // página da release
    QString assetName;   // VTHOST-Setup-0.2.0.exe
    QUrl assetUrl;
    qint64 assetSize = 0;
    QString sha256;      // hex minúsculo (pode vir vazio se só existir o .sha256)
    QUrl sha256Url;      // VTHOST-Setup-0.2.0.exe.sha256
    QList<ReleaseNotes> changes; // todas as versões mais recentes do que a instalada (da mais nova para a mais antiga)
};

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    void check();
    void download(const UpdateInfo& info);
    void cancelDownload();

signals:
    void updateAvailable(const UpdateInfo& info);
    void upToDate(const QString& latestVersion);
    void checkFailed(const QString& error);
    void downloadProgress(qint64 received, qint64 total);
    void downloadReady(const QString& installerPath);
    void downloadFailed(const QString& error);

private:
    QNetworkRequest makeRequest(const QUrl& url) const;
    void onCheckFinished(QNetworkReply* reply);
    void startInstallerDownload();
    void finishDownload();
    void failDownload(const QString& error);

    QNetworkAccessManager net_;
    UpdateInfo pending_;
    QPointer<QNetworkReply> downloadReply_;
    QFile file_;
    bool checking_ = false;
};
