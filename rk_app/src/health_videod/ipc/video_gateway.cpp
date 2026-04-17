#include "ipc/video_gateway.h"

#include "core/video_service.h"

#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

namespace {
const char kSocketName[] = "rk_video.sock";
const char kSocketEnvVar[] = "RK_VIDEO_SOCKET_NAME";
const char kLineSeparator = '\n';
}

QString VideoGateway::socketName() {
    const QString overrideName = qEnvironmentVariable(kSocketEnvVar);
    return overrideName.isEmpty() ? QString::fromUtf8(kSocketName) : overrideName;
}

VideoGateway::VideoGateway(VideoService *service, QObject *parent)
    : QObject(parent)
    , service_(service)
    , server_(new QLocalServer(this)) {
    connect(server_, &QLocalServer::newConnection, this, &VideoGateway::onNewConnection);
}

VideoGateway::~VideoGateway() {
    stop();
}

bool VideoGateway::start() {
    stop();
    QLocalServer::removeServer(socketName());
    return server_->listen(socketName());
}

void VideoGateway::stop() {
    const auto sockets = readBuffers_.keys();
    for (QLocalSocket *socket : sockets) {
        if (socket) {
            socket->disconnect(this);
            socket->disconnectFromServer();
            socket->deleteLater();
        }
    }
    readBuffers_.clear();

    if (server_->isListening()) {
        server_->close();
    }
    QLocalServer::removeServer(socketName());
}

void VideoGateway::onNewConnection() {
    while (server_->hasPendingConnections()) {
        QLocalSocket *socket = server_->nextPendingConnection();
        if (!socket) {
            continue;
        }
        readBuffers_.insert(socket, QByteArray());
        connect(socket, &QLocalSocket::readyRead, this, &VideoGateway::onSocketReadyRead);
        connect(socket, &QLocalSocket::disconnected, this, &VideoGateway::onSocketDisconnected);
    }
}

void VideoGateway::onSocketReadyRead() {
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket || !readBuffers_.contains(socket)) {
        return;
    }

    QByteArray &buffer = readBuffers_[socket];
    buffer.append(socket->readAll());

    int separatorIndex = buffer.indexOf(kLineSeparator);
    while (separatorIndex >= 0) {
        const QByteArray frame = buffer.left(separatorIndex);
        buffer.remove(0, separatorIndex + 1);

        VideoCommand command;
        VideoCommandResult result;
        if (decodeCommand(frame, &command)) {
            result = route(command);
            result.requestId = command.requestId;
        } else {
            result = buildErrorResult(QStringLiteral("invalid_request"), QString(),
                QString(), QStringLiteral("invalid_format"));
        }

        socket->write(encodeResult(result));
        socket->flush();

        separatorIndex = buffer.indexOf(kLineSeparator);
    }
}

void VideoGateway::onSocketDisconnected() {
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }
    readBuffers_.remove(socket);
    socket->deleteLater();
}

QByteArray VideoGateway::encodeResult(const VideoCommandResult &result) const {
    QByteArray encoded = QJsonDocument(videoCommandResultToJson(result)).toJson(QJsonDocument::Compact);
    encoded.append(kLineSeparator);
    return encoded;
}

bool VideoGateway::decodeCommand(const QByteArray &frame, VideoCommand *command) const {
    if (!command) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(frame.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    return videoCommandFromJson(document.object(), command);
}

VideoCommandResult VideoGateway::route(const VideoCommand &command) const {
    if (!service_) {
        return buildErrorResult(command.action, command.requestId, command.cameraId,
            QStringLiteral("service_unavailable"));
    }

    if (command.action == QStringLiteral("get_status")) {
        return buildStatusResult(command.cameraId, command.requestId);
    }
    if (command.action == QStringLiteral("start_preview")) {
        return service_->startPreview(command.cameraId);
    }
    if (command.action == QStringLiteral("take_snapshot")) {
        return service_->takeSnapshot(command.cameraId);
    }
    if (command.action == QStringLiteral("start_recording")) {
        return service_->startRecording(command.cameraId);
    }
    if (command.action == QStringLiteral("stop_recording")) {
        return service_->stopRecording(command.cameraId);
    }
    if (command.action == QStringLiteral("set_storage_dir")) {
        return service_->applyStorageDir(
            command.cameraId, command.payload.value(QStringLiteral("storage_dir")).toString());
    }

    return buildErrorResult(command.action, command.requestId, command.cameraId,
        QStringLiteral("unsupported_action"));
}

VideoCommandResult VideoGateway::buildStatusResult(
    const QString &cameraId, const QString &requestId) const {
    const VideoChannelStatus status = service_->statusForCamera(cameraId);
    if (status.cameraId.isEmpty()) {
        return buildErrorResult(
            QStringLiteral("get_status"), requestId, cameraId, QStringLiteral("camera_not_found"));
    }

    VideoCommandResult result;
    result.action = QStringLiteral("get_status");
    result.requestId = requestId;
    result.cameraId = cameraId;
    result.ok = true;
    result.payload = videoChannelStatusToJson(status);
    return result;
}

VideoCommandResult VideoGateway::buildErrorResult(const QString &action,
    const QString &requestId, const QString &cameraId, const QString &errorCode) const {
    VideoCommandResult result;
    result.action = action;
    result.requestId = requestId;
    result.cameraId = cameraId;
    result.ok = false;
    result.errorCode = errorCode;
    return result;
}
