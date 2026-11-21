#pragma once
// Informação, miniaturas, forma de onda e loudness das media (FFmpeg em threads
// de fundo). Nunca bloqueia a interface: info()/audio() devolvem o que já se sabe
// e pedem o resto; o sinal updated() avisa quando há novidades.

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVector>

#include <optional>

struct MediaInfo {
    bool ready = false; // análise terminada (com ou sem sucesso)
    bool ok = false;
    int width = 0;
    int height = 0;
    double duration = 0.0; // 0 = imagem fixa / desconhecida
    bool still = false;
    QImage thumb;          // até 192x108
    QString error;
    double fps = 0.0;
    QString videoCodec;    // ex. "H.264"
    QString audioCodec;    // ex. "AAC"
    int audioChannels = 0;
    int bitDepth = 0;
    bool hasAudio = false;
};

// Análise do som (descodifica o ficheiro inteiro, em fundo).
struct AudioAnalysis {
    bool ready = false;
    bool hasAudio = false;
    QVector<float> peaks;      // por bloco: pico L, pico R (0..1)
    double blocksPerSecond = 0.0;
    std::optional<double> loudness; // LUFS integrados
    std::optional<double> peakDb;   // dBFS
};

class MediaCache : public QObject {
    Q_OBJECT
public:
    explicit MediaCache(QObject* parent = nullptr);
    ~MediaCache() override;

    // Devolve a info (ready=false enquanto está a ser analisada).
    MediaInfo info(const QString& path);

    // Forma de onda e loudness (pede a análise se ainda não existir).
    AudioAnalysis audio(const QString& path);
    // Esquece o que se sabe do ficheiro (ex. foi substituído): volta a analisar.
    void forget(const QString& path);

signals:
    void updated(const QString& path);
    void audioAnalyzed(const QString& path);

private:
    void finished(const QString& path, const MediaInfo& info);

    QHash<QString, MediaInfo> cache_;
    QSet<QString> inFlight_;
    QHash<QString, AudioAnalysis> audio_;
    QSet<QString> audioInFlight_;
    QThreadPool pool_;      // miniaturas
    QThreadPool audioPool_; // análise de som (pesada): 1 thread, prioridade baixa
};
