#include "engine_client.h"

#include "vthost/core/paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

using namespace vthost;

namespace {
constexpr int kPollMs = 500;
constexpr int kReconnectMs = 1000;
constexpr qint64 kReplyTimeoutMs = 6000;
} // namespace

EngineClient::EngineClient(QObject* parent) : QObject(parent) {
    clock_.start();
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        buffer_.clear();
        pending_.clear();
        statusInFlight_ = false;
        emit connectionChanged(true);
        poll();
    });
    connect(&socket_, &QLocalSocket::disconnected, this, &EngineClient::onDisconnected);
    connect(&socket_, &QLocalSocket::readyRead, this, &EngineClient::onReadyRead);
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (socket_.state() != QLocalSocket::ConnectedState) reconnectTimer_.start();
    });

    reconnectTimer_.setSingleShot(true);
    reconnectTimer_.setInterval(kReconnectMs);
    connect(&reconnectTimer_, &QTimer::timeout, this, &EngineClient::tryConnect);

    pollTimer_.setInterval(kPollMs);
    connect(&pollTimer_, &QTimer::timeout, this, &EngineClient::poll);
    pollTimer_.start();

    tryConnect();
}

bool EngineClient::isConnected() const { return socket_.state() == QLocalSocket::ConnectedState; }

void EngineClient::tryConnect() {
    if (socket_.state() != QLocalSocket::UnconnectedState) return;
    socket_.connectToServer(QString::fromLatin1(paths::kPipeName));
}

void EngineClient::onDisconnected() {
    // Pedidos sem resposta falham de forma explícita (nunca ficam pendurados).
    auto pending = std::move(pending_);
    pending_.clear();
    for (auto& p : pending) {
        if (p.callback) {
            protocol::ParsedResponse r;
            r.error = "ligação ao motor perdida";
            p.callback(r);
        }
    }
    statusInFlight_ = false;
    hasStatus_ = false;
    emit connectionChanged(false);
    reconnectTimer_.start();
}

void EngineClient::send(const protocol::Command& command, Callback callback) {
    if (!isConnected()) {
        if (callback) {
            protocol::ParsedResponse r;
            r.error = "motor desligado";
            callback(r);
        }
        return;
    }
    QByteArray line = QByteArray::fromStdString(protocol::serializeCommand(command));
    line.append('\n');
    socket_.write(line);
    pending_.push_back({std::move(callback), clock_.elapsed()});
}

void EngineClient::sendLatest(const QString& key, const protocol::Command& command) {
    Coalesced& st = coalesced_[key];
    if (st.inFlight) {
        st.next = command; // substitui o que estava à espera
        return;
    }
    st.inFlight = true;
    send(command, [this, key](const protocol::ParsedResponse&) {
        Coalesced& s2 = coalesced_[key];
        s2.inFlight = false;
        if (s2.next) {
            const protocol::Command n = *s2.next;
            s2.next.reset();
            sendLatest(key, n);
        }
    });
}

void EngineClient::onReadyRead() {
    buffer_.append(socket_.readAll());
    if (buffer_.size() > static_cast<qsizetype>(protocol::kMaxLineBytes) * 4) {
        socket_.abort(); // resposta anómala: refaz a ligação
        return;
    }
    qsizetype nl;
    while ((nl = buffer_.indexOf('\n')) >= 0) {
        const std::string line = buffer_.left(nl).toStdString();
        buffer_.remove(0, nl + 1);
        if (pending_.empty()) continue; // resposta inesperada: ignora
        Pending p = std::move(pending_.front());
        pending_.pop_front();
        if (p.callback) p.callback(protocol::parseResponse(line));
    }
}

void EngineClient::poll() {
    if (!isConnected()) return;
    if (!pending_.empty() && clock_.elapsed() - pending_.front().sentAtMs > kReplyTimeoutMs) {
        socket_.abort(); // motor não responde: o watchdog trata dele, nós religamos
        return;
    }
    if (statusInFlight_) return;
    statusInFlight_ = true;
    protocol::Command c;
    c.type = protocol::CommandType::GetStatus;
    send(c, [this](const protocol::ParsedResponse& r) {
        statusInFlight_ = false;
        if (r.ok && r.status) {
            status_ = *r.status;
            hasStatus_ = true;
            emit statusUpdated();
        }
    });
}

bool EngineClient::startEngine(const QString& projectPath) {
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString exe = QDir(dir).filePath(QStringLiteral("vthost-watchdog.exe"));
#else
    const QString exe = QDir(dir).filePath(QStringLiteral("vthost-watchdog"));
#endif
    if (!QFileInfo::exists(exe)) return false;
    QStringList args;
    if (!projectPath.isEmpty()) args << QStringLiteral("--project") << QDir::toNativeSeparators(projectPath);
    // startDetached: fechar o Studio NÃO pára o motor.
    const bool ok = QProcess::startDetached(exe, args, dir);
    reconnectTimer_.start();
    return ok;
}
