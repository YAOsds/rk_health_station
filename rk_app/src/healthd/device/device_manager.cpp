#include "device/device_manager.h"

#include "storage/database.h"

#include <QDateTime>
#include <QDebug>
#include <algorithm>

namespace {
qint64 currentEpochSeconds() {
    return QDateTime::currentSecsSinceEpoch();
}
} // namespace

DeviceManager::DeviceManager(Database *database)
    : database_(database) {
}

void DeviceManager::setDatabase(Database *database) {
    database_ = database;
}

bool DeviceManager::ensureRegistered(
    const QString &deviceId, qint64 lastSeenAt, const QString &remoteIp) {
    if (deviceId.isEmpty()) {
        return false;
    }

    hydrateFromDatabase(deviceId);

    DeviceInfo info = devices_.value(deviceId);
    if (info.deviceId.isEmpty()) {
        info.deviceId = deviceId;
        info.deviceName = deviceId;
        info.status = DeviceLifecycleState::Active;
        devices_.insert(deviceId, info);
    }

    DeviceStatus status = runtime_.value(deviceId);
    status.deviceId = deviceId;
    status.status = info.status;
    status.online = info.status == DeviceLifecycleState::Active;
    status.lastSeenAt = lastSeenAt > 0 ? lastSeenAt : currentEpochSeconds();
    if (!remoteIp.isEmpty()) {
        status.remoteIp = remoteIp;
    }
    runtime_.insert(deviceId, status);

    return persistDevice(deviceId, lastRssi_.value(deviceId, 0));
}

bool DeviceManager::updateMetadata(
    const DeviceInfo &info, std::optional<QString> secretHash, std::optional<int> lastRssi) {
    if (info.deviceId.isEmpty()) {
        return false;
    }

    hydrateFromDatabase(info.deviceId);

    devices_.insert(info.deviceId, info);
    if (secretHash.has_value()) {
        secretHashes_.insert(info.deviceId, secretHash.value());
    }
    if (lastRssi.has_value()) {
        lastRssi_.insert(info.deviceId, lastRssi.value());
    }

    DeviceStatus runtime = runtime_.value(info.deviceId);
    runtime.deviceId = info.deviceId;
    runtime.status = info.status;
    if (info.status != DeviceLifecycleState::Active) {
        runtime.online = false;
    }
    runtime_.insert(info.deviceId, runtime);

    return persistDevice(info.deviceId, lastRssi_.value(info.deviceId, 0));
}

bool DeviceManager::applyTelemetry(
    const TelemetrySample &sample, const QString &remoteIp, std::optional<int> lastRssi) {
    if (sample.deviceId.isEmpty()) {
        return false;
    }

    hydrateFromDatabase(sample.deviceId);

    if (!ensureRegistered(sample.deviceId, sample.sampleTime, remoteIp)) {
        return false;
    }

    if (lastRssi.has_value()) {
        lastRssi_.insert(sample.deviceId, lastRssi.value());
    }

    DeviceStatus status = runtime_.value(sample.deviceId);
    status.deviceId = sample.deviceId;
    status.status = devices_.value(sample.deviceId).status;
    status.online = status.status == DeviceLifecycleState::Active;
    status.lastSeenAt = sample.sampleTime > 0 ? sample.sampleTime : currentEpochSeconds();
    if (!remoteIp.isEmpty()) {
        status.remoteIp = remoteIp;
    }
    runtime_.insert(sample.deviceId, status);

    return persistDevice(sample.deviceId, lastRssi_.value(sample.deviceId, 0));
}

bool DeviceManager::approveDevice(
    const QString &deviceId, const QString &deviceName, const QString &secretHash) {
    if (!database_ || deviceId.isEmpty() || deviceName.isEmpty() || secretHash.isEmpty()) {
        return false;
    }

    Database::PendingDeviceRequest pending;
    QString error;
    if (!database_->fetchPendingRequest(deviceId, &pending, &error)) {
        return false;
    }

    DeviceInfo info;
    info.deviceId = deviceId;
    info.deviceName = deviceName;
    info.deviceSecret = secretHash;
    info.status = DeviceLifecycleState::Active;
    info.bindMode = QStringLiteral("approved");
    info.model = pending.hardwareModel;
    info.firmwareVersion = pending.firmwareVersion;

    DeviceStatus runtime = runtime_.value(deviceId);
    runtime.deviceId = deviceId;
    runtime.status = DeviceLifecycleState::Active;
    runtime.online = false;
    runtime.remoteIp = pending.ip;
    if (runtime.lastSeenAt == 0) {
        runtime.lastSeenAt = pending.requestTime;
    }

    if (!database_->beginTransaction(&error)) {
        return false;
    }

    const bool deviceOk = database_->upsertDevice(
        info, runtime, secretHash, lastRssi_.value(deviceId, 0), &error);
    const bool pendingOk = deviceOk && database_->deletePendingRequest(deviceId, &error);
    const bool auditOk = pendingOk
        && database_->insertAuditLog(
            deviceId, QStringLiteral("approve_device"), deviceName, &error);
    const bool commitOk = auditOk && database_->commitTransaction(&error);
    if (!commitOk) {
        database_->rollbackTransaction();
        reloadFromDatabase(deviceId);
        return false;
    }

    devices_.insert(deviceId, info);
    runtime_.insert(deviceId, runtime);
    secretHashes_.insert(deviceId, secretHash);
    return true;
}

bool DeviceManager::rejectDevice(const QString &deviceId) {
    if (!database_ || deviceId.isEmpty()) {
        return false;
    }

    QString error;
    if (!database_->beginTransaction(&error)) {
        return false;
    }
    const bool deleteOk = database_->deletePendingRequest(deviceId, &error);
    const bool auditOk = deleteOk
        && database_->insertAuditLog(
            deviceId, QStringLiteral("reject_device"), QStringLiteral(""), &error);
    const bool commitOk = auditOk && database_->commitTransaction(&error);
    if (!commitOk) {
        database_->rollbackTransaction();
        return false;
    }

    devices_.remove(deviceId);
    runtime_.remove(deviceId);
    secretHashes_.remove(deviceId);
    lastRssi_.remove(deviceId);
    return true;
}

bool DeviceManager::renameDevice(const QString &deviceId, const QString &deviceName) {
    if (deviceId.isEmpty() || deviceName.isEmpty()) {
        return false;
    }

    hydrateFromDatabase(deviceId);
    DeviceInfo info = devices_.value(deviceId);
    if (info.deviceId.isEmpty()) {
        return false;
    }
    info.deviceName = deviceName;
    devices_.insert(deviceId, info);
    if (!database_) {
        return persistDevice(deviceId, lastRssi_.value(deviceId, 0));
    }

    QString error;
    if (!database_->beginTransaction(&error)) {
        return false;
    }
    if (!persistDevice(deviceId, lastRssi_.value(deviceId, 0))
        || !database_->insertAuditLog(
            deviceId, QStringLiteral("rename_device"), deviceName, &error)
        || !database_->commitTransaction(&error)) {
        database_->rollbackTransaction();
        reloadFromDatabase(deviceId);
        return false;
    }
    return true;
}

bool DeviceManager::setDeviceEnabled(const QString &deviceId, bool enabled) {
    if (deviceId.isEmpty()) {
        return false;
    }

    hydrateFromDatabase(deviceId);
    DeviceInfo info = devices_.value(deviceId);
    if (info.deviceId.isEmpty()) {
        return false;
    }

    info.status = enabled ? DeviceLifecycleState::Active : DeviceLifecycleState::Disabled;
    devices_.insert(deviceId, info);

    DeviceStatus runtime = runtime_.value(deviceId);
    runtime.deviceId = deviceId;
    runtime.status = info.status;
    runtime.online = enabled && runtime.online;
    runtime_.insert(deviceId, runtime);
    if (!database_) {
        return persistDevice(deviceId, lastRssi_.value(deviceId, 0));
    }

    QString error;
    if (!database_->beginTransaction(&error)) {
        return false;
    }
    if (!persistDevice(deviceId, lastRssi_.value(deviceId, 0))
        || !database_->insertAuditLog(deviceId, QStringLiteral("set_device_enabled"),
            enabled ? QStringLiteral("true") : QStringLiteral("false"), &error)
        || !database_->commitTransaction(&error)) {
        database_->rollbackTransaction();
        reloadFromDatabase(deviceId);
        return false;
    }
    return true;
}

bool DeviceManager::resetSecret(const QString &deviceId, const QString &secretHash) {
    if (deviceId.isEmpty() || secretHash.isEmpty()) {
        return false;
    }

    hydrateFromDatabase(deviceId);
    if (!devices_.contains(deviceId)) {
        return false;
    }

    secretHashes_.insert(deviceId, secretHash);
    DeviceInfo info = devices_.value(deviceId);
    info.deviceSecret = secretHash;
    devices_.insert(deviceId, info);
    if (!database_) {
        return persistDevice(deviceId, lastRssi_.value(deviceId, 0));
    }

    QString error;
    if (!database_->beginTransaction(&error)) {
        return false;
    }
    if (!persistDevice(deviceId, lastRssi_.value(deviceId, 0))
        || !database_->insertAuditLog(
            deviceId, QStringLiteral("reset_device_secret"), QStringLiteral("updated"), &error)
        || !database_->commitTransaction(&error)) {
        database_->rollbackTransaction();
        reloadFromDatabase(deviceId);
        return false;
    }
    return true;
}

bool DeviceManager::hasDevice(const QString &deviceId) const {
    return devices_.contains(deviceId);
}

bool DeviceManager::reloadFromDatabase(const QString &deviceId) {
    if (hydrateFromDatabase(deviceId, true)) {
        return true;
    }

    if (!database_) {
        return false;
    }

    devices_.remove(deviceId);
    runtime_.remove(deviceId);
    secretHashes_.remove(deviceId);
    lastRssi_.remove(deviceId);
    return false;
}

DeviceInfo DeviceManager::deviceInfo(const QString &deviceId) const {
    return devices_.value(deviceId);
}

DeviceStatus DeviceManager::runtimeStatus(const QString &deviceId) const {
    return runtime_.value(deviceId);
}

QList<DeviceManager::DeviceRecord> DeviceManager::allDevices() const {
    QList<QString> deviceIds = devices_.keys();
    std::sort(deviceIds.begin(), deviceIds.end());

    QList<DeviceRecord> records;
    records.reserve(deviceIds.size());
    for (const QString &deviceId : deviceIds) {
        DeviceRecord record;
        record.info = devices_.value(deviceId);
        record.runtime = runtime_.value(deviceId);
        records.append(record);
    }
    return records;
}

QList<DeviceManager::PendingRecord> DeviceManager::pendingRequests() const {
    if (!database_) {
        return {};
    }

    QString error;
    const QList<Database::PendingDeviceRequest> requests = database_->listPendingRequests(&error);
    if (!error.isEmpty()) {
        qWarning() << "failed to list pending requests" << error;
        return {};
    }

    QList<PendingRecord> records;
    records.reserve(requests.size());
    for (const Database::PendingDeviceRequest &request : requests) {
        PendingRecord record;
        record.deviceId = request.deviceId;
        record.proposedName = request.proposedName;
        record.firmwareVersion = request.firmwareVersion;
        record.hardwareModel = request.hardwareModel;
        record.ip = request.ip;
        record.requestTime = request.requestTime;
        records.append(record);
    }
    return records;
}

bool DeviceManager::hydrateFromDatabase(const QString &deviceId, bool overwrite) {
    if (!database_ || deviceId.isEmpty() || (!overwrite && devices_.contains(deviceId))) {
        return false;
    }

    Database::StoredDevice stored;
    QString error;
    if (!database_->fetchDevice(deviceId, &stored, &error)) {
        return false;
    }

    devices_.insert(deviceId, stored.info);
    runtime_.insert(deviceId, stored.runtime);
    secretHashes_.insert(deviceId, stored.secretHash);
    lastRssi_.insert(deviceId, stored.lastRssi);
    return true;
}

bool DeviceManager::persistDevice(const QString &deviceId, int lastRssi) {
    if (!database_) {
        return true;
    }

    DeviceInfo info = devices_.value(deviceId);
    if (info.deviceId.isEmpty()) {
        info.deviceId = deviceId;
        info.deviceName = deviceId;
        info.status = DeviceLifecycleState::Pending;
        devices_.insert(deviceId, info);
    }

    DeviceStatus status = runtime_.value(deviceId);
    if (status.deviceId.isEmpty()) {
        status.deviceId = deviceId;
        status.status = info.status;
        status.online = false;
        status.lastSeenAt = 0;
    }

    QString error;
    if (!database_->upsertDevice(info, status, secretHashes_.value(deviceId), lastRssi, &error)) {
        qWarning() << "failed to persist device" << deviceId << error;
        reloadFromDatabase(deviceId);
        return false;
    }

    return true;
}
