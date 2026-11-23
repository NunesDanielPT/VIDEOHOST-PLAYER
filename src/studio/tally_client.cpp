#include "tally_client.h"

#include "vthost/core/tally.h"

#include <QNetworkDatagram>

using namespace vthost;

TallyClient::TallyClient(QObject* parent) : QObject(parent) {
    retry_.setInterval(2000);
    connect(&retry_, &QTimer::timeout, this, &TallyClient::reconnect);
    watchdog_.setInterval(1000);
    connect(&watchdog_, &QTimer::timeout, this, [this] {
        if (cfg_.mode == Mode::Atem && atemEstablished_ && lastPacket_.isValid() && lastPacket_.elapsed() > 5000) {
            atemEstablished_ = false;
            setConnected(false, tr("no answer from the ATEM"));
            reconnect();
        }
    });
    connect(&tcp_, &QTcpSocket::connected, this, [this] {
        tcp_.write("SUBSCRIBE TALLY\r\n");
        setConnected(true);
    });
    connect(&tcp_, &QTcpSocket::readyRead, this, &TallyClient::vmixRead);
    connect(&tcp_, &QTcpSocket::disconnected, this, [this] { setConnected(false, tr("connection closed")); });
    connect(&tcp_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        setConnected(false, tcp_.errorString());
    });
    connect(&udp_, &QUdpSocket::readyRead, this, &TallyClient::atemRead);
}

void TallyClient::configure(const Config& c) {
    if (c == cfg_ && (cfg_.mode == Mode::Off || retry_.isActive())) return;
    cfg_ = c;
    tcp_.abort();
    udp_.close();
    retry_.stop();
    watchdog_.stop();
    atemEstablished_ = false;
    setOnAir(false);
    setConnected(false);
    if (cfg_.mode == Mode::Off || cfg_.host.trimmed().isEmpty()) return;
    retry_.start();
    if (cfg_.mode == Mode::Atem) watchdog_.start();
    reconnect();
}

void TallyClient::reconnect() {
    if (cfg_.mode == Mode::Vmix) {
        if (tcp_.state() == QAbstractSocket::ConnectedState || tcp_.state() == QAbstractSocket::ConnectingState) return;
        vmixBuffer_.clear();
        tcp_.connectToHost(cfg_.host.trimmed(), 8099);
    } else if (cfg_.mode == Mode::Atem) {
        if (atemEstablished_) return;
        atemAddress_ = QHostAddress(cfg_.host.trimmed());
        if (atemAddress_.isNull()) {
            setConnected(false, tr("write the IP address of the ATEM"));
            return;
        }
        if (udp_.state() != QAbstractSocket::BoundState && !udp_.bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
            setConnected(false, udp_.errorString());
            return;
        }
        atemLastPacket_ = -1;
        atemSend(tally::atemHello());
    }
}

void TallyClient::setOnAir(bool on) {
    if (on == onAir_) return;
    onAir_ = on;
    emit onAirChanged(on);
}

void TallyClient::tallyState(bool on) {
    const bool rising = haveState_ && on && !onAir_;
    haveState_ = true;
    setOnAir(on);
    if (rising) emit wentOnAir();
}

void TallyClient::setConnected(bool on, const QString& error) {
    const bool changed = on != connected_ || error != error_;
    if (on && !connected_) haveState_ = false; // ligação nova: o primeiro estado não dispara
    connected_ = on;
    error_ = on ? QString() : error;
    if (!on) {
        setOnAir(false); // sem ligação não se dispara nada
        haveState_ = false;
    }
    if (changed) emit stateChanged();
}

void TallyClient::vmixRead() {
    vmixBuffer_ += tcp_.readAll();
    if (vmixBuffer_.size() > 64 * 1024) vmixBuffer_.clear(); // memória limitada
    int nl;
    while ((nl = vmixBuffer_.indexOf('\n')) >= 0) {
        const std::string line = vmixBuffer_.left(nl).toStdString();
        vmixBuffer_.remove(0, nl + 1);
        if (const auto on = tally::vmixProgram(line, cfg_.input)) tallyState(*on);
    }
}

void TallyClient::atemSend(const std::vector<std::uint8_t>& b) {
    udp_.writeDatagram(reinterpret_cast<const char*>(b.data()), static_cast<qint64>(b.size()), atemAddress_, tally::kAtemPort);
}

void TallyClient::atemRead() {
    for (int n = 0; n < 500 && udp_.hasPendingDatagrams(); ++n) {
        const QNetworkDatagram d = udp_.receiveDatagram(4096);
        if (!d.isValid() || !d.senderAddress().isEqual(atemAddress_, QHostAddress::TolerantConversion)) continue;
        const QByteArray data = d.data();
        const auto* p = reinterpret_cast<const std::uint8_t*>(data.constData());
        tally::AtemHeader h;
        if (!tally::parseAtemHeader(p, static_cast<std::size_t>(data.size()), h)) continue;
        lastPacket_.start();
        atemSession_ = h.session;
        if (h.flags & tally::kFlagNewSession) {
            // Resposta ao hello: ligação aceite.
            atemEstablished_ = true;
            atemLastPacket_ = h.packetId;
            atemSend(tally::atemAck(atemSession_, h.packetId));
            setConnected(true);
            continue;
        }
        if (!atemEstablished_ || !(h.flags & tally::kFlagAckRequest) || h.packetId >= tally::kAtemMaxPacketId) continue;
        const int expected = (atemLastPacket_ + 1) % tally::kAtemMaxPacketId;
        if (h.packetId == expected) {
            atemLastPacket_ = h.packetId;
            for (const auto& c : tally::parseAtemCommands(p + 12, static_cast<std::size_t>(data.size()) - 12))
                if (const auto on = tally::atemTallyProgram(c, cfg_.input)) tallyState(*on);
            atemSend(tally::atemAck(atemSession_, h.packetId));
        } else if (atemLastPacket_ >= 0) {
            // Repetido ou fora de ordem: confirma o último que temos (o ATEM volta a enviar o que falta).
            atemSend(tally::atemAck(atemSession_, static_cast<std::uint16_t>(atemLastPacket_)));
        }
    }
    while (udp_.hasPendingDatagrams()) udp_.receiveDatagram(1);
}
