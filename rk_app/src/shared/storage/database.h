#pragma once

#include "models/device_models.h"
#include "models/telemetry_models.h"

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariant>

class Database {
public:
    struct PendingDeviceRequest {
        QString deviceId;
        QString proposedName;
        QString firmwareVersion;
        QString hardwareModel;
        QString mac;
        QString ip;
        qint64 requestTime = 0;
        QString status = QStringLiteral("pending");
    };

    struct StoredDevice {
        DeviceInfo info;
        DeviceStatus runtime;
        QString secretHash;
        int lastRssi = 0;
    };

    struct TelemetryRow {
        TelemetrySample sample;
        QVariant heartRate;
        QVariant spo2;
        QVariant acceleration;
        QVariant fingerDetected;
        QVariant battery;
        QVariant rssi;
    };

    struct LatestTelemetryRecord {
        TelemetrySample sample;
        QVariant heartRate;
        QVariant spo2;
        QVariant battery;
    };

    struct TelemetryMinuteAggRow {
        QString deviceId;
        qint64 minuteTs = 0;
        int samplesTotal = 0;

        int hrCount = 0;
        QVariant hrMin;
        QVariant hrMax;
        qint64 hrSum = 0;

        int spo2Count = 0;
        QVariant spo2Min;
        QVariant spo2Max;
        double spo2Sum = 0.0;

        int batteryCount = 0;
        QVariant batteryMin;
        QVariant batteryMax;
        qint64 batterySum = 0;
    };

    Database();
    ~Database();

    bool open(const QString &path, QString *error = nullptr);
    bool initializeSchema(QString *error = nullptr);
    bool beginTransaction(QString *error = nullptr);
    bool commitTransaction(QString *error = nullptr);
    void rollbackTransaction();

    bool upsertDevice(const DeviceInfo &info, const DeviceStatus &runtime,
        const QString &secretHash = QString(), int lastRssi = 0, QString *error = nullptr);
    bool fetchDevice(const QString &deviceId, StoredDevice *out, QString *error = nullptr) const;
    QStringList listDeviceIds(QString *error = nullptr) const;
    bool fetchPendingRequest(
        const QString &deviceId, PendingDeviceRequest *out, QString *error = nullptr) const;
    QList<PendingDeviceRequest> listPendingRequests(QString *error = nullptr) const;
    bool upsertPendingRequest(const PendingDeviceRequest &request, QString *error = nullptr);
    bool deletePendingRequest(const QString &deviceId, QString *error = nullptr);
    bool insertAuditLog(const QString &deviceId, const QString &action, const QString &detail,
        QString *error = nullptr);
    bool insertTelemetrySample(const TelemetryRow &row, QString *error = nullptr);
    bool fetchLatestTelemetry(
        const QString &deviceId, LatestTelemetryRecord *out, QString *error = nullptr) const;
    bool upsertTelemetryMinuteAgg(const TelemetryRow &row, QString *error = nullptr);
    QList<TelemetryMinuteAggRow> fetchTelemetryMinuteAgg(const QString &deviceId,
        qint64 fromTs, qint64 toTs, QString *error = nullptr) const;

    bool isOpen() const;

private:
    static DeviceLifecycleState parseLifecycleState(const QString &value);
    static qint64 currentEpochSeconds();
    static void setError(QString *error, const QString &message);

    QString connectionName_;
    QSqlDatabase db_;
};
