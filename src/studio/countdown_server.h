#pragma once
// Servidor HTTP mínimo (só leitura) para a página do countdown na rede local:
//   GET /               -> página (web/countdown.html)
//   GET /api/countdown  -> JSON com o estado actual
// Corre na thread da interface (QTcpServer, não bloqueia). Nunca toca no motor.

#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>

class QTcpServer;
class QTcpSocket;

class CountdownServer : public QObject {
    Q_OBJECT
public:
    using JsonProvider = std::function<QByteArray()>;

    explicit CountdownServer(JsonProvider provider, QObject* parent = nullptr);
    ~CountdownServer() override;

    // Liga (ou religa). `bindAddress` = IP de uma placa de rede ("" = todas as placas).
    // Devolve false e preenche `error` se a porta estiver ocupada ou a placa não existir.
    bool start(quint16 port, const QString& bindAddress, QString* error = nullptr);
    void stop();
    bool isRunning() const;
    quint16 port() const { return port_; }
    QString bindAddress() const { return bind_; }

    // Endereços para abrir no telemóvel/computador na mesma rede (ex. http://192.168.1.20:8088).
    QStringList urls() const;

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket* s);
    void reply(QTcpSocket* s, const QByteArray& status, const QByteArray& type, const QByteArray& body);

    JsonProvider provider_;
    QTcpServer* server_ = nullptr;
    quint16 port_ = 0;
    QString bind_;
    QHash<QTcpSocket*, QByteArray> buffers_;
};
