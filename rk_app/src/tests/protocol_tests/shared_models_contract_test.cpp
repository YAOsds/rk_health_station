#include "models/device_models.h"
#include "models/telemetry_models.h"

#include <QtTest/QTest>

class SharedModelsContractTest : public QObject {
    Q_OBJECT

private slots:
    void telemetrySampleContract();
    void deviceIdentityContract();
};

void SharedModelsContractTest::telemetrySampleContract() {
    TelemetrySample sample;
    sample.deviceId = QStringLiteral("watch_001");
    sample.sampleTime = 1712345678;
    sample.heartRate = 72;
    sample.spo2 = 98.5;
    sample.acceleration = 0.23;
    sample.fingerDetected = true;
    sample.battery = 87;
    sample.rssi = -58;
    sample.wearState = QStringLiteral("worn");

    QCOMPARE(sample.deviceId, QStringLiteral("watch_001"));
    QCOMPARE(sample.sampleTime, qint64(1712345678));
    QCOMPARE(sample.heartRate, 72);
    QCOMPARE(sample.battery, 87);

    TelemetryBatch batch;
    batch.deviceId = sample.deviceId;
    batch.samples.append(sample);
    QCOMPARE(batch.deviceId, QStringLiteral("watch_001"));
    QCOMPARE(batch.samples.size(), 1);
    QCOMPARE(batch.samples.at(0).wearState, QStringLiteral("worn"));
}

void SharedModelsContractTest::deviceIdentityContract() {
    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_001");
    info.deviceName = QStringLiteral("Ward A Watch");
    info.deviceSecret = QStringLiteral("secret-001");
    info.bindMode = QStringLiteral("pre_shared");
    info.status = DeviceLifecycleState::Active;
    info.model = QStringLiteral("rk-watch-v1");
    info.firmwareVersion = QStringLiteral("1.0.3");

    DeviceStatus runtime;
    runtime.deviceId = info.deviceId;
    runtime.status = DeviceLifecycleState::Offline;
    runtime.online = false;
    runtime.lastSeenAt = 1712345678;
    runtime.remoteIp = QStringLiteral("192.168.10.15");

    QCOMPARE(info.deviceId, QStringLiteral("watch_001"));
    QCOMPARE(deviceLifecycleStateToString(DeviceLifecycleState::Pending), QStringLiteral("pending"));
    QCOMPARE(deviceLifecycleStateToString(DeviceLifecycleState::Revoked), QStringLiteral("revoked"));
    QCOMPARE(info.model, QStringLiteral("rk-watch-v1"));
    QCOMPARE(info.firmwareVersion, QStringLiteral("1.0.3"));
    QCOMPARE(runtime.status, DeviceLifecycleState::Offline);
    QCOMPARE(runtime.remoteIp, QStringLiteral("192.168.10.15"));
}

QTEST_MAIN(SharedModelsContractTest)

#include "shared_models_contract_test.moc"
