#pragma once
// Controlo por OSC (Bitfocus Companion): escuta numa porta UDP só na placa de rede
// escolhida e envia o estado (cue no ar, tempo que falta, play/pausa...) para o IP
// do Companion. Fala o protocolo OSC do Mitti (/mitti/...), por isso o módulo
// "Imimot Mitti" do Companion funciona com os seus presets.
// Nunca bloqueia: UDP sem ligação, pacotes inválidos são ignorados.

#include "vthost/core/osc.h"

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>

class OscServer : public QObject {
    Q_OBJECT
public:
    explicit OscServer(QObject* parent = nullptr);

    struct Config {
        bool enabled = false;
        QString bindAddress;     // "" = todas as placas
        quint16 port = 51000;
        QString feedbackHost;    // IP do Companion ("" = sem estado de volta)
        quint16 feedbackPort = 51001;
        bool onlyFromFeedbackHost = true;
        bool operator==(const Config&) const = default;
    };
    // Devolve false (com err) se não conseguir abrir a porta.
    bool configure(const Config& c, QString* err);
    bool isListening() const { return socket_.state() == QAbstractSocket::BoundState; }
    const Config& config() const { return cfg_; }

    // Estado para o Companion. send() só envia se o valor mudou (force = envia sempre).
    void send(const QString& address, const QString& value, bool force = false);
    void send(const QString& address, int value, bool force = false);
    void forgetSent() { sent_.clear(); }

signals:
    void messageReceived(const vthost::osc::Message& m);

private:
    void onReadyRead();
    void sendRaw(const vthost::osc::Message& m);

    QUdpSocket socket_;
    Config cfg_;
    QHostAddress feedback_;
    QHash<QString, QString> sent_;
};
