#include "ipc_client/ui_ipc_client.h"

#include "runtime_config/app_runtime_config_loader.h"

#include <QDebug>
#include <QJsonDocument>
#include <QLocalSocket>

namespace {
const char kLineSeparator = '\n';

QByteArray encodeMessage(const IpcMessage &message) {
    const QJsonObject object = ipcMessageToJson(message);
    if (object.isEmpty()) {
        return {};
    }

    QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact);
    encoded.append(kLineSeparator);
    return encoded;
}

bool decodeMessage(const QByteArray &buffer, IpcMessage *message) {
    if (!message) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(buffer.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    return ipcMessageFromJson(document.object(), message);
}
}

UiIpcClient::UiIpcClient(const QString &socketName, QObject *parent)
    : QObject(parent)
    , socketName_(socketName.isEmpty()
            ? loadAppRuntimeConfig(QString()).config.ipc.healthSocketPath
            : socketName)
    , socket_(new QLocalSocket(this)) {
    connect(socket_, &QLocalSocket::readyRead, this, &UiIpcClient::onReadyRead);
    connect(socket_, &QLocalSocket::connected, this, [this]() {
        qInfo() << "health-ui ipc: connected to backend"
                << "socket_name=" << socket_->fullServerName();
    });
    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
        qWarning() << "health-ui ipc: disconnected from backend"
                   << "socket_name=" << socket_->fullServerName();
    });
    connect(socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        qWarning() << "health-ui ipc: socket error"
                   << socket_->errorString();
    });
}

bool UiIpcClient::connectToBackend() {
    if (isConnected()) {
        return true;
    }

    socket_->connectToServer(socketName_);
    const bool ok = socket_->waitForConnected(3000);
    if (!ok) {
        qWarning() << "health-ui ipc: connect failed"
                   << "socket_name=" << socketName_
                   << "error=" << socket_->errorString();
    }
    return ok;
}

bool UiIpcClient::isConnected() const {
    return socket_ && socket_->state() == QLocalSocket::ConnectedState;
}

void UiIpcClient::requestDeviceList() {
    sendRequest(QStringLiteral("get_device_list"),
        QStringLiteral("ui-%1").arg(nextRequestId_++));
}

void UiIpcClient::requestDashboardSnapshot() {
    sendRequest(QStringLiteral("get_dashboard_snapshot"),
        QStringLiteral("ui-%1").arg(nextRequestId_++));
}

void UiIpcClient::requestPendingDevices() {
    sendRequest(QStringLiteral("get_pending_devices"),
        QStringLiteral("ui-%1").arg(nextRequestId_++));
}

void UiIpcClient::requestAlertsSnapshot() {
    sendRequest(QStringLiteral("get_alerts_snapshot"),
        QStringLiteral("ui-%1").arg(nextRequestId_++));
}

void UiIpcClient::requestHistorySeries(const QString &deviceId, qint64 fromTs, qint64 toTs) {
    QJsonObject payload;
    payload.insert(QStringLiteral("device_id"), deviceId);
    payload.insert(QStringLiteral("from_ts"), static_cast<double>(fromTs));
    payload.insert(QStringLiteral("to_ts"), static_cast<double>(toTs));
    sendRequest(QStringLiteral("get_history_series"),
        QStringLiteral("ui-%1").arg(nextRequestId_++), payload);
}

void UiIpcClient::approveDevice(
    const QString &deviceId, const QString &deviceName, const QString &secretHash) {
    QJsonObject payload;
    payload.insert(QStringLiteral("device_id"), deviceId);
    payload.insert(QStringLiteral("device_name"), deviceName);
    payload.insert(QStringLiteral("secret_hash"), secretHash);
    sendRequest(QStringLiteral("approve_device"), QStringLiteral("ui-%1").arg(nextRequestId_++), payload);
}

void UiIpcClient::rejectDevice(const QString &deviceId) {
    QJsonObject payload;
    payload.insert(QStringLiteral("device_id"), deviceId);
    sendRequest(QStringLiteral("reject_device"), QStringLiteral("ui-%1").arg(nextRequestId_++), payload);
}

void UiIpcClient::renameDevice(const QString &deviceId, const QString &deviceName) {
    QJsonObject payload;
    payload.insert(QStringLiteral("device_id"), deviceId);
    payload.insert(QStringLiteral("device_name"), deviceName);
    sendRequest(QStringLiteral("rename_device"), QStringLiteral("ui-%1").arg(nextRequestId_++), payload);
}

void UiIpcClient::setDeviceEnabled(const QString &deviceId, bool enabled) {
    QJsonObject payload;
    payload.insert(QStringLiteral("device_id"), deviceId);
    payload.insert(QStringLiteral("enabled"), enabled);
    sendRequest(QStringLiteral("set_device_enabled"), QStringLiteral("ui-%1").arg(nextRequestId_++), payload);
}

void UiIpcClient::resetDeviceSecret(const QString &deviceId, const QString &secretHash) {
    QJsonObject payload;
    payload.insert(QStringLiteral("device_id"), deviceId);
    payload.insert(QStringLiteral("secret_hash"), secretHash);
    sendRequest(QStringLiteral("reset_device_secret"), QStringLiteral("ui-%1").arg(nextRequestId_++), payload);
}

void UiIpcClient::onReadyRead() {
    readBuffer_.append(socket_->readAll());

    int separatorIndex = readBuffer_.indexOf(kLineSeparator);
    while (separatorIndex >= 0) {
        const QByteArray frame = readBuffer_.left(separatorIndex);
        readBuffer_.remove(0, separatorIndex + 1);

        IpcMessage message;
        if (decodeMessage(frame, &message)) {
            handleMessage(message);
        } else {
            qWarning() << "health-ui ipc: failed to decode response frame";
        }

        separatorIndex = readBuffer_.indexOf(kLineSeparator);
    }
}

void UiIpcClient::sendRequest(const QString &action, const QString &reqId, const QJsonObject &payload) {
    if (!isConnected()) {
        qWarning() << "health-ui ipc: skipped request because backend is disconnected"
                   << "action=" << action
                   << "req_id=" << reqId;
        return;
    }

    IpcMessage request;
    request.kind = QStringLiteral("request");
    request.action = action;
    request.reqId = reqId;
    request.ok = true;
    request.payload = payload;

    const QByteArray encoded = encodeMessage(request);
    if (!encoded.isEmpty()) {
        socket_->write(encoded);
        socket_->flush();
    }
}

void UiIpcClient::handleMessage(const IpcMessage &message) {
    if (message.kind != QStringLiteral("response")) {
        qWarning() << "health-ui ipc: ignored non-response message"
                   << "kind=" << message.kind
                   << "action=" << message.action;
        return;
    }

    if (message.action == QStringLiteral("get_device_list")) {
        emit deviceListReceived(message.payload.value(QStringLiteral("devices")).toArray());
        return;
    }
    if (message.action == QStringLiteral("get_dashboard_snapshot")) {
        emit dashboardSnapshotReceived(message.payload);
        return;
    }
    if (message.action == QStringLiteral("get_pending_devices")) {
        emit pendingDevicesReceived(message.payload.value(QStringLiteral("pending_devices")).toArray());
        return;
    }
    if (message.action == QStringLiteral("get_alerts_snapshot")) {
        emit alertsSnapshotReceived(message.payload.value(QStringLiteral("alerts")).toArray());
        return;
    }
    if (message.action == QStringLiteral("get_history_series")) {
        emit historySeriesReceived(message.payload);
        return;
    }

    emit operationFinished(message.action, message.ok, message.payload);
}
