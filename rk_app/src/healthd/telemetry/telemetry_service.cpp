#include "telemetry/telemetry_service.h"

#include "device/device_manager.h"
#include "models/telemetry_models.h"
#include "storage/database.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>
#include <limits>
#include <optional>

TelemetryService::TelemetryService(DeviceManager *deviceManager, Database *database)
    : deviceManager_(deviceManager)
    , database_(database)
    , aggregationService_(database) {
}

void TelemetryService::setDeviceManager(DeviceManager *deviceManager) {
    deviceManager_ = deviceManager;
}

void TelemetryService::setDatabase(Database *database) {
    database_ = database;
    aggregationService_.setDatabase(database);
}

bool TelemetryService::handleTelemetry(const DeviceEnvelope &envelope, const QString &remoteIp) {
    if (envelope.type != QStringLiteral("telemetry_batch") || envelope.deviceId.isEmpty()) {
        return false;
    }

    TelemetrySample sample;
    sample.deviceId = envelope.deviceId;
    sample.sampleTime = envelope.ts > 0 ? envelope.ts : QDateTime::currentSecsSinceEpoch();

    int intValue = 0;
    double doubleValue = 0.0;
    bool boolValue = false;
    qint64 int64Value = 0;
    QString stringValue;
    Database::TelemetryRow row;
    row.sample = sample;
    std::optional<int> lastRssi;

    const QJsonObject &payload = envelope.payload;
    bool foundInvalid = false;
    if (readInt64(payload, {QStringLiteral("timestamp"), QStringLiteral("t")}, &int64Value, &foundInvalid)) {
        sample.sampleTime = int64Value;
    } else if (foundInvalid) {
        return false;
    }
    foundInvalid = false;
    if (readInt(payload, {QStringLiteral("heart_rate"), QStringLiteral("hr"), QStringLiteral("h")},
            &intValue, &foundInvalid)) {
        sample.heartRate = intValue;
        row.heartRate = intValue;
    } else if (foundInvalid) {
        return false;
    }
    if (readDouble(payload, {QStringLiteral("spo2"), QStringLiteral("o")}, &doubleValue)) {
        sample.spo2 = doubleValue;
        row.spo2 = doubleValue;
    }
    if (readDouble(payload, {QStringLiteral("acceleration"), QStringLiteral("acc"), QStringLiteral("a")},
            &doubleValue)) {
        sample.acceleration = doubleValue;
        row.acceleration = doubleValue;
    }
    if (readBool(payload, {QStringLiteral("finger_detected"), QStringLiteral("finger"), QStringLiteral("f")},
            &boolValue)) {
        sample.fingerDetected = boolValue;
        row.fingerDetected = boolValue ? 1 : 0;
    }
    foundInvalid = false;
    if (readInt(payload, {QStringLiteral("battery")}, &intValue, &foundInvalid)) {
        sample.battery = intValue;
        row.battery = intValue;
    } else if (foundInvalid) {
        return false;
    }
    foundInvalid = false;
    if (readInt(payload, {QStringLiteral("rssi")}, &intValue, &foundInvalid)) {
        sample.rssi = intValue;
        row.rssi = intValue;
        lastRssi = intValue;
    } else if (foundInvalid) {
        return false;
    }
    if (readString(payload, {QStringLiteral("wear_state"), QStringLiteral("wear")}, &stringValue)) {
        sample.wearState = stringValue;
    }
    if (readBool(payload, {QStringLiteral("imu_fall_valid")}, &boolValue)) {
        sample.imuFallValid = boolValue;
    }
    foundInvalid = false;
    if (readInt(payload, {QStringLiteral("imu_fall_class")}, &intValue, &foundInvalid)) {
        sample.imuFallClass = intValue;
    } else if (foundInvalid) {
        return false;
    }
    if (readDouble(payload, {QStringLiteral("imu_nonfall_prob")}, &doubleValue)) {
        sample.imuNonFallProb = doubleValue;
    }
    if (readDouble(payload, {QStringLiteral("imu_preimpact_prob")}, &doubleValue)) {
        sample.imuPreImpactProb = doubleValue;
    }
    if (readDouble(payload, {QStringLiteral("imu_fall_prob")}, &doubleValue)) {
        sample.imuFallProb = doubleValue;
    }
    row.sample = sample;

    bool ok = true;
    const bool useTransaction = database_ != nullptr;
    QString error;
    if (useTransaction && !database_->beginTransaction(&error)) {
        qWarning() << "failed to begin telemetry transaction" << error;
        return false;
    }

    if (deviceManager_) {
        ok = deviceManager_->applyTelemetry(sample, remoteIp, lastRssi);
    }

    if (database_) {
        if (!database_->insertTelemetrySample(row, &error)) {
            qWarning() << "failed to insert telemetry sample for" << sample.deviceId << error;
            ok = false;
        } else if (!aggregationService_.recordTelemetry(row, &error)) {
            qWarning() << "failed to update telemetry minute aggregation for" << sample.deviceId << error;
            ok = false;
        }
    }

    if (useTransaction) {
        if (!ok) {
            database_->rollbackTransaction();
            if (deviceManager_) {
                deviceManager_->reloadFromDatabase(sample.deviceId);
            }
            return false;
        }

        if (!database_->commitTransaction(&error)) {
            qWarning() << "failed to commit telemetry transaction for" << sample.deviceId << error;
            database_->rollbackTransaction();
            if (deviceManager_) {
                deviceManager_->reloadFromDatabase(sample.deviceId);
            }
            return false;
        }
    }

    return ok;
}

bool TelemetryService::readInt(
    const QJsonObject &payload, const QStringList &keys, int *out, bool *found) {
    if (!out) {
        return false;
    }
    if (found) {
        *found = false;
    }

    for (const QString &key : keys) {
        const QJsonValue value = payload.value(key);
        if (value.isUndefined()) {
            continue;
        }
        if (found) {
            *found = true;
        }
        if (!value.isDouble()) {
            return false;
        }
        const double raw = value.toDouble();
        double integral = 0.0;
        if (!qIsFinite(raw)
            || std::modf(raw, &integral) != 0.0
            || raw < static_cast<double>(std::numeric_limits<int>::min())
            || raw > static_cast<double>(std::numeric_limits<int>::max())) {
            return false;
        }
        *out = static_cast<int>(integral);
        return true;
    }
    return false;
}

bool TelemetryService::readDouble(const QJsonObject &payload, const QStringList &keys, double *out) {
    if (!out) {
        return false;
    }

    for (const QString &key : keys) {
        const QJsonValue value = payload.value(key);
        if (!value.isDouble()) {
            continue;
        }

        *out = value.toDouble();
        return true;
    }
    return false;
}

bool TelemetryService::readBool(const QJsonObject &payload, const QStringList &keys, bool *out) {
    if (!out) {
        return false;
    }

    for (const QString &key : keys) {
        const QJsonValue value = payload.value(key);
        if (value.isBool()) {
            *out = value.toBool();
            return true;
        }
        if (value.isDouble()) {
            *out = value.toDouble() != 0.0;
            return true;
        }
    }

    return false;
}

bool TelemetryService::readInt64(
    const QJsonObject &payload, const QStringList &keys, qint64 *out, bool *found) {
    if (!out) {
        return false;
    }
    if (found) {
        *found = false;
    }

    for (const QString &key : keys) {
        const QJsonValue value = payload.value(key);
        if (value.isUndefined()) {
            continue;
        }
        if (found) {
            *found = true;
        }
        if (!value.isDouble()) {
            return false;
        }
        const double raw = value.toDouble();
        double integral = 0.0;
        constexpr double kMaxSafeJsonInteger = 9007199254740991.0;
        if (!qIsFinite(raw)
            || std::modf(raw, &integral) != 0.0
            || raw < -kMaxSafeJsonInteger
            || raw > kMaxSafeJsonInteger) {
            return false;
        }
        *out = static_cast<qint64>(integral);
        return true;
    }

    return false;
}

bool TelemetryService::readString(const QJsonObject &payload, const QStringList &keys, QString *out) {
    if (!out) {
        return false;
    }

    for (const QString &key : keys) {
        const QJsonValue value = payload.value(key);
        if (!value.isString()) {
            continue;
        }

        *out = value.toString();
        return true;
    }

    return false;
}
