#include "countdown_server.h"

#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {
constexpr int kMaxClients = 64;          // ligações em simultâneo
constexpr int kMaxRequestBytes = 8192;   // pedidos maiores são recusados
constexpr int kIdleMs = 5000;            // ligação sem pedido completo é fechada
} // namespace

CountdownServer::CountdownServer(JsonProvider provider, QObject* parent)
    : QObject(parent), provider_(std::move(provider)) {}

CountdownServer::~CountdownServer() { stop(); }

bool CountdownServer::isRunning() const { return server_ && server_->isListening(); }

bool CountdownServer::start(quint16 port, const QString& bindAddress, QString* error) {
    stop();
    bind_ = bindAddress;
    QHostAddress addr(QHostAddress::AnyIPv4);
    if (!bindAddress.isEmpty() && !addr.setAddress(bindAddress)) {
        if (error) *error = tr("Invalid network address: %1").arg(bindAddress);
        return false;
    }
    server_ = new QTcpServer(this);
    server_->setMaxPendingConnections(kMaxClients);
    connect(server_, &QTcpServer::newConnection, this, &CountdownServer::onNewConnection);
    // Só esta placa: quem está noutra rede do PC não chega à página.
    if (!server_->listen(addr, port)) {
        if (error) *error = server_->errorString();
        server_->deleteLater();
        server_ = nullptr;
        port_ = 0;
        return false;
    }
    port_ = server_->serverPort();
    return true;
}

void CountdownServer::stop() {
    for (auto it = buffers_.begin(); it != buffers_.end(); ++it) {
        it.key()->disconnect(this);
        it.key()->abort();
        it.key()->deleteLater();
    }
    buffers_.clear();
    if (server_) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
    port_ = 0;
}

QStringList CountdownServer::urls() const {
    QStringList out;
    if (!isRunning()) return out;
    if (!bind_.isEmpty()) return {QStringLiteral("http://%1:%2").arg(bind_).arg(port_)};
    for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
        if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLoopback() || a.isLinkLocal()) continue;
        out << QStringLiteral("http://%1:%2").arg(a.toString()).arg(port_);
    }
    if (out.isEmpty()) out << QStringLiteral("http://localhost:%1").arg(port_);
    return out;
}

void CountdownServer::onNewConnection() {
    while (server_ && server_->hasPendingConnections()) {
        QTcpSocket* s = server_->nextPendingConnection();
        if (!s) break;
        if (buffers_.size() >= kMaxClients) {
            s->abort();
            s->deleteLater();
            continue;
        }
        buffers_.insert(s, {});
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            buffers_.remove(s);
            s->deleteLater();
        });
        // Ligações penduradas não ficam abertas para sempre.
        QTimer::singleShot(kIdleMs, s, [this, s] {
            if (buffers_.contains(s)) s->abort();
        });
    }
}

void CountdownServer::onReadyRead(QTcpSocket* s) {
    auto it = buffers_.find(s);
    if (it == buffers_.end()) return;
    it.value() += s->readAll();
    QByteArray& buf = it.value();
    if (buf.size() > kMaxRequestBytes) {
        reply(s, "413 Payload Too Large", "text/plain", "too large");
        return;
    }
    const int end = buf.indexOf("\r\n\r\n");
    if (end < 0) return; // pedido ainda incompleto
    const QList<QByteArray> parts = buf.left(buf.indexOf("\r\n")).split(' ');
    if (parts.size() < 2 || (parts[0] != "GET" && parts[0] != "HEAD")) {
        reply(s, "405 Method Not Allowed", "text/plain", "GET only");
        return;
    }
    QByteArray path = parts[1];
    if (const int q = path.indexOf('?'); q >= 0) path.truncate(q);

    if (path == "/api/countdown") {
        reply(s, "200 OK", "application/json; charset=utf-8", provider_ ? provider_() : QByteArray("{}"));
    } else if (path == "/" || path == "/index.html") {
        QFile f(QStringLiteral(":/web/countdown.html"));
        if (f.open(QIODevice::ReadOnly)) reply(s, "200 OK", "text/html; charset=utf-8", f.readAll());
        else reply(s, "500 Internal Server Error", "text/plain", "page missing");
    } else {
        reply(s, "404 Not Found", "text/plain", "not found");
    }
}

void CountdownServer::reply(QTcpSocket* s, const QByteArray& status, const QByteArray& type, const QByteArray& body) {
    QByteArray head = "HTTP/1.1 " + status + "\r\nContent-Type: " + type +
                      "\r\nContent-Length: " + QByteArray::number(body.size()) +
                      "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
    const bool head_only = buffers_.value(s).startsWith("HEAD");
    buffers_.remove(s);
    s->write(head);
    if (!head_only) s->write(body);
    s->disconnectFromHost(); // fecha depois de enviar tudo
}
