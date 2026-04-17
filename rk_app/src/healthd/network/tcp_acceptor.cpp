#include "network/tcp_acceptor.h"

#include "network/device_session.h"

#include <QDebug>
#include <QHostAddress>
#include <QTcpSocket>

TcpAcceptor::TcpAcceptor(QObject *parent)
    : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &TcpAcceptor::onNewConnection);
}

bool TcpAcceptor::start(quint16 port) {
    if (server_.isListening()) {
        qInfo() << "healthd tcp: listen requested while already active"
                << "port=" << server_.serverPort();
        return true;
    }

    const bool ok = server_.listen(QHostAddress::AnyIPv4, port);
    if (!ok) {
        qCritical() << "tcp acceptor listen failed:" << server_.errorString();
    } else {
        qInfo() << "healthd tcp: acceptor listening"
                << "address=" << server_.serverAddress().toString()
                << "port=" << server_.serverPort();
    }
    return ok;
}

void TcpAcceptor::onNewConnection() {
    while (server_.hasPendingConnections()) {
        QTcpSocket *socket = server_.nextPendingConnection();
        if (!socket) {
            continue;
        }

        DeviceSession *session = new DeviceSession(socket, this);
        sessions_.insert(session);
        qInfo() << "healthd tcp: new device connection"
                << "remote_ip=" << socket->peerAddress().toString()
                << "remote_port=" << socket->peerPort()
                << "session_count=" << sessions_.size();

        connect(session, &DeviceSession::envelopeReceived, this,
            [this, socket](const DeviceEnvelope &envelope) {
                const QString remoteIp = socket->peerAddress().toString();
                auto *session = qobject_cast<DeviceSession *>(sender());
                if (session) {
                    emit envelopeReceived(session, envelope, remoteIp);
                }
            });
        connect(session, &QObject::destroyed, this, [this, session]() {
            sessions_.remove(session);
            qInfo() << "healthd tcp: device session destroyed"
                    << "session_count=" << sessions_.size();
        });
    }
}
