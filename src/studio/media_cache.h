#pragma once
// Informação e miniaturas das media (FFmpeg numa thread do pool).
// Nunca bloqueia a interface: info() devolve o que já se sabe e pede o resto.

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>

struct MediaInfo {
    bool ready = false; // análise terminada (com ou sem sucesso)
    bool ok = false;
    int width = 0;
    int height = 0;
    double duration = 0.0; // 0 = imagem fixa / desconhecida
    bool still = false;
    QImage thumb;          // até 192x108
    QString error;
};

class MediaCache : public QObject {
    Q_OBJECT
public:
    explicit MediaCache(QObject* parent = nullptr);
    ~MediaCache() override;

    // Devolve a info (ready=false enquanto está a ser analisada).
    MediaInfo info(const QString& path);

    // Duração sugerida para um clip novo na timeline.
    double suggestedClipDuration(const QString& path);

signals:
    void updated(const QString& path);

private:
    void finished(const QString& path, const MediaInfo& info);

    QHash<QString, MediaInfo> cache_;
    QSet<QString> inFlight_;
};
