#include "network/device_session.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QHostAddress>
#include <QMetaObject>
#include <QTcpSocket>

#include <limits>

namespace {
constexpr int kFrameHeaderBytes = 4;
constexpr int kMaxBufferedBytes = 1024 * 1024;

QString authStateToString(DeviceSession::SessionAuthState state) {
    switch (state) {
    case DeviceSession::SessionAuthState::New:
        return QStringLiteral("new");
    case DeviceSession::SessionAuthState::ChallengeSent:
        return QStringLiteral("challenge_sent");
    case DeviceSession::SessionAuthState::PendingApproval:
        return QStringLiteral("pending_approval");
    case DeviceSession::SessionAuthState::Active:
        return QStringLiteral("active");
    case DeviceSession::SessionAuthState::Rejected:
        return QStringLiteral("rejected");
    }
    return QStringLiteral("unknown");
}
}

DeviceSession::DeviceSession(QTcpSocket *socket, QObject *parent)
    : QObject(parent)
    , socket_(socket) {
    Q_ASSERT(socket_);
    if (!socket_) {
        return;
    }

    if (socket_->parent() != this) {
        socket_->setParent(this);
    }

    connect(socket_, &QTcpSocket::readyRead, this, &DeviceSession::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &DeviceSession::onDisconnected);
    connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        qWarning() << "device session socket error:" << socket_->errorString();
    });

    qInfo() << "healthd tcp: session created"
            << "remote_ip=" << socket_->peerAddress().toString()
            << "remote_port=" << socket_->peerPort();

    if (socket_->bytesAvailable() > 0) {
        QMetaObject::invokeMethod(this, &DeviceSession::onReadyRead, Qt::QueuedConnection);
    }
}

DeviceSession::SessionAuthState DeviceSession::authState() const {
    return authState_;
}

void DeviceSession::setAuthState(SessionAuthState state) {
    if (authState_ == state) {
        return;
    }
    qInfo() << "healthd tcp: auth state changed"
            << "device_id=" << authenticatedDeviceId_
            << "from=" << authStateToString(authState_)
            << "to=" << authStateToString(state);
    authState_ = state;
}

QString DeviceSession::authenticatedDeviceId() const {
    return authenticatedDeviceId_;
}

void DeviceSession::setAuthenticatedDeviceId(const QString &deviceId) {
    authenticatedDeviceId_ = deviceId;
}

QString DeviceSession::serverNonce() const {
    return serverNonce_;
}

void DeviceSession::setServerNonce(const QString &serverNonce) {
    serverNonce_ = serverNonce;
}

bool DeviceSession::sendEnvelope(const DeviceEnvelope &envelope) {
    if (!socket_) {
        return false;
    }

    const QByteArray frame = DeviceFrameCodec::encode(envelope);
    if (frame.isEmpty()) {
        return false;
    }

    const qint64 written = socket_->write(frame);
    if (written != frame.size()) {
        return false;
    }
    socket_->flush();
    qInfo() << "healthd tcp: outbound frame"
            << "type=" << envelope.type
            << "device_id=" << envelope.deviceId
            << "seq=" << envelope.seq;
    return true;
}

void DeviceSession::onReadyRead() {
    if (!socket_) {
        return;
    }

    readBuffer_.append(socket_->readAll());
    processReadBuffer();

    // Enforce the cap only on bytes that could not yet form complete frames.
    if (readBuffer_.size() > kMaxBufferedBytes) {
        qWarning() << "device session read buffer exceeded limit:" << readBuffer_.size();
        readBuffer_.clear();
        socket_->disconnectFromHost();
        return;
    }
}

void DeviceSession::onDisconnected() {
    qInfo() << "healthd tcp: session disconnected"
            << "device_id=" << authenticatedDeviceId_
            << "auth_state=" << authStateToString(authState_)
            << "remote_ip=" << (socket_ ? socket_->peerAddress().toString() : QString())
            << "remote_port=" << (socket_ ? socket_->peerPort() : 0);
    deleteLater();
}

bool DeviceSession::tryExtractFrame(QByteArray *frameOut) {
    if (!frameOut || readBuffer_.size() < kFrameHeaderBytes) {
        return false;
    }

    const quint32 payloadLen = (static_cast<quint32>(static_cast<quint8>(readBuffer_.at(0))) << 24U)
        | (static_cast<quint32>(static_cast<quint8>(readBuffer_.at(1))) << 16U)
        | (static_cast<quint32>(static_cast<quint8>(readBuffer_.at(2))) << 8U)
        | static_cast<quint32>(static_cast<quint8>(readBuffer_.at(3)));

    if (payloadLen > static_cast<quint32>(std::numeric_limits<int>::max() - kFrameHeaderBytes)) {
        qWarning() << "device session frame length overflow:" << payloadLen;
        readBuffer_.clear();
        if (socket_) {
            socket_->disconnectFromHost();
        }
        return false;
    }

    const int frameLen = kFrameHeaderBytes + static_cast<int>(payloadLen);
    if (readBuffer_.size() < frameLen) {
        return false;
    }

    *frameOut = readBuffer_.left(frameLen);
    readBuffer_.remove(0, frameLen);
    return true;
}

void DeviceSession::processReadBuffer() {
    QByteArray frame;
    while (tryExtractFrame(&frame)) {
        DeviceEnvelope envelope;
        if (DeviceFrameCodec::decode(frame, &envelope)) {
            emit envelopeReceived(envelope);
        } else {
            qWarning() << "device session dropped invalid frame";
        }
    }
}
