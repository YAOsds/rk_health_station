#include "device/device_manager.h"
#include "storage/database.h"
#include "telemetry/telemetry_service.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QTest>

class TelemetryServiceTest : public QObject {
    Q_OBJECT

private slots:
    void handlesLongFormTelemetryAndPersistsRemoteIp();
    void acceptsShortFormCompatibilityAliases();
    void metadataUpdatesPreserveSecretAndRssi();
    void missingRssiDoesNotClearPreviousDeviceState();
    void telemetryDoesNotReactivateDisabledDevice();
    void metadataDisableUpdatesRuntimeStateImmediately();
    void restartKeepsDisabledStateOnNextTelemetry();
    void rejectsMalformedIntegerFields();
    void failedTelemetryInsertRollsBackDeviceState();
    void persistsMinuteAggregationForTelemetry();
};

void TelemetryServiceTest::handlesLongFormTelemetryAndPersistsRemoteIp() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("telemetry_service.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);
    TelemetryService telemetryService(&deviceManager, &database);

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("telemetry_batch");
    envelope.seq = 1;
    envelope.ts = 0;
    envelope.deviceId = QStringLiteral("watch_long_001");
    envelope.payload.insert(QStringLiteral("heart_rate"), 71);
    envelope.payload.insert(QStringLiteral("spo2"), 98.6);
    envelope.payload.insert(QStringLiteral("acceleration"), 0.42);
    envelope.payload.insert(QStringLiteral("finger_detected"), true);
    envelope.payload.insert(QStringLiteral("wear_state"), QStringLiteral("worn"));
    envelope.payload.insert(QStringLiteral("timestamp"), qint64(1713000000));

    QVERIFY(telemetryService.handleTelemetry(envelope, QStringLiteral("192.168.1.55")));

    const DeviceStatus runtime = deviceManager.runtimeStatus(envelope.deviceId);
    QCOMPARE(runtime.deviceId, envelope.deviceId);
    QCOMPARE(runtime.remoteIp, QStringLiteral("192.168.1.55"));
    QCOMPARE(runtime.lastSeenAt, qint64(1713000000));
    QCOMPARE(runtime.status, DeviceLifecycleState::Active);
    QVERIFY(runtime.online);

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(envelope.deviceId, &stored, &error), qPrintable(error));
    QCOMPARE(stored.runtime.remoteIp, QStringLiteral("192.168.1.55"));
    QCOMPARE(stored.runtime.lastSeenAt, qint64(1713000000));

    const QString connectionName = QStringLiteral("telemetry_service_inspect_long");
    {
        QSqlDatabase inspectDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        inspectDb.setDatabaseName(dbPath);
        QVERIFY2(inspectDb.open(), qPrintable(inspectDb.lastError().text()));

        QSqlQuery query(inspectDb);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT sample_time, heart_rate, spo2, acceleration, finger_detected, battery, rssi "
            "FROM telemetry_samples WHERE device_id = 'watch_long_001' ORDER BY id DESC LIMIT 1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toLongLong(), qint64(1713000000));
        QCOMPARE(query.value(1).toInt(), 71);
        QCOMPARE(query.value(2).toDouble(), 98.6);
        QCOMPARE(query.value(3).toDouble(), 0.42);
        QCOMPARE(query.value(4).toInt(), 1);
        QVERIFY(query.value(5).isNull());
        QVERIFY(query.value(6).isNull());

        inspectDb.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

void TelemetryServiceTest::acceptsShortFormCompatibilityAliases() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("telemetry_short.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);
    TelemetryService telemetryService(&deviceManager, &database);

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("telemetry_batch");
    envelope.seq = 2;
    envelope.ts = 1713001234;
    envelope.deviceId = QStringLiteral("watch_short_001");
    envelope.payload.insert(QStringLiteral("hr"), 77);
    envelope.payload.insert(QStringLiteral("acc"), 0.12);
    envelope.payload.insert(QStringLiteral("finger"), 1);
    envelope.payload.insert(QStringLiteral("battery"), 88);
    envelope.payload.insert(QStringLiteral("rssi"), -51);

    QVERIFY(telemetryService.handleTelemetry(envelope, QStringLiteral("10.0.0.8")));

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(envelope.deviceId, &stored, &error), qPrintable(error));
    QCOMPARE(stored.runtime.remoteIp, QStringLiteral("10.0.0.8"));
    QCOMPARE(stored.runtime.lastSeenAt, envelope.ts);
    QCOMPARE(stored.lastRssi, -51);
}

void TelemetryServiceTest::metadataUpdatesPreserveSecretAndRssi() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("device_manager_metadata.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);

    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_meta_001");
    info.deviceName = QStringLiteral("Initial Name");
    info.status = DeviceLifecycleState::Active;

    QVERIFY(deviceManager.updateMetadata(info, QStringLiteral("secret-xyz"), -60));

    DeviceInfo refreshed = info;
    refreshed.deviceName = QStringLiteral("Renamed Watch");
    QVERIFY(deviceManager.updateMetadata(refreshed));

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(info.deviceId, &stored, &error), qPrintable(error));
    QCOMPARE(stored.info.deviceName, QStringLiteral("Renamed Watch"));
    QCOMPARE(stored.secretHash, QStringLiteral("secret-xyz"));
    QCOMPARE(stored.lastRssi, -60);
    QCOMPARE(stored.runtime.lastSeenAt, qint64(0));
    QCOMPARE(stored.runtime.online, false);
}

void TelemetryServiceTest::missingRssiDoesNotClearPreviousDeviceState() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("telemetry_rssi.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);
    TelemetryService telemetryService(&deviceManager, &database);

    DeviceEnvelope first;
    first.ver = 1;
    first.type = QStringLiteral("telemetry_batch");
    first.seq = 1;
    first.ts = 1713002000;
    first.deviceId = QStringLiteral("watch_rssi_001");
    first.payload.insert(QStringLiteral("hr"), 70);
    first.payload.insert(QStringLiteral("rssi"), -48);
    QVERIFY(telemetryService.handleTelemetry(first, QStringLiteral("10.0.0.10")));

    DeviceEnvelope second = first;
    second.seq = 2;
    second.ts = 1713002001;
    second.payload = QJsonObject{{QStringLiteral("hr"), 72}};
    QVERIFY(telemetryService.handleTelemetry(second, QStringLiteral("10.0.0.10")));

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(first.deviceId, &stored, &error), qPrintable(error));
    QCOMPARE(stored.lastRssi, -48);
}

void TelemetryServiceTest::persistsMinuteAggregationForTelemetry() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("telemetry_agg.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);
    TelemetryService telemetryService(&deviceManager, &database);

    DeviceEnvelope first;
    first.ver = 1;
    first.type = QStringLiteral("telemetry_batch");
    first.seq = 1;
    first.ts = 1713000001;
    first.deviceId = QStringLiteral("watch_agg_001");
    first.payload.insert(QStringLiteral("hr"), 70);
    first.payload.insert(QStringLiteral("spo2"), 98.0);
    first.payload.insert(QStringLiteral("battery"), 80);

    DeviceEnvelope second = first;
    second.seq = 2;
    second.ts = 1713000020;
    second.payload.insert(QStringLiteral("hr"), 72);
    second.payload.insert(QStringLiteral("spo2"), 97.0);
    second.payload.insert(QStringLiteral("battery"), 79);

    QVERIFY(telemetryService.handleTelemetry(first, QStringLiteral("10.0.0.1")));
    QVERIFY(telemetryService.handleTelemetry(second, QStringLiteral("10.0.0.1")));

    const QString connectionName = QStringLiteral("telemetry_agg_inspect");
    {
        QSqlDatabase inspectDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        inspectDb.setDatabaseName(dbPath);
        QVERIFY2(inspectDb.open(), qPrintable(inspectDb.lastError().text()));

        const qint64 minuteTs = first.ts - (first.ts % 60);

        QSqlQuery query(inspectDb);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT samples_total, hr_count, hr_min, hr_max, hr_sum, "
            "spo2_count, spo2_min, spo2_max, spo2_sum, "
            "battery_count, battery_min, battery_max, battery_sum "
            "FROM telemetry_minute_agg WHERE device_id = 'watch_agg_001' AND minute_ts = %1")
            .arg(minuteTs)));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 2);
        QCOMPARE(query.value(1).toInt(), 2);
        QCOMPARE(query.value(2).toInt(), 70);
        QCOMPARE(query.value(3).toInt(), 72);
        QCOMPARE(query.value(4).toInt(), 142);
        QCOMPARE(query.value(5).toInt(), 2);
        QCOMPARE(query.value(6).toDouble(), 97.0);
        QCOMPARE(query.value(7).toDouble(), 98.0);
        QCOMPARE(query.value(8).toDouble(), 195.0);
        QCOMPARE(query.value(9).toInt(), 2);
        QCOMPARE(query.value(10).toInt(), 79);
        QCOMPARE(query.value(11).toInt(), 80);
        QCOMPARE(query.value(12).toInt(), 159);

        inspectDb.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

void TelemetryServiceTest::telemetryDoesNotReactivateDisabledDevice() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("telemetry_disabled.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);
    TelemetryService telemetryService(&deviceManager, &database);

    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_disabled_001");
    info.deviceName = QStringLiteral("Disabled Watch");
    info.status = DeviceLifecycleState::Disabled;
    QVERIFY(deviceManager.updateMetadata(info, QStringLiteral("secret-disabled"), -40));

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("telemetry_batch");
    envelope.seq = 1;
    envelope.ts = 1713003000;
    envelope.deviceId = info.deviceId;
    envelope.payload.insert(QStringLiteral("hr"), 66);

    QVERIFY(telemetryService.handleTelemetry(envelope, QStringLiteral("10.0.0.20")));

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(info.deviceId, &stored, &error), qPrintable(error));
    QCOMPARE(stored.info.status, DeviceLifecycleState::Disabled);
    QCOMPARE(stored.runtime.status, DeviceLifecycleState::Disabled);
    QCOMPARE(stored.runtime.online, false);
}

void TelemetryServiceTest::metadataDisableUpdatesRuntimeStateImmediately() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("telemetry_disable_runtime.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);
    TelemetryService telemetryService(&deviceManager, &database);

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("telemetry_batch");
    envelope.seq = 1;
    envelope.ts = 1713004000;
    envelope.deviceId = QStringLiteral("watch_runtime_disable_001");
    envelope.payload.insert(QStringLiteral("hr"), 68);
    QVERIFY(telemetryService.handleTelemetry(envelope, QStringLiteral("10.0.0.21")));

    DeviceStatus runtime = deviceManager.runtimeStatus(envelope.deviceId);
    QCOMPARE(runtime.status, DeviceLifecycleState::Active);
    QVERIFY(runtime.online);

    DeviceInfo info = deviceManager.deviceInfo(envelope.deviceId);
    info.status = DeviceLifecycleState::Disabled;
    QVERIFY(deviceManager.updateMetadata(info));

    runtime = deviceManager.runtimeStatus(envelope.deviceId);
    QCOMPARE(runtime.status, DeviceLifecycleState::Disabled);
    QCOMPARE(runtime.online, false);
}

void TelemetryServiceTest::restartKeepsDisabledStateOnNextTelemetry() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("telemetry_restart.sqlite"));

    {
        Database database;
        QString error;
        QVERIFY2(database.open(dbPath, &error), qPrintable(error));
        QVERIFY2(database.initializeSchema(&error), qPrintable(error));

        DeviceManager deviceManager(&database);
        DeviceInfo info;
        info.deviceId = QStringLiteral("watch_restart_001");
        info.deviceName = QStringLiteral("Restart Disabled Watch");
        info.status = DeviceLifecycleState::Disabled;
        QVERIFY(deviceManager.updateMetadata(info, QStringLiteral("secret-r"), -33));
    }

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);
    TelemetryService telemetryService(&deviceManager, &database);

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("telemetry_batch");
    envelope.seq = 1;
    envelope.ts = 1713005000;
    envelope.deviceId = QStringLiteral("watch_restart_001");
    envelope.payload.insert(QStringLiteral("hr"), 69);

    QVERIFY(telemetryService.handleTelemetry(envelope, QStringLiteral("10.0.0.30")));

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(envelope.deviceId, &stored, &error), qPrintable(error));
    QCOMPARE(stored.info.status, DeviceLifecycleState::Disabled);
    QCOMPARE(stored.runtime.status, DeviceLifecycleState::Disabled);
    QCOMPARE(stored.runtime.online, false);
    QCOMPARE(stored.secretHash, QStringLiteral("secret-r"));
    QCOMPARE(stored.lastRssi, -33);
}

void TelemetryServiceTest::rejectsMalformedIntegerFields() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("telemetry_invalid.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);
    TelemetryService telemetryService(&deviceManager, &database);

    DeviceEnvelope fractionalHeartRate;
    fractionalHeartRate.ver = 1;
    fractionalHeartRate.type = QStringLiteral("telemetry_batch");
    fractionalHeartRate.seq = 1;
    fractionalHeartRate.ts = 1713006000;
    fractionalHeartRate.deviceId = QStringLiteral("watch_invalid_001");
    fractionalHeartRate.payload.insert(QStringLiteral("heart_rate"), 71.5);
    QVERIFY(!telemetryService.handleTelemetry(fractionalHeartRate, QStringLiteral("10.0.0.31")));

    DeviceEnvelope unsafeTimestamp = fractionalHeartRate;
    unsafeTimestamp.payload = QJsonObject{{QStringLiteral("timestamp"), 9.007199254740993e15}};
    QVERIFY(!telemetryService.handleTelemetry(unsafeTimestamp, QStringLiteral("10.0.0.31")));
}

void TelemetryServiceTest::failedTelemetryInsertRollsBackDeviceState() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("telemetry_rollback.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceManager deviceManager(&database);
    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_rollback_001");
    info.deviceName = QStringLiteral("Rollback Watch");
    info.status = DeviceLifecycleState::Disabled;
    QVERIFY(deviceManager.updateMetadata(info, QStringLiteral("secret-rb"), -44));

    {
        const QString connectionName = QStringLiteral("telemetry_service_break_samples");
        QSqlDatabase inspectDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        inspectDb.setDatabaseName(dbPath);
        QVERIFY2(inspectDb.open(), qPrintable(inspectDb.lastError().text()));
        QSqlQuery drop(inspectDb);
        QVERIFY(drop.exec(QStringLiteral("DROP TABLE telemetry_samples")));
        inspectDb.close();
        QSqlDatabase::removeDatabase(connectionName);
    }

    TelemetryService telemetryService(&deviceManager, &database);
    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("telemetry_batch");
    envelope.seq = 1;
    envelope.ts = 1713007000;
    envelope.deviceId = info.deviceId;
    envelope.payload.insert(QStringLiteral("hr"), 70);

    QVERIFY(!telemetryService.handleTelemetry(envelope, QStringLiteral("10.0.0.32")));

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(info.deviceId, &stored, &error), qPrintable(error));
    QCOMPARE(stored.info.status, DeviceLifecycleState::Disabled);
    QCOMPARE(stored.runtime.status, DeviceLifecycleState::Disabled);
    QCOMPARE(stored.runtime.online, false);
    QCOMPARE(stored.runtime.lastSeenAt, qint64(0));
    QCOMPARE(stored.secretHash, QStringLiteral("secret-rb"));
    QCOMPARE(stored.lastRssi, -44);

    const DeviceStatus runtime = deviceManager.runtimeStatus(info.deviceId);
    QCOMPARE(runtime.status, DeviceLifecycleState::Disabled);
    QCOMPARE(runtime.online, false);
}

QTEST_MAIN(TelemetryServiceTest)

#include "telemetry_service_test.moc"
