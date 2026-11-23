#pragma once
// Ligação a um misturador (vMix ou Blackmagic ATEM) para saber quando o input do
// player vai para o ar (tally de programa). Volta a ligar sozinho; nunca bloqueia.

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

class TallyClient : public QObject {
    Q_OBJECT
public:
    enum class Mode { Off = 0, Vmix = 1, Atem = 2 };
    struct Config {
        Mode mode = Mode::Off;
        QString host;
        int input = 1; // vMix: número do input; ATEM: número da fonte (1 = Input 1)
        bool operator==(const Config&) const = default;
    };

    explicit TallyClient(QObject* parent = nullptr);
    void configure(const Config& c);
    const Config& config() const { return cfg_; }
    bool isConnected() const { return connected_; }
    bool onAir() const { return onAir_; }
    QString lastError() const { return error_; }

signals:
    void onAirChanged(bool onAir);
    // O input passou de fora para NO AR (não conta o estado que já tinha ao ligar).
    void wentOnAir();
    void stateChanged(); // ligado/desligado, erro

private:
    void reconnect();
    void setOnAir(bool on);
    void tallyState(bool on); // estado vindo do misturador
    void setConnected(bool on, const QString& error = QString());
    // vMix
    void vmixRead();
    // ATEM
    void atemRead();
    void atemSend(const std::vector<std::uint8_t>& bytes);

    Config cfg_;
    QTcpSocket tcp_;
    QUdpSocket udp_;
    QTimer retry_;      // nova tentativa / hello
    QTimer watchdog_;   // ATEM: sem pacotes há 5 s = ligação perdida
    QElapsedTimer lastPacket_;
    QByteArray vmixBuffer_;
    QHostAddress atemAddress_;
    quint16 atemSession_ = 0;
    int atemLastPacket_ = -1;
    bool atemEstablished_ = false;
    bool connected_ = false;
    bool onAir_ = false;
    bool haveState_ = false; // já recebeu o primeiro estado desde que ligou
    QString error_;
};
