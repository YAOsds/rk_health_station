#pragma once

#include "models/device_models.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

struct LatestTelemetryFields {
    qint64 sampleTime = 0;
    std::optional<int> heartRate;
    std::optional<double> spo2;
    std::optional<int> battery;
};

struct AlertInput {
    QString deviceId;
    DeviceLifecycleState lifecycle = DeviceLifecycleState::Pending;
    bool online = false;
    qint64 lastSeenAt = 0;
    qint64 now = 0;
    LatestTelemetryFields latest;
};

struct Alert {
    QString id;
    QString severity;
    qint64 since = 0;
    QString message;
};

class AlertEngine {
public:
    QVector<Alert> evaluate(const AlertInput &input);

private:
    QString makeKey(const QString &deviceId, const QString &alertId) const;
    qint64 touchSince(const QString &deviceId, const QString &alertId, qint64 now, bool active);

    // Tracks "since" timestamps across calls so the UI can show stable durations.
    QHash<QString, qint64> sinceByKey_;
};

