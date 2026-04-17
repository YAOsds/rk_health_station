#include "alerts/alert_engine.h"

namespace {
constexpr int kHeartRateLowThreshold = 50;
constexpr int kHeartRateHighThreshold = 120;
constexpr double kSpo2LowThreshold = 92.0;
constexpr int kLowBatteryThreshold = 20;
constexpr qint64 kNoRecentTelemetrySeconds = 120;

Alert makeAlert(const QString &id, const QString &severity, qint64 since, const QString &message) {
    Alert alert;
    alert.id = id;
    alert.severity = severity;
    alert.since = since;
    alert.message = message;
    return alert;
}
} // namespace

QVector<Alert> AlertEngine::evaluate(const AlertInput &input) {
    QVector<Alert> alerts;
    if (input.deviceId.isEmpty()) {
        return alerts;
    }

    const bool eligible = input.lifecycle == DeviceLifecycleState::Active;

    const auto addRule = [&](const QString &id, bool active, const QString &severity, const QString &message) {
        const qint64 since = touchSince(input.deviceId, id, input.now, active);
        if (active) {
            alerts.push_back(makeAlert(id, severity, since, message));
        }
    };

    if (eligible) {
        if (input.latest.heartRate.has_value()) {
            addRule(QStringLiteral("heart_rate_low"),
                input.latest.heartRate.value() < kHeartRateLowThreshold,
                QStringLiteral("warning"),
                QStringLiteral("Heart rate below %1 bpm").arg(kHeartRateLowThreshold));

            addRule(QStringLiteral("heart_rate_high"),
                input.latest.heartRate.value() > kHeartRateHighThreshold,
                QStringLiteral("warning"),
                QStringLiteral("Heart rate above %1 bpm").arg(kHeartRateHighThreshold));
        } else {
            touchSince(input.deviceId, QStringLiteral("heart_rate_low"), input.now, false);
            touchSince(input.deviceId, QStringLiteral("heart_rate_high"), input.now, false);
        }

        if (input.latest.spo2.has_value()) {
            addRule(QStringLiteral("spo2_low"),
                input.latest.spo2.value() < kSpo2LowThreshold,
                QStringLiteral("critical"),
                QStringLiteral("SpO2 below %1").arg(kSpo2LowThreshold));
        } else {
            touchSince(input.deviceId, QStringLiteral("spo2_low"), input.now, false);
        }

        if (input.latest.battery.has_value()) {
            addRule(QStringLiteral("low_battery"),
                input.latest.battery.value() < kLowBatteryThreshold,
                QStringLiteral("warning"),
                QStringLiteral("Battery below %1%").arg(kLowBatteryThreshold));
        } else {
            touchSince(input.deviceId, QStringLiteral("low_battery"), input.now, false);
        }

        addRule(QStringLiteral("device_offline"),
            !input.online,
            QStringLiteral("warning"),
            QStringLiteral("Device offline"));

        const bool tooOld = input.lastSeenAt <= 0
            || (input.now > input.lastSeenAt
                && (input.now - input.lastSeenAt) > kNoRecentTelemetrySeconds);
        addRule(QStringLiteral("no_recent_telemetry"),
            tooOld,
            QStringLiteral("warning"),
            QStringLiteral("No telemetry in the last %1 seconds").arg(kNoRecentTelemetrySeconds));
    }

    if (!eligible) {
        // Device isn't in a state where alerts matter; clear any tracked "since" values.
        const QString prefix = input.deviceId + QStringLiteral("|");
        for (auto it = sinceByKey_.begin(); it != sinceByKey_.end();) {
            if (it.key().startsWith(prefix)) {
                it = sinceByKey_.erase(it);
            } else {
                ++it;
            }
        }
    }

    return alerts;
}

QString AlertEngine::makeKey(const QString &deviceId, const QString &alertId) const {
    return deviceId + QStringLiteral("|") + alertId;
}

qint64 AlertEngine::touchSince(
    const QString &deviceId, const QString &alertId, qint64 now, bool active) {
    const QString key = makeKey(deviceId, alertId);
    if (!active) {
        sinceByKey_.remove(key);
        return 0;
    }

    const auto it = sinceByKey_.constFind(key);
    if (it != sinceByKey_.constEnd()) {
        return it.value();
    }

    const qint64 since = now > 0 ? now : 0;
    sinceByKey_.insert(key, since);
    return since;
}

