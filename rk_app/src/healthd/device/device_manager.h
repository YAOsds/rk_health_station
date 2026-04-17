#pragma once

#include "models/device_models.h"
#include "models/telemetry_models.h"

#include <QHash>
#include <QList>
#include <QString>
#include <optional>

class Database;

class DeviceManager {
public:
    struct PendingRecord {
        QString deviceId;
        QString proposedName;
        QString firmwareVersion;
        QString hardwareModel;
        QString ip;
        qint64 requestTime = 0;
    };

    struct DeviceRecord {
        DeviceInfo info;
        DeviceStatus runtime;
    };

    explicit DeviceManager(Database *database = nullptr);

    void setDatabase(Database *database);

    bool ensureRegistered(const QString &deviceId, qint64 lastSeenAt,
        const QString &remoteIp = QString());
    bool updateMetadata(const DeviceInfo &info,
        std::optional<QString> secretHash = std::nullopt,
        std::optional<int> lastRssi = std::nullopt);
    bool applyTelemetry(const TelemetrySample &sample,
        const QString &remoteIp = QString(),
        std::optional<int> lastRssi = std::nullopt);
    bool reloadFromDatabase(const QString &deviceId);
    bool approveDevice(
        const QString &deviceId, const QString &deviceName, const QString &secretHash);
    bool rejectDevice(const QString &deviceId);
    bool renameDevice(const QString &deviceId, const QString &deviceName);
    bool setDeviceEnabled(const QString &deviceId, bool enabled);
    bool resetSecret(const QString &deviceId, const QString &secretHash);

    bool hasDevice(const QString &deviceId) const;
    DeviceInfo deviceInfo(const QString &deviceId) const;
    DeviceStatus runtimeStatus(const QString &deviceId) const;
    QList<DeviceRecord> allDevices() const;
    QList<PendingRecord> pendingRequests() const;

private:
    bool hydrateFromDatabase(const QString &deviceId, bool overwrite = false);
    bool persistDevice(const QString &deviceId, int lastRssi);

    Database *database_ = nullptr;
    QHash<QString, DeviceInfo> devices_;
    QHash<QString, DeviceStatus> runtime_;
    QHash<QString, QString> secretHashes_;
    QHash<QString, int> lastRssi_;
};
