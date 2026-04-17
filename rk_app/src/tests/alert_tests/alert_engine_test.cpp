#include "alerts/alert_engine.h"

#include <QtTest/QTest>

namespace {
bool hasAlert(const QVector<Alert> &alerts, const QString &alertId, Alert *out = nullptr) {
    for (const Alert &alert : alerts) {
        if (alert.id == alertId) {
            if (out) {
                *out = alert;
            }
            return true;
        }
    }
    return false;
}
} // namespace

class AlertEngineTest : public QObject {
    Q_OBJECT

private slots:
    void triggersHeartRateLowAndKeepsSinceTimestampWhileActive();
    void triggersHeartRateHigh();
    void triggersSpo2Low();
    void triggersLowBattery();
    void derivesDeviceOfflineForActiveDevices();
    void derivesNoRecentTelemetryWhenLastSeenTooOld();
};

void AlertEngineTest::triggersHeartRateLowAndKeepsSinceTimestampWhileActive() {
    AlertEngine engine;

    AlertInput input;
    input.deviceId = QStringLiteral("watch-001");
    input.lifecycle = DeviceLifecycleState::Active;
    input.online = true;
    input.lastSeenAt = 1000;
    input.now = 1000;
    input.latest.sampleTime = 1000;
    input.latest.heartRate = 49;

    QVector<Alert> first = engine.evaluate(input);
    Alert firstAlert;
    QVERIFY(hasAlert(first, QStringLiteral("heart_rate_low"), &firstAlert));
    QCOMPARE(firstAlert.severity, QStringLiteral("warning"));
    QCOMPARE(firstAlert.since, qint64(1000));

    input.now = 1005;
    input.lastSeenAt = 1005;
    input.latest.sampleTime = 1005;
    input.latest.heartRate = 48;

    QVector<Alert> second = engine.evaluate(input);
    Alert secondAlert;
    QVERIFY(hasAlert(second, QStringLiteral("heart_rate_low"), &secondAlert));
    QCOMPARE(secondAlert.since, qint64(1000));
}

void AlertEngineTest::triggersHeartRateHigh() {
    AlertEngine engine;

    AlertInput input;
    input.deviceId = QStringLiteral("watch-002");
    input.lifecycle = DeviceLifecycleState::Active;
    input.online = true;
    input.lastSeenAt = 2000;
    input.now = 2000;
    input.latest.sampleTime = 2000;
    input.latest.heartRate = 121;

    QVector<Alert> alerts = engine.evaluate(input);
    QVERIFY(hasAlert(alerts, QStringLiteral("heart_rate_high")));
}

void AlertEngineTest::triggersSpo2Low() {
    AlertEngine engine;

    AlertInput input;
    input.deviceId = QStringLiteral("watch-003");
    input.lifecycle = DeviceLifecycleState::Active;
    input.online = true;
    input.lastSeenAt = 3000;
    input.now = 3000;
    input.latest.sampleTime = 3000;
    input.latest.spo2 = 91.9;

    QVector<Alert> alerts = engine.evaluate(input);
    QVERIFY(hasAlert(alerts, QStringLiteral("spo2_low")));
}

void AlertEngineTest::triggersLowBattery() {
    AlertEngine engine;

    AlertInput input;
    input.deviceId = QStringLiteral("watch-004");
    input.lifecycle = DeviceLifecycleState::Active;
    input.online = true;
    input.lastSeenAt = 4000;
    input.now = 4000;
    input.latest.sampleTime = 4000;
    input.latest.battery = 19;

    QVector<Alert> alerts = engine.evaluate(input);
    QVERIFY(hasAlert(alerts, QStringLiteral("low_battery")));
}

void AlertEngineTest::derivesDeviceOfflineForActiveDevices() {
    AlertEngine engine;

    AlertInput input;
    input.deviceId = QStringLiteral("watch-005");
    input.lifecycle = DeviceLifecycleState::Active;
    input.online = false;
    input.lastSeenAt = 0;
    input.now = 5000;

    QVector<Alert> alerts = engine.evaluate(input);
    QVERIFY(hasAlert(alerts, QStringLiteral("device_offline")));
}

void AlertEngineTest::derivesNoRecentTelemetryWhenLastSeenTooOld() {
    AlertEngine engine;

    AlertInput input;
    input.deviceId = QStringLiteral("watch-006");
    input.lifecycle = DeviceLifecycleState::Active;
    input.online = true;
    input.lastSeenAt = 1000;
    input.now = 1121;

    QVector<Alert> alerts = engine.evaluate(input);
    QVERIFY(hasAlert(alerts, QStringLiteral("no_recent_telemetry")));
}

QTEST_MAIN(AlertEngineTest)
#include "alert_engine_test.moc"

