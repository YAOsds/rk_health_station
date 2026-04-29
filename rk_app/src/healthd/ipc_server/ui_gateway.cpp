#include "ipc_server/ui_gateway.h"

#include "device/device_manager.h"
#include "host/host_wifi_status_provider.h"
#include "runtime_config/app_runtime_config_loader.h"
#include "storage/database.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDateTime>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>

namespace {
const char kLineSeparator = '\n';
constexpr int kMaxBufferedBytes = 1024 * 1024;
}

QByteArray IpcCodec::encode(const IpcMessage &message) {
    const QJsonObject object = ipcMessageToJson(message);
    if (object.isEmpty()) {
        return {};
    }

    QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact);
    encoded.append(kLineSeparator);
    return encoded;
}

bool IpcCodec::decode(const QByteArray &buffer, IpcMessage *out) {
    if (!out) {
        return false;
    }

    const QByteArray trimmed = buffer.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    return ipcMessageFromJson(document.object(), out);
}

QString UiGateway::socketName() {
    return loadAppRuntimeConfig(QString()).config.ipc.healthSocketPath;
}

UiGateway::UiGateway(const QString &socketName, DeviceManager *deviceManager, Database *database,
    HostWifiStatusProvider *hostWifiStatusProvider, QObject *parent)
    : QObject(parent)
    , deviceManager_(deviceManager)
    , database_(database)
    , hostWifiStatusProvider_(hostWifiStatusProvider)
    , socketName_(socketName)
    , server_(new QLocalServer(this)) {
    connect(server_, &QLocalServer::newConnection, this, &UiGateway::onNewConnection);
}

UiGateway::UiGateway(DeviceManager *deviceManager, Database *database,
    HostWifiStatusProvider *hostWifiStatusProvider, QObject *parent)
    : UiGateway(socketName(), deviceManager, database, hostWifiStatusProvider, parent) {
}

UiGateway::~UiGateway() {
    stop();
}

bool UiGateway::start() {
    stop();
    QLocalServer::removeServer(socketName_);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    const bool ok = server_->listen(socketName_);
    if (!ok) {
        qWarning() << "ui gateway listen failed:" << server_->errorString();
    } else {
        qInfo() << "healthd ipc: gateway listening"
                << "socket_name=" << socketName_;
    }
    return ok;
}

void UiGateway::stop() {
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
    QLocalServer::removeServer(socketName_);
    qInfo() << "healthd ipc: gateway stopped"
            << "socket_name=" << socketName_;
}

void UiGateway::onNewConnection() {
    while (server_->hasPendingConnections()) {
        QLocalSocket *socket = server_->nextPendingConnection();
        if (!socket) {
            continue;
        }

        readBuffers_.insert(socket, QByteArray());
        qInfo() << "healthd ipc: ui client connected"
                << "client_count=" << readBuffers_.size();
        connect(socket, &QLocalSocket::readyRead, this, &UiGateway::onSocketReadyRead);
        connect(socket, &QLocalSocket::disconnected, this, &UiGateway::onSocketDisconnected);
    }
}

void UiGateway::onSocketReadyRead() {
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket || !readBuffers_.contains(socket)) {
        return;
    }

    QByteArray &buffer = readBuffers_[socket];
    buffer.append(socket->readAll());
    if (buffer.size() > kMaxBufferedBytes) {
        qWarning() << "healthd ipc: request buffer exceeded limit" << buffer.size();
        readBuffers_.remove(socket);
        socket->disconnectFromServer();
        return;
    }

    int separatorIndex = buffer.indexOf(kLineSeparator);
    while (separatorIndex >= 0) {
        const QByteArray frame = buffer.left(separatorIndex);
        buffer.remove(0, separatorIndex + 1);

        IpcMessage request;
        IpcMessage response;
        if (IpcCodec::decode(frame, &request)) {
            qInfo() << "healthd ipc: request received"
                    << "action=" << request.action
                    << "req_id=" << request.reqId;
            response = routeMessage(request);
        } else {
            qWarning() << "healthd ipc: invalid request frame";
            response = buildErrorResponse(
                QStringLiteral("invalid_request"), QString(), QStringLiteral("invalid_format"));
        }

        const QByteArray encoded = IpcCodec::encode(response);
        if (!encoded.isEmpty()) {
            qInfo() << "healthd ipc: response sent"
                    << "action=" << response.action
                    << "req_id=" << response.reqId
                    << "ok=" << response.ok;
            socket->write(encoded);
            socket->flush();
        }

        separatorIndex = buffer.indexOf(kLineSeparator);
    }
}

void UiGateway::onSocketDisconnected() {
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    readBuffers_.remove(socket);
    qInfo() << "healthd ipc: ui client disconnected"
            << "client_count=" << readBuffers_.size();
    socket->deleteLater();
}

IpcMessage UiGateway::routeMessage(const IpcMessage &message) const {
    if (message.action == QStringLiteral("get_device_list")) {
        return buildDeviceListResponse(message.reqId);
    }
    if (message.action == QStringLiteral("get_dashboard_snapshot")) {
        return buildDashboardResponse(message.reqId);
    }
    if (message.action == QStringLiteral("get_pending_devices")) {
        return buildPendingDeviceResponse(message.reqId);
    }
    if (message.action == QStringLiteral("get_alerts_snapshot")) {
        return buildAlertsSnapshotResponse(message.reqId);
    }
    if (message.action == QStringLiteral("get_history_series")) {
        return buildHistorySeriesResponse(message);
    }
    if (message.action == QStringLiteral("approve_device")) {
        return handleApproveDevice(message);
    }
    if (message.action == QStringLiteral("reject_device")) {
        return handleRejectDevice(message);
    }
    if (message.action == QStringLiteral("rename_device")) {
        return handleRenameDevice(message);
    }
    if (message.action == QStringLiteral("set_device_enabled")) {
        return handleSetDeviceEnabled(message);
    }
    if (message.action == QStringLiteral("reset_device_secret")) {
        return handleResetDeviceSecret(message);
    }
    return buildErrorResponse(message.action, message.reqId, QStringLiteral("unsupported_action"));
}

IpcMessage UiGateway::buildDeviceListResponse(const QString &reqId) const {
    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = QStringLiteral("get_device_list");
    response.reqId = reqId;
    response.ok = true;

    QJsonArray devices;
    if (deviceManager_) {
        const QList<DeviceManager::DeviceRecord> records = deviceManager_->allDevices();
        for (const DeviceManager::DeviceRecord &record : records) {
            appendDeviceJson(&devices, record.info.deviceId, record.info.deviceName,
                deviceLifecycleStateToString(record.info.status), record.runtime.online,
                record.runtime.lastSeenAt, record.runtime.remoteIp);
        }
    }
    response.payload.insert(QStringLiteral("devices"), devices);
    return response;
}

IpcMessage UiGateway::buildDashboardResponse(const QString &reqId) const {
    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = QStringLiteral("get_dashboard_snapshot");
    response.reqId = reqId;
    response.ok = true;

    QJsonArray devices;
    QString currentDeviceId;
    if (deviceManager_) {
        const QList<DeviceManager::DeviceRecord> records = deviceManager_->allDevices();
        for (const DeviceManager::DeviceRecord &record : records) {
            appendDeviceJson(&devices, record.info.deviceId, record.info.deviceName,
                deviceLifecycleStateToString(record.info.status), record.runtime.online,
                record.runtime.lastSeenAt, record.runtime.remoteIp);
            if (currentDeviceId.isEmpty() && record.info.status == DeviceLifecycleState::Active) {
                currentDeviceId = record.info.deviceId;
            }
        }
        if (currentDeviceId.isEmpty() && !records.isEmpty()) {
            currentDeviceId = records.first().info.deviceId;
        }
    }

    response.payload.insert(QStringLiteral("device_count"), devices.size());
    response.payload.insert(QStringLiteral("current_device_id"), currentDeviceId);
    response.payload.insert(QStringLiteral("devices"), devices);
    const HostWifiStatus hostWifi = hostWifiStatusProvider_
        ? hostWifiStatusProvider_->currentStatus()
        : HostWifiStatus();
    response.payload.insert(QStringLiteral("host_wifi"), hostWifiToJson(hostWifi));
    return response;
}

IpcMessage UiGateway::buildPendingDeviceResponse(const QString &reqId) const {
    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = QStringLiteral("get_pending_devices");
    response.reqId = reqId;
    response.ok = true;

    QJsonArray pendingDevices;
    if (deviceManager_) {
        const QList<DeviceManager::PendingRecord> pending = deviceManager_->pendingRequests();
        for (const DeviceManager::PendingRecord &record : pending) {
            QJsonObject object;
            object.insert(QStringLiteral("device_id"), record.deviceId);
            object.insert(QStringLiteral("proposed_name"), record.proposedName);
            object.insert(QStringLiteral("firmware_version"), record.firmwareVersion);
            object.insert(QStringLiteral("hardware_model"), record.hardwareModel);
            object.insert(QStringLiteral("ip"), record.ip);
            object.insert(QStringLiteral("request_time"), static_cast<double>(record.requestTime));
            pendingDevices.append(object);
        }
    }

    response.payload.insert(QStringLiteral("pending_devices"), pendingDevices);
    return response;
}

IpcMessage UiGateway::buildAlertsSnapshotResponse(const QString &reqId) const {
    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = QStringLiteral("get_alerts_snapshot");
    response.reqId = reqId;
    response.ok = true;

    QJsonArray alerts;
    if (deviceManager_) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const QList<DeviceManager::DeviceRecord> records = deviceManager_->allDevices();
        for (const DeviceManager::DeviceRecord &record : records) {
            AlertInput input;
            input.deviceId = record.info.deviceId;
            input.lifecycle = record.info.status;
            input.online = record.runtime.online;
            input.lastSeenAt = record.runtime.lastSeenAt;
            input.now = now;

            if (database_) {
                Database::LatestTelemetryRecord latest;
                QString error;
                if (database_->fetchLatestTelemetry(record.info.deviceId, &latest, &error)) {
                    input.latest.sampleTime = latest.sample.sampleTime;
                    if (latest.heartRate.isValid() && !latest.heartRate.isNull()) {
                        input.latest.heartRate = latest.heartRate.toInt();
                    }
                    if (latest.spo2.isValid() && !latest.spo2.isNull()) {
                        input.latest.spo2 = latest.spo2.toDouble();
                    }
                    if (latest.battery.isValid() && !latest.battery.isNull()) {
                        input.latest.battery = latest.battery.toInt();
                    }
                    if (input.lastSeenAt <= 0) {
                        input.lastSeenAt = latest.sample.sampleTime;
                    }
                }
            }

            const QVector<Alert> activeAlerts = alertEngine_.evaluate(input);
            for (const Alert &alert : activeAlerts) {
                QJsonObject object;
                object.insert(QStringLiteral("device_id"), record.info.deviceId);
                object.insert(QStringLiteral("device_name"), record.info.deviceName);
                object.insert(QStringLiteral("alert_id"), alert.id);
                object.insert(QStringLiteral("severity"), alert.severity);
                object.insert(QStringLiteral("since"), static_cast<double>(alert.since));
                object.insert(QStringLiteral("message"), alert.message);
                alerts.append(object);
            }
        }
    }

    response.payload.insert(QStringLiteral("alerts"), alerts);
    return response;
}

IpcMessage UiGateway::buildHistorySeriesResponse(const IpcMessage &message) const {
    if (!database_) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("backend_unavailable"));
    }

    const QString deviceId = message.payload.value(QStringLiteral("device_id")).toString();
    qint64 fromTs = 0;
    qint64 toTs = QDateTime::currentSecsSinceEpoch();
    if (deviceId.isEmpty()
        || !readInt64Value(message.payload.value(QStringLiteral("from_ts")), &fromTs)
        || !readInt64Value(message.payload.value(QStringLiteral("to_ts")), &toTs)
        || fromTs > toTs) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("invalid_history_request"));
    }

    QString error;
    const QList<Database::TelemetryMinuteAggRow> rows
        = database_->fetchTelemetryMinuteAgg(deviceId, fromTs, toTs, &error);
    if (!error.isEmpty()) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("history_query_failed"));
    }

    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = QStringLiteral("get_history_series");
    response.reqId = message.reqId;
    response.ok = true;

    QJsonArray series;
    for (const Database::TelemetryMinuteAggRow &row : rows) {
        QJsonObject object;
        object.insert(QStringLiteral("minute_ts"), static_cast<double>(row.minuteTs));
        object.insert(QStringLiteral("samples_total"), row.samplesTotal);
        if (row.hrCount > 0) {
            object.insert(QStringLiteral("avg_heart_rate"),
                static_cast<double>(row.hrSum) / static_cast<double>(row.hrCount));
            object.insert(QStringLiteral("min_heart_rate"), row.hrMin.toInt());
            object.insert(QStringLiteral("max_heart_rate"), row.hrMax.toInt());
        }
        if (row.spo2Count > 0) {
            object.insert(QStringLiteral("avg_spo2"),
                row.spo2Sum / static_cast<double>(row.spo2Count));
            object.insert(QStringLiteral("min_spo2"), row.spo2Min.toDouble());
            object.insert(QStringLiteral("max_spo2"), row.spo2Max.toDouble());
        }
        if (row.batteryCount > 0) {
            object.insert(QStringLiteral("avg_battery"),
                static_cast<double>(row.batterySum) / static_cast<double>(row.batteryCount));
            object.insert(QStringLiteral("min_battery"), row.batteryMin.toInt());
            object.insert(QStringLiteral("max_battery"), row.batteryMax.toInt());
        }
        series.append(object);
    }

    response.payload.insert(QStringLiteral("device_id"), deviceId);
    response.payload.insert(QStringLiteral("series"), series);
    return response;
}

IpcMessage UiGateway::handleApproveDevice(const IpcMessage &message) const {
    if (!deviceManager_) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("backend_unavailable"));
    }

    const QString deviceId = message.payload.value(QStringLiteral("device_id")).toString();
    const QString deviceName = message.payload.value(QStringLiteral("device_name")).toString();
    const QString secretHash = message.payload.value(QStringLiteral("secret_hash")).toString();
    if (!deviceManager_->approveDevice(deviceId, deviceName, secretHash)) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("approve_failed"));
    }

    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = message.action;
    response.reqId = message.reqId;
    response.ok = true;
    return response;
}

IpcMessage UiGateway::handleRejectDevice(const IpcMessage &message) const {
    if (!deviceManager_) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("backend_unavailable"));
    }

    const QString deviceId = message.payload.value(QStringLiteral("device_id")).toString();
    if (!deviceManager_->rejectDevice(deviceId)) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("reject_failed"));
    }

    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = message.action;
    response.reqId = message.reqId;
    response.ok = true;
    return response;
}

IpcMessage UiGateway::handleRenameDevice(const IpcMessage &message) const {
    if (!deviceManager_) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("backend_unavailable"));
    }

    const QString deviceId = message.payload.value(QStringLiteral("device_id")).toString();
    const QString deviceName = message.payload.value(QStringLiteral("device_name")).toString();
    if (!deviceManager_->renameDevice(deviceId, deviceName)) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("rename_failed"));
    }

    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = message.action;
    response.reqId = message.reqId;
    response.ok = true;
    return response;
}

IpcMessage UiGateway::handleSetDeviceEnabled(const IpcMessage &message) const {
    if (!deviceManager_) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("backend_unavailable"));
    }

    const QString deviceId = message.payload.value(QStringLiteral("device_id")).toString();
    const QJsonValue enabledValue = message.payload.value(QStringLiteral("enabled"));
    if (!enabledValue.isBool()
        || !deviceManager_->setDeviceEnabled(deviceId, enabledValue.toBool())) {
        return buildErrorResponse(
            message.action, message.reqId, QStringLiteral("set_enabled_failed"));
    }

    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = message.action;
    response.reqId = message.reqId;
    response.ok = true;
    return response;
}

IpcMessage UiGateway::handleResetDeviceSecret(const IpcMessage &message) const {
    if (!deviceManager_) {
        return buildErrorResponse(message.action, message.reqId, QStringLiteral("backend_unavailable"));
    }

    const QString deviceId = message.payload.value(QStringLiteral("device_id")).toString();
    const QString secretHash = message.payload.value(QStringLiteral("secret_hash")).toString();
    if (!deviceManager_->resetSecret(deviceId, secretHash)) {
        return buildErrorResponse(
            message.action, message.reqId, QStringLiteral("reset_secret_failed"));
    }

    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = message.action;
    response.reqId = message.reqId;
    response.ok = true;
    return response;
}

IpcMessage UiGateway::buildErrorResponse(
    const QString &action, const QString &reqId, const QString &errorCode) const {
    IpcMessage response;
    response.kind = QStringLiteral("response");
    response.action = action.isEmpty() ? QStringLiteral("error") : action;
    response.reqId = reqId;
    response.ok = false;
    response.payload.insert(QStringLiteral("error"), errorCode);
    return response;
}

QJsonObject UiGateway::hostWifiToJson(const HostWifiStatus &status) {
    QJsonObject object;
    object.insert(QStringLiteral("present"), status.present);
    object.insert(QStringLiteral("connected"), status.connected);
    object.insert(QStringLiteral("interface_name"), status.interfaceName);
    object.insert(QStringLiteral("ssid"), status.ssid);
    object.insert(QStringLiteral("ipv4"), status.ipv4);
    return object;
}

void UiGateway::appendDeviceJson(QJsonArray *devices, const QString &deviceId,
    const QString &deviceName, const QString &status, bool online, qint64 lastSeenAt,
    const QString &remoteIp) {
    if (!devices) {
        return;
    }

    QJsonObject object;
    object.insert(QStringLiteral("device_id"), deviceId);
    object.insert(QStringLiteral("device_name"), deviceName);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("online"), online);
    object.insert(QStringLiteral("last_seen_at"), static_cast<double>(lastSeenAt));
    object.insert(QStringLiteral("remote_ip"), remoteIp);
    devices->append(object);
}

bool UiGateway::readInt64Value(const QJsonValue &value, qint64 *out) {
    if (!out || !value.isDouble()) {
        return false;
    }
    *out = static_cast<qint64>(value.toDouble());
    return true;
}
