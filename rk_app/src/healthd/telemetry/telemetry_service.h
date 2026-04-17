#pragma once

#include "protocol/device_frame.h"
#include "telemetry/aggregation_service.h"

#include <QString>
#include <QStringList>

class Database;
class DeviceManager;
class QJsonObject;

class TelemetryService {
public:
    TelemetryService(DeviceManager *deviceManager = nullptr, Database *database = nullptr);

    void setDeviceManager(DeviceManager *deviceManager);
    void setDatabase(Database *database);

    bool handleTelemetry(const DeviceEnvelope &envelope, const QString &remoteIp = QString());

private:
    static bool readInt(const QJsonObject &payload, const QStringList &keys, int *out, bool *found = nullptr);
    static bool readDouble(const QJsonObject &payload, const QStringList &keys, double *out);
    static bool readBool(const QJsonObject &payload, const QStringList &keys, bool *out);
    static bool readInt64(const QJsonObject &payload, const QStringList &keys, qint64 *out, bool *found = nullptr);
    static bool readString(const QJsonObject &payload, const QStringList &keys, QString *out);

    DeviceManager *deviceManager_ = nullptr;
    Database *database_ = nullptr;
    AggregationService aggregationService_;
};
