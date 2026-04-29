#include "ipc_client/video_ipc_client.h"

#include "runtime_config/app_runtime_config_loader.h"

#include <QDebug>
#include <QJsonDocument>
#include <QLocalSocket>

namespace {
const char kLineSeparator = '\n';
}

VideoIpcClient::VideoIpcClient(const QString &socketName, QObject *parent)
    : AbstractVideoClient(parent)
    , socketName_(socketName.isEmpty()
            ? loadAppRuntimeConfig(QString()).config.ipc.videoSocketPath
            : socketName)
    , socket_(new QLocalSocket(this)) {
    connect(socket_, &QLocalSocket::readyRead, this, &VideoIpcClient::onReadyRead);
}

bool VideoIpcClient::connectToBackend() {
    if (socket_->state() == QLocalSocket::ConnectedState) {
        return true;
    }

    socket_->connectToServer(socketName_);
    const bool connected = socket_->waitForConnected(3000);
    qInfo() << "health-ui video ipc: connect result"
            << connected
            << socketName_
            << socket_->errorString();
    return connected;
}

void VideoIpcClient::requestStatus(const QString &cameraId) {
    sendCommand(QStringLiteral("get_status"), cameraId);
}

void VideoIpcClient::takeSnapshot(const QString &cameraId) {
    sendCommand(QStringLiteral("take_snapshot"), cameraId);
}

void VideoIpcClient::startRecording(const QString &cameraId) {
    sendCommand(QStringLiteral("start_recording"), cameraId);
}

void VideoIpcClient::stopRecording(const QString &cameraId) {
    sendCommand(QStringLiteral("stop_recording"), cameraId);
}

void VideoIpcClient::setStorageDir(const QString &cameraId, const QString &storageDir) {
    QJsonObject payload;
    payload.insert(QStringLiteral("storage_dir"), storageDir);
    sendCommand(QStringLiteral("set_storage_dir"), cameraId, payload);
}

void VideoIpcClient::startTestInput(const QString &cameraId, const QString &filePath) {
    QJsonObject payload;
    payload.insert(QStringLiteral("file_path"), filePath);
    sendCommand(QStringLiteral("start_test_input"), cameraId, payload);
}

void VideoIpcClient::stopTestInput(const QString &cameraId) {
    sendCommand(QStringLiteral("stop_test_input"), cameraId);
}

void VideoIpcClient::onReadyRead() {
    readBuffer_.append(socket_->readAll());

    int separatorIndex = readBuffer_.indexOf(kLineSeparator);
    while (separatorIndex >= 0) {
        const QByteArray frame = readBuffer_.left(separatorIndex);
        readBuffer_.remove(0, separatorIndex + 1);

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(frame.trimmed(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            qWarning() << "health-ui video ipc: invalid response frame";
            separatorIndex = readBuffer_.indexOf(kLineSeparator);
            continue;
        }

        VideoCommandResult result;
        if (!videoCommandResultFromJson(document.object(), &result)) {
            qWarning() << "health-ui video ipc: failed to decode response";
            separatorIndex = readBuffer_.indexOf(kLineSeparator);
            continue;
        }
        if (result.action == QStringLiteral("get_status") || result.action == QStringLiteral("start_preview")) {
            VideoChannelStatus status;
            if (videoChannelStatusFromJson(result.payload, &status)) {
                emit statusReceived(status);
            }
        }
        emit commandFinished(result);
        separatorIndex = readBuffer_.indexOf(kLineSeparator);
    }
}

void VideoIpcClient::sendCommand(
    const QString &action, const QString &cameraId, const QJsonObject &payload) {
    if (!connectToBackend()) {
        qWarning() << "health-ui video ipc: skipped command because backend is disconnected"
                   << action;
        return;
    }

    VideoCommand command;
    command.action = action;
    command.requestId = QStringLiteral("video-ui-%1").arg(nextRequestId_++);
    command.cameraId = cameraId;
    command.payload = payload;

    QByteArray encoded = QJsonDocument(videoCommandToJson(command)).toJson(QJsonDocument::Compact);
    encoded.append(kLineSeparator);
    socket_->write(encoded);
    socket_->flush();
}
