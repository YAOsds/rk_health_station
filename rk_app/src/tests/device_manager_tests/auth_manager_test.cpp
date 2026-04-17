#include "device/auth_manager.h"
#include "protocol/device_frame.h"
#include "security/hmac_helper.h"
#include "storage/database.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QTest>

class AuthManagerTest : public QObject {
    Q_OBJECT

private slots:
    void createsPendingRequestForUnknownDevice();
    void issuesChallengeForActiveDevice();
    void rejectsDisabledDevice();
    void verifyChallengeResponse();
};

void AuthManagerTest::createsPendingRequestForUnknownDevice() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("auth_unknown.sqlite"));
    Database database;
    QString error;
    QVERIFY2(database.open(dbPath, &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("auth_hello");
    envelope.seq = 1;
    envelope.ts = 1712345678;
    envelope.deviceId = QStringLiteral("watch_pending_001");
    envelope.payload.insert(QStringLiteral("device_name"), QStringLiteral("Pending Watch"));
    envelope.payload.insert(QStringLiteral("firmware_version"), QStringLiteral("1.0.0"));
    envelope.payload.insert(QStringLiteral("hardware_model"), QStringLiteral("esp32-watch"));
    envelope.payload.insert(QStringLiteral("mac"), QStringLiteral("AA:BB:CC:DD:EE:FF"));
    envelope.payload.insert(QStringLiteral("client_nonce"), QStringLiteral("client-001"));

    AuthManager auth;
    const AuthManager::HelloResult result
        = auth.handleAuthHello(envelope, QStringLiteral("192.168.10.20"), &database, &error);
    QCOMPARE(result.decision, AuthManager::HelloDecision::RegistrationRequired);
    QCOMPARE(result.reason, QStringLiteral("registration_required"));

    const QString inspectConnection = QStringLiteral("auth_pending_inspect");
    {
        QSqlDatabase inspectDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), inspectConnection);
        inspectDb.setDatabaseName(dbPath);
        QVERIFY2(inspectDb.open(), qPrintable(inspectDb.lastError().text()));

        QSqlQuery query(inspectDb);
        query.prepare(QStringLiteral(
            "SELECT proposed_name, firmware_version, hardware_model, mac, ip, status "
            "FROM device_pending_requests WHERE device_id = ?"));
        query.addBindValue(envelope.deviceId);
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("Pending Watch"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("1.0.0"));
        QCOMPARE(query.value(2).toString(), QStringLiteral("esp32-watch"));
        QCOMPARE(query.value(3).toString(), QStringLiteral("AA:BB:CC:DD:EE:FF"));
        QCOMPARE(query.value(4).toString(), QStringLiteral("192.168.10.20"));
        QCOMPARE(query.value(5).toString(), QStringLiteral("pending"));

        inspectDb.close();
    }
    QSqlDatabase::removeDatabase(inspectConnection);
}

void AuthManagerTest::issuesChallengeForActiveDevice() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(tempDir.filePath(QStringLiteral("auth_active.sqlite")), &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_active_001");
    info.deviceName = QStringLiteral("Approved Watch");
    info.status = DeviceLifecycleState::Active;
    info.bindMode = QStringLiteral("pre_shared");

    DeviceStatus runtime;
    runtime.deviceId = info.deviceId;
    runtime.status = DeviceLifecycleState::Active;
    runtime.online = false;
    runtime.lastSeenAt = 0;

    QVERIFY2(database.upsertDevice(info, runtime, QStringLiteral("device_secret"), -48, &error),
        qPrintable(error));

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("auth_hello");
    envelope.seq = 2;
    envelope.ts = 1712345680;
    envelope.deviceId = info.deviceId;
    envelope.payload.insert(QStringLiteral("client_nonce"), QStringLiteral("client-002"));

    AuthManager auth;
    const AuthManager::HelloResult result
        = auth.handleAuthHello(envelope, QStringLiteral("192.168.10.30"), &database, &error);
    QCOMPARE(result.decision, AuthManager::HelloDecision::SendChallenge);
    QCOMPARE(result.reason, QStringLiteral("ok"));
    QVERIFY(!result.serverNonce.isEmpty());
}

void AuthManagerTest::rejectsDisabledDevice() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(tempDir.filePath(QStringLiteral("auth_disabled.sqlite")), &error), qPrintable(error));
    QVERIFY2(database.initializeSchema(&error), qPrintable(error));

    DeviceInfo info;
    info.deviceId = QStringLiteral("watch_disabled_auth_001");
    info.deviceName = QStringLiteral("Disabled Auth Watch");
    info.status = DeviceLifecycleState::Disabled;

    DeviceStatus runtime;
    runtime.deviceId = info.deviceId;
    runtime.status = DeviceLifecycleState::Disabled;
    runtime.online = false;
    runtime.lastSeenAt = 0;

    QVERIFY2(database.upsertDevice(info, runtime, QStringLiteral("device_secret"), -35, &error),
        qPrintable(error));

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("auth_hello");
    envelope.seq = 3;
    envelope.ts = 1712345681;
    envelope.deviceId = info.deviceId;
    envelope.payload.insert(QStringLiteral("client_nonce"), QStringLiteral("client-003"));

    AuthManager auth;
    const AuthManager::HelloResult result
        = auth.handleAuthHello(envelope, QStringLiteral("192.168.10.40"), &database, &error);
    QCOMPARE(result.decision, AuthManager::HelloDecision::Rejected);
    QCOMPARE(result.reason, QStringLiteral("rejected"));
    QVERIFY(result.serverNonce.isEmpty());
}

void AuthManagerTest::verifyChallengeResponse() {
    AuthManager auth;
    const QString nonce = QStringLiteral("server_nonce");
    const QString clientNonce = QStringLiteral("client_nonce");
    const QString secret = QStringLiteral("device_secret");
    const QByteArray proof = HmacHelper::sign(
        QStringLiteral("watch_001"), nonce, clientNonce, 1712345678, secret);
    QVERIFY(auth.verify(QStringLiteral("watch_001"), nonce, clientNonce, 1712345678, secret, proof));
    QVERIFY(!auth.verify(
        QStringLiteral("watch_001"), nonce, clientNonce, 1712345678, QStringLiteral("wrong_secret"), proof));
}

QTEST_MAIN(AuthManagerTest)

#include "auth_manager_test.moc"
