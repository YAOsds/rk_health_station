#include "storage/database.h"

#include <QTemporaryDir>
#include <QtTest/QTest>

class DeviceStorageTest : public QObject {
    Q_OBJECT

private slots:
    void insertsAndFetchesDevice();
    void preservesUnseenDeviceRuntimeState();
};

void DeviceStorageTest::insertsAndFetchesDevice() {
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "temp dir should be valid");

    const QString dbPath = tempDir.filePath(QStringLiteral("healthd_task4_test.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_task4_001");
    info.deviceName = QStringLiteral("Task4 Ward Watch");
    info.status = DeviceLifecycleState::Active;
    info.bindMode = QStringLiteral("pre_shared");
    info.firmwareVersion = QStringLiteral("1.0.4");
    info.model = QStringLiteral("rk3588-watch");

    DeviceStatus runtime;
    runtime.deviceId = info.deviceId;
    runtime.status = DeviceLifecycleState::Active;
    runtime.online = true;
    runtime.lastSeenAt = 1712345678;
    runtime.remoteIp = QStringLiteral("192.168.1.42");

    QVERIFY2(database.upsertDevice(info, runtime, QStringLiteral("hash-123"), -54, &error),
        qPrintable(error));

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(info.deviceId, &stored, &error), qPrintable(error));

    QCOMPARE(stored.info.deviceId, info.deviceId);
    QCOMPARE(stored.info.deviceName, info.deviceName);
    QCOMPARE(stored.info.bindMode, info.bindMode);
    QCOMPARE(stored.info.firmwareVersion, info.firmwareVersion);
    QCOMPARE(stored.info.model, info.model);
    QCOMPARE(stored.runtime.deviceId, info.deviceId);
    QCOMPARE(stored.runtime.status, DeviceLifecycleState::Active);
    QCOMPARE(stored.runtime.online, true);
    QCOMPARE(stored.runtime.lastSeenAt, runtime.lastSeenAt);
    QCOMPARE(stored.runtime.remoteIp, runtime.remoteIp);
    QCOMPARE(stored.secretHash, QStringLiteral("hash-123"));
    QCOMPARE(stored.lastRssi, -54);
}

void DeviceStorageTest::preservesUnseenDeviceRuntimeState() {
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "temp dir should be valid");

    const QString dbPath = tempDir.filePath(QStringLiteral("healthd_task4_unseen.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_task4_002");
    info.deviceName = QStringLiteral("Metadata Only Watch");
    info.status = DeviceLifecycleState::Active;

    DeviceStatus runtime;
    runtime.deviceId = info.deviceId;
    runtime.status = DeviceLifecycleState::Active;
    runtime.online = false;
    runtime.lastSeenAt = 0;

    QVERIFY2(database.upsertDevice(info, runtime, QStringLiteral("hash-meta"), -42, &error),
        qPrintable(error));

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(info.deviceId, &stored, &error), qPrintable(error));
    QCOMPARE(stored.runtime.lastSeenAt, qint64(0));
    QCOMPARE(stored.runtime.online, false);
    QCOMPARE(stored.secretHash, QStringLiteral("hash-meta"));
    QCOMPARE(stored.lastRssi, -42);
}

QTEST_MAIN(DeviceStorageTest)

#include "device_storage_test.moc"
