#pragma once
// Ligação do Studio ao motor (QLocalSocket -> \\.\pipe\vthost-engine).
// Reconecta sozinho; se o motor não responder, a ligação é refeita.

#include "vthost/core/protocol.h"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

#include <deque>
#include <functional>

class EngineClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const vthost::protocol::ParsedResponse&)>;

    explicit EngineClient(QObject* parent = nullptr);

    bool isConnected() const;
    void send(const vthost::protocol::Command& command, Callback callback = {});

    // Arranca o watchdog (que arranca o motor). Devolve false se não encontrar o executável.
    bool startEngine(const QString& projectPath);

    const vthost::protocol::EngineStatus& lastStatus() const { return status_; }
    bool hasStatus() const { return hasStatus_; }

signals:
    void connectionChanged(bool connected);
    void statusUpdated();

private:
    void tryConnect();
    void onReadyRead();
    void onDisconnected();
    void poll();

    struct Pending {
        Callback callback;
        qint64 sentAtMs = 0;
    };

    QLocalSocket socket_;
    QByteArray buffer_;
    std::deque<Pending> pending_;
    QTimer pollTimer_;
    QTimer reconnectTimer_;
    QElapsedTimer clock_;
    bool statusInFlight_ = false;
    bool hasStatus_ = false;
    vthost::protocol::EngineStatus status_;
};
