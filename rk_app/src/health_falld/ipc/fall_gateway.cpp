#include "ipc/fall_gateway.h"

#include "protocol/fall_ipc.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

FallGateway::FallGateway(const FallRuntimeStatus &initialStatus, QObject *parent)
    : QObject(parent)
    , status_(initialStatus)
    , server_(new QLocalServer(this)) {
    connect(server_, &QLocalServer::newConnection, this, &FallGateway::onNewConnection);
}

FallGateway::~FallGateway() {
    stop();
}

bool FallGateway::start() {
    stop();
    const QFileInfo socketInfo(socketName_);
    if (socketName_.contains('/') && !socketInfo.absolutePath().isEmpty()) {
        QDir().mkpath(socketInfo.absolutePath());
    }
    QLocalServer::removeServer(socketName_);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    return server_->listen(socketName_);
}

void FallGateway::stop() {
    classificationSubscribers_.clear();
    if (server_->isListening()) {
        server_->close();
    }
    QLocalServer::removeServer(socketName_);
}

void FallGateway::setRuntimeStatus(const FallRuntimeStatus &status) {
    status_ = status;
}

void FallGateway::setSocketName(const QString &socketName) {
    socketName_ = socketName;
}

void FallGateway::publishClassification(const FallClassificationResult &result) {
    const QByteArray message = buildClassificationMessage(result);
    for (int index = classificationSubscribers_.size() - 1; index >= 0; --index) {
        QLocalSocket *socket = classificationSubscribers_.at(index).data();
        if (!socket || socket->state() != QLocalSocket::ConnectedState) {
            classificationSubscribers_.removeAt(index);
            continue;
        }

        socket->write(message);
        socket->flush();
    }
}

void FallGateway::publishClassificationBatch(const FallClassificationBatch &batch) {
    const QByteArray message = buildClassificationBatchMessage(batch);
    for (int index = classificationSubscribers_.size() - 1; index >= 0; --index) {
        QLocalSocket *socket = classificationSubscribers_.at(index).data();
        if (!socket || socket->state() != QLocalSocket::ConnectedState) {
            classificationSubscribers_.removeAt(index);
            continue;
        }

        socket->write(message);
        socket->flush();
    }
}

void FallGateway::publishEvent(const FallEvent &event) {
    const QByteArray message = buildEventMessage(event);
    for (int index = classificationSubscribers_.size() - 1; index >= 0; --index) {
        QLocalSocket *socket = classificationSubscribers_.at(index).data();
        if (!socket || socket->state() != QLocalSocket::ConnectedState) {
            classificationSubscribers_.removeAt(index);
            continue;
        }

        socket->write(message);
        socket->flush();
    }
}

void FallGateway::onNewConnection() {
    while (server_->hasPendingConnections()) {
        QLocalSocket *socket = server_->nextPendingConnection();
        if (!socket) {
            continue;
        }
        connect(socket, &QLocalSocket::readyRead, this, &FallGateway::onSocketReadyRead);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            removeSubscriber(socket);
            socket->deleteLater();
        });
    }
}

void FallGateway::onSocketReadyRead() {
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    const QByteArray request = socket->readAll();
    if (request.contains("get_runtime_status")) {
        socket->write(buildStatusResponse());
        socket->flush();
        return;
    }

    if (request.contains("subscribe_classification")) {
        if (!classificationSubscribers_.contains(socket)) {
            classificationSubscribers_.push_back(socket);
        }
    }
}

void FallGateway::removeSubscriber(QLocalSocket *socket) {
    for (int index = classificationSubscribers_.size() - 1; index >= 0; --index) {
        if (classificationSubscribers_.at(index).data() == socket) {
            classificationSubscribers_.removeAt(index);
        }
    }
}

QByteArray FallGateway::buildStatusResponse() const {
    QByteArray payload = QJsonDocument(fallRuntimeStatusToJson(status_)).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}

QByteArray FallGateway::buildClassificationMessage(const FallClassificationResult &result) const {
    QByteArray payload =
        QJsonDocument(fallClassificationResultToJson(result)).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}

QByteArray FallGateway::buildClassificationBatchMessage(const FallClassificationBatch &batch) const {
    QByteArray payload =
        QJsonDocument(fallClassificationBatchToJson(batch)).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}

QByteArray FallGateway::buildEventMessage(const FallEvent &event) const {
    QJsonObject json = fallEventToJson(event);
    json.insert(QStringLiteral("type"), QStringLiteral("fall_event"));
    QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}
