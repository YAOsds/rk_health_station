#include "device/device_manager.h"
#include "storage/database.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QTest>

class DeviceAdminTest : public QObject {
    Q_OBJECT

private slots:
    void approvePendingDeviceActivatesIt();
    void renameDisableAndResetSecret();
    void rejectPendingDeviceRemovesApprovalCandidate();
};

void DeviceAdminTest::approvePendingDeviceActivatesIt() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("device_admin.sqlite"));

    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    Database::PendingDeviceRequest request;
    request.deviceId = QStringLiteral("watch_002");
    request.proposedName = QStringLiteral("Pending Watch 002");
    request.firmwareVersion = QStringLiteral("1.2.3");
    request.hardwareModel = QStringLiteral("esp32-watch");
    request.ip = QStringLiteral("192.168.1.88");
    request.requestTime = 1713000000;
    QVERIFY2(database.upsertPendingRequest(request, &error), qPrintable(error));

    DeviceManager deviceManager(&database);
    QVERIFY(deviceManager.approveDevice(
        QStringLiteral("watch_002"), QStringLiteral("Watch 002"), QStringLiteral("new_secret_hash")));

    const DeviceInfo info = deviceManager.deviceInfo(QStringLiteral("watch_002"));
    QCOMPARE(info.deviceId, QStringLiteral("watch_002"));
    QCOMPARE(info.deviceName, QStringLiteral("Watch 002"));
    QCOMPARE(info.status, DeviceLifecycleState::Active);

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(QStringLiteral("watch_002"), &stored, &error), qPrintable(error));
    QCOMPARE(stored.info.status, DeviceLifecycleState::Active);
    QCOMPARE(stored.info.deviceName, QStringLiteral("Watch 002"));
    QCOMPARE(stored.secretHash, QStringLiteral("new_secret_hash"));

    Database::PendingDeviceRequest pending;
    QVERIFY(!database.fetchPendingRequest(QStringLiteral("watch_002"), &pending, &error));

    const QString inspectConnection = QStringLiteral("device_admin_audit_inspect");
    {
        QSqlDatabase inspectDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), inspectConnection);
        inspectDb.setDatabaseName(dbPath);
        QVERIFY2(inspectDb.open(), qPrintable(inspectDb.lastError().text()));

        QSqlQuery query(inspectDb);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM device_audit_log WHERE device_id = 'watch_002' AND action = 'approve_device'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        inspectDb.close();
    }
    QSqlDatabase::removeDatabase(inspectConnection);
}

void DeviceAdminTest::renameDisableAndResetSecret() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(tempDir.filePath(QStringLiteral("device_admin_update.sqlite")), &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_ops_001");
    info.deviceName = QStringLiteral("Ops Watch");
    info.status = DeviceLifecycleState::Active;

    DeviceStatus runtime;
    runtime.deviceId = info.deviceId;
    runtime.status = DeviceLifecycleState::Active;
    runtime.online = true;
    runtime.lastSeenAt = 1713000100;

    QVERIFY2(database.upsertDevice(info, runtime, QStringLiteral("secret-old"), -40, &error),
        qPrintable(error));

    DeviceManager deviceManager(&database);
    QVERIFY(deviceManager.reloadFromDatabase(info.deviceId));
    QVERIFY(deviceManager.renameDevice(info.deviceId, QStringLiteral("Renamed Watch")));
    QVERIFY(deviceManager.setDeviceEnabled(info.deviceId, false));
    QVERIFY(deviceManager.resetSecret(info.deviceId, QStringLiteral("secret-new")));

    const DeviceInfo updated = deviceManager.deviceInfo(info.deviceId);
    QCOMPARE(updated.deviceName, QStringLiteral("Renamed Watch"));
    QCOMPARE(updated.status, DeviceLifecycleState::Disabled);

    Database::StoredDevice stored;
    QVERIFY2(database.fetchDevice(info.deviceId, &stored, &error), qPrintable(error));
    QCOMPARE(stored.info.deviceName, QStringLiteral("Renamed Watch"));
    QCOMPARE(stored.info.status, DeviceLifecycleState::Disabled);
    QCOMPARE(stored.secretHash, QStringLiteral("secret-new"));
}

void DeviceAdminTest::rejectPendingDeviceRemovesApprovalCandidate() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(tempDir.filePath(QStringLiteral("device_admin_reject.sqlite")), &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    Database::PendingDeviceRequest request;
    request.deviceId = QStringLiteral("watch_reject_001");
    request.proposedName = QStringLiteral("Reject Me");
    request.requestTime = 1713000200;
    QVERIFY2(database.upsertPendingRequest(request, &error), qPrintable(error));

    DeviceManager deviceManager(&database);
    QVERIFY(deviceManager.rejectDevice(QStringLiteral("watch_reject_001")));

    Database::PendingDeviceRequest pending;
    QVERIFY(!database.fetchPendingRequest(QStringLiteral("watch_reject_001"), &pending, &error));

    const QString inspectConnection = QStringLiteral("device_reject_audit_inspect");
    {
        QSqlDatabase inspectDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), inspectConnection);
        inspectDb.setDatabaseName(tempDir.filePath(QStringLiteral("device_admin_reject.sqlite")));
        QVERIFY2(inspectDb.open(), qPrintable(inspectDb.lastError().text()));

        QSqlQuery query(inspectDb);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM device_audit_log "
            "WHERE device_id = 'watch_reject_001' AND action = 'reject_device'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        inspectDb.close();
    }
    QSqlDatabase::removeDatabase(inspectConnection);
}

QTEST_MAIN(DeviceAdminTest)

#include "device_admin_test.moc"
