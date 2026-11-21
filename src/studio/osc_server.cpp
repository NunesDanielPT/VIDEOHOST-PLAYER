#include "osc_server.h"

#include <QNetworkDatagram>

using namespace vthost;

OscServer::OscServer(QObject* parent) : QObject(parent) {
    connect(&socket_, &QUdpSocket::readyRead, this, &OscServer::onReadyRead);
}

bool OscServer::configure(const Config& c, QString* err) {
    if (c == cfg_ && (isListening() || !c.enabled)) return true;
    socket_.close();
    sent_.clear();
    cfg_ = c;
    feedback_ = QHostAddress(c.feedbackHost.trimmed());
    if (!c.enabled) return true;
    const QHostAddress bind = c.bindAddress.isEmpty() ? QHostAddress(QHostAddress::AnyIPv4) : QHostAddress(c.bindAddress);
    if (!socket_.bind(bind, c.port, QUdpSocket::DontShareAddress)) {
        if (err) *err = socket_.errorString();
        socket_.close();
        return false;
    }
    return true;
}

void OscServer::onReadyRead() {
    // Limite por chamada: um flood de pacotes não prende a interface.
    for (int n = 0; n < 200 && socket_.hasPendingDatagrams(); ++n) {
        const QNetworkDatagram d = socket_.receiveDatagram(static_cast<qint64>(osc::kMaxPacketBytes));
        if (!d.isValid()) continue;
        // Só o Companion (se pedido): ninguém mais na rede controla o show.
        if (cfg_.onlyFromFeedbackHost && !feedback_.isNull() && !d.senderAddress().isEqual(feedback_, QHostAddress::TolerantConversion))
            continue;
        const QByteArray data = d.data();
        const auto msgs = osc::decodePacket(reinterpret_cast<const std::uint8_t*>(data.constData()), static_cast<std::size_t>(data.size()));
        for (const auto& m : msgs) emit messageReceived(m);
    }
    while (socket_.hasPendingDatagrams()) socket_.receiveDatagram(1); // descarta o excesso
}

void OscServer::sendRaw(const osc::Message& m) {
    if (!isListening() || feedback_.isNull() || cfg_.feedbackPort == 0) return;
    const auto bytes = osc::encodeMessage(m);
    socket_.writeDatagram(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()), feedback_, cfg_.feedbackPort);
}

void OscServer::send(const QString& address, const QString& value, bool force) {
    const QString key = QStringLiteral("s") + value;
    if (!force && sent_.value(address) == key) return;
    sent_.insert(address, key);
    sendRaw({address.toStdString(), {value.toStdString()}});
}

void OscServer::send(const QString& address, int value, bool force) {
    const QString key = QStringLiteral("i") + QString::number(value);
    if (!force && sent_.value(address) == key) return;
    sent_.insert(address, key);
    sendRaw({address.toStdString(), {static_cast<std::int32_t>(value)}});
}
