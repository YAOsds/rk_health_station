#include "protocol/device_frame.h"
#include "security/hmac_helper.h"

#include <QFile>
#include <QHostAddress>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QDateTime>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <memory>

#ifndef HEALTHD_TEST_BINARY_PATH
#define HEALTHD_TEST_BINARY_PATH ""
#endif

namespace {
constexpr quint16 kHealthdPort = 19001;
constexpr int kMarkerWaitMs = 2000;
constexpr int kMarkerPollMs = 50;
constexpr int kNoMarkerWaitMs = 200;
const char kMarkerEnvVar[] = "HEALTHD_EVENT_MARKER_PATH";
const char kDefaultMarkerPath[] = "/tmp/healthd-task3-marker.jsonl";
const char kMarkerEnablePath[] = "/tmp/healthd-task3-marker.enable";
const char kDatabaseEnvVar[] = "HEALTHD_DB_PATH";
const char kDefaultDatabasePath[] = "/tmp/healthd-task4.sqlite";
const char kSmokeSecret[] = "smoke_secret_001";

DeviceEnvelope buildSmokeEnvelope(qint64 seq) {
    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("telemetry_batch");
    envelope.seq = seq;
    envelope.ts = 1712345678 + seq;
    envelope.deviceId = QStringLiteral("smoke_watch_001");
    envelope.payload.insert(QStringLiteral("hr"), 72);
    envelope.payload.insert(QStringLiteral("spo2"), 98);
    envelope.payload.insert(QStringLiteral("imu_fall_valid"), true);
    envelope.payload.insert(QStringLiteral("imu_fall_class"), 2);
    envelope.payload.insert(QStringLiteral("imu_nonfall_prob"), 0.02);
    envelope.payload.insert(QStringLiteral("imu_preimpact_prob"), 0.08);
    envelope.payload.insert(QStringLiteral("imu_fall_prob"), 0.90);
    return envelope;
}

DeviceEnvelope buildAuthHelloEnvelope(const QString &clientNonce) {
    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("auth_hello");
    envelope.seq = 1;
    envelope.ts = 1712345678;
    envelope.deviceId = QStringLiteral("smoke_watch_001");
    envelope.payload.insert(QStringLiteral("device_name"), QStringLiteral("Smoke Watch"));
    envelope.payload.insert(QStringLiteral("firmware_version"), QStringLiteral("1.0.0"));
    envelope.payload.insert(QStringLiteral("hardware_model"), QStringLiteral("esp32-watch"));
    envelope.payload.insert(QStringLiteral("mac"), QStringLiteral("AA:BB:CC:DD:EE:FF"));
    envelope.payload.insert(QStringLiteral("client_nonce"), clientNonce);
    return envelope;
}

DeviceEnvelope buildAuthProofEnvelope(const QString &serverNonce, const QString &clientNonce) {
    const qint64 proofTs = 1712345680;

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("auth_proof");
    envelope.seq = 2;
    envelope.ts = proofTs;
    envelope.deviceId = QStringLiteral("smoke_watch_001");
    envelope.payload.insert(QStringLiteral("client_nonce"), clientNonce);
    envelope.payload.insert(QStringLiteral("ts"), static_cast<double>(proofTs));
    envelope.payload.insert(QStringLiteral("proof"),
        QString::fromUtf8(HmacHelper::sign(
            envelope.deviceId, serverNonce, clientNonce, proofTs, QString::fromUtf8(kSmokeSecret))));
    return envelope;
}

QString resolveMarkerPath() {
    const QString markerPath = qEnvironmentVariable(kMarkerEnvVar);
    if (!markerPath.isEmpty()) {
        return markerPath;
    }
    return QString::fromUtf8(kDefaultMarkerPath);
}

QString resolveDatabasePath() {
    const QString databasePath = qEnvironmentVariable(kDatabaseEnvVar);
    if (!databasePath.isEmpty()) {
        return databasePath;
    }
    return QString::fromUtf8(kDefaultDatabasePath);
}

bool markerEnabledByEnvironment() {
    return !qEnvironmentVariable(kMarkerEnvVar).isEmpty();
}

bool markerContainsTelemetryEvidence(const QString &markerPath, const DeviceEnvelope &envelope) {
    QFile markerFile(markerPath);
    if (!markerFile.exists() || !markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QByteArray content = markerFile.readAll();
    const QByteArray expectedType = QByteArrayLiteral("\"type\":\"telemetry_batch\"");
    const QByteArray expectedSeq = QByteArrayLiteral("\"seq\":\"")
        + QByteArray::number(envelope.seq) + QByteArrayLiteral("\"");
    const QByteArray expectedDeviceId
        = QByteArrayLiteral("\"device_id\":\"") + envelope.deviceId.toUtf8() + QByteArrayLiteral("\"");
    return content.contains(expectedType) && content.contains(expectedSeq)
        && content.contains(expectedDeviceId);
}

bool preProvisionActiveDevice(const QString &databasePath, QString *error) {
    const QString connectionName = QStringLiteral("healthd_smoke_db");
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            if (error) {
                *error = db.lastError().text();
            }
        } else {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            QSqlQuery query(db);
            query.prepare(QStringLiteral(
                "INSERT INTO devices ("
                "device_id, device_name, status, secret_hash, bind_mode, firmware_version, "
                "hardware_model, last_seen_at, last_ip, last_rssi, created_at, updated_at"
                ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(device_id) DO UPDATE SET "
                "device_name=excluded.device_name, "
                "status=excluded.status, "
                "secret_hash=excluded.secret_hash, "
                "bind_mode=excluded.bind_mode, "
                "firmware_version=excluded.firmware_version, "
                "hardware_model=excluded.hardware_model, "
                "updated_at=excluded.updated_at"));
            query.addBindValue(QStringLiteral("smoke_watch_001"));
            query.addBindValue(QStringLiteral("Smoke Watch"));
            query.addBindValue(QStringLiteral("active"));
            query.addBindValue(QString::fromUtf8(kSmokeSecret));
            query.addBindValue(QStringLiteral("pre_shared"));
            query.addBindValue(QStringLiteral("1.0.0"));
            query.addBindValue(QStringLiteral("esp32-watch"));
            query.addBindValue(0);
            query.addBindValue(QStringLiteral("127.0.0.1"));
            query.addBindValue(-50);
            query.addBindValue(now);
            query.addBindValue(now);
            ok = query.exec();
            if (!ok && error) {
                *error = query.lastError().text();
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool readEnvelope(QTcpSocket *socket, DeviceEnvelope *out, int timeoutMs = 1000) {
    if (!socket || !out) {
        return false;
    }

    QByteArray buffer;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (socket->bytesAvailable() == 0 && !socket->waitForReadyRead(50)) {
            continue;
        }
        buffer.append(socket->readAll());
        while (buffer.size() >= 4) {
            const quint32 len = (static_cast<quint32>(static_cast<quint8>(buffer.at(0))) << 24U)
                | (static_cast<quint32>(static_cast<quint8>(buffer.at(1))) << 16U)
                | (static_cast<quint32>(static_cast<quint8>(buffer.at(2))) << 8U)
                | static_cast<quint32>(static_cast<quint8>(buffer.at(3)));
            const int frameLen = 4 + static_cast<int>(len);
            if (buffer.size() < frameLen) {
                break;
            }

            const QByteArray frame = buffer.left(frameLen);
            buffer.remove(0, frameLen);
            if (DeviceFrameCodec::decode(frame, out)) {
                return true;
            }
        }
    }

    return false;
}
} // namespace

class HealthdTcpSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void sendsOneTelemetryFrame();

private:
    QProcess healthd_;
    std::unique_ptr<QTemporaryDir> tempDir_;
};

void HealthdTcpSmokeTest::initTestCase() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());

    const QString databasePath = tempDir_->filePath(QStringLiteral("healthd_smoke.sqlite"));
    qputenv(kDatabaseEnvVar, databasePath.toUtf8());

    const QString healthdPath = QString::fromLocal8Bit(HEALTHD_TEST_BINARY_PATH);
    QVERIFY2(!healthdPath.isEmpty(), "HEALTHD_TEST_BINARY_PATH is not configured");
    QVERIFY2(QFile::exists(healthdPath), qPrintable(QStringLiteral("healthd not found: %1").arg(healthdPath)));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QString::fromUtf8(kDatabaseEnvVar), databasePath);
    healthd_.setProcessEnvironment(env);
    healthd_.setProgram(healthdPath);
    healthd_.setProcessChannelMode(QProcess::MergedChannels);
    healthd_.start();
    QVERIFY2(healthd_.waitForStarted(3000), qPrintable(healthd_.errorString()));

    bool listening = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (healthd_.state() == QProcess::NotRunning) {
            break;
        }
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, kHealthdPort);
        if (probe.waitForConnected(100)) {
            listening = true;
            probe.disconnectFromHost();
            break;
        }
        probe.abort();
        QTest::qWait(50);
    }

    QVERIFY2(listening,
        qPrintable(QStringLiteral("healthd did not listen on %1, output=%2")
                       .arg(kHealthdPort)
                       .arg(QString::fromUtf8(healthd_.readAll()))));
}

void HealthdTcpSmokeTest::cleanupTestCase() {
    if (healthd_.state() != QProcess::NotRunning) {
        healthd_.terminate();
        if (!healthd_.waitForFinished(3000)) {
            healthd_.kill();
            healthd_.waitForFinished(3000);
        }
    }
    qunsetenv(kDatabaseEnvVar);
}

void HealthdTcpSmokeTest::sendsOneTelemetryFrame() {
    const QString markerPath = resolveMarkerPath();
    const QString enablePath = QString::fromUtf8(kMarkerEnablePath);
    const bool markerAlreadyEnabled = markerEnabledByEnvironment();
    QFile::remove(markerPath);
    QFile::remove(enablePath);
    QVERIFY2(!QFile::exists(markerPath), qPrintable(QStringLiteral("failed to clear stale marker %1")
                                                     .arg(markerPath)));
    QVERIFY2(!QFile::exists(enablePath), qPrintable(QStringLiteral("failed to clear stale enable file %1")
                                                     .arg(enablePath)));

    QTcpSocket socket;

    bool connected = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        socket.connectToHost(QHostAddress::LocalHost, kHealthdPort);
        if (socket.waitForConnected(100)) {
            connected = true;
            break;
        }
        socket.abort();
        QTest::qWait(50);
    }
    QVERIFY2(connected, qPrintable(socket.errorString()));

    QString error;
    QVERIFY2(preProvisionActiveDevice(resolveDatabasePath(), &error), qPrintable(error));

    const QString clientNonce = QStringLiteral("client-nonce-smoke");
    const DeviceEnvelope authHelloEnvelope = buildAuthHelloEnvelope(clientNonce);
    const QByteArray authHelloFrame = DeviceFrameCodec::encode(authHelloEnvelope);
    QVERIFY2(!authHelloFrame.isEmpty(), "Auth hello should encode into a frame.");
    qint64 written = socket.write(authHelloFrame);
    QCOMPARE(written, static_cast<qint64>(authHelloFrame.size()));
    QVERIFY(socket.waitForBytesWritten(1000));

    DeviceEnvelope authChallengeEnvelope;
    QVERIFY2(readEnvelope(&socket, &authChallengeEnvelope), "Expected auth_challenge frame.");
    QCOMPARE(authChallengeEnvelope.type, QStringLiteral("auth_challenge"));
    const QString serverNonce
        = authChallengeEnvelope.payload.value(QStringLiteral("server_nonce")).toString();
    QVERIFY2(!serverNonce.isEmpty(), "Server nonce should not be empty.");

    const DeviceEnvelope authProofEnvelope = buildAuthProofEnvelope(serverNonce, clientNonce);
    const QByteArray authProofFrame = DeviceFrameCodec::encode(authProofEnvelope);
    QVERIFY2(!authProofFrame.isEmpty(), "Auth proof should encode into a frame.");
    written = socket.write(authProofFrame);
    QCOMPARE(written, static_cast<qint64>(authProofFrame.size()));
    QVERIFY(socket.waitForBytesWritten(1000));

    DeviceEnvelope authResultEnvelope;
    QVERIFY2(readEnvelope(&socket, &authResultEnvelope), "Expected auth_result frame.");
    QCOMPARE(authResultEnvelope.type, QStringLiteral("auth_result"));
    QCOMPARE(authResultEnvelope.payload.value(QStringLiteral("result")).toString(), QStringLiteral("ok"));

    const DeviceEnvelope preEnableEnvelope = buildSmokeEnvelope(1);
    const QByteArray preEnableFrame = DeviceFrameCodec::encode(preEnableEnvelope);
    QVERIFY2(!preEnableFrame.isEmpty(), "Smoke envelope should encode into a frame.");

    written = socket.write(preEnableFrame);
    QCOMPARE(written, static_cast<qint64>(preEnableFrame.size()));
    QVERIFY(socket.waitForBytesWritten(1000));

    if (!markerAlreadyEnabled) {
        QTest::qWait(kNoMarkerWaitMs);
        QVERIFY2(!QFile::exists(markerPath),
            qPrintable(QStringLiteral("marker should stay disabled before enable file appears: %1")
                           .arg(markerPath)));

        QFile enableFile(enablePath);
        QVERIFY2(enableFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text),
            qPrintable(QStringLiteral("failed to create marker enable file %1").arg(enablePath)));
        enableFile.write("1\n");
        enableFile.close();
        QVERIFY2(QFile::exists(enablePath),
            qPrintable(QStringLiteral("marker enable file missing %1").arg(enablePath)));
    }

    const DeviceEnvelope envelope = buildSmokeEnvelope(2);
    const QByteArray frame = DeviceFrameCodec::encode(envelope);
    QVERIFY2(!frame.isEmpty(), "Smoke envelope should encode into a frame.");

    written = socket.write(frame);
    QCOMPARE(written, static_cast<qint64>(frame.size()));
    QVERIFY(socket.waitForBytesWritten(1000));

    socket.disconnectFromHost();
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        QVERIFY(socket.waitForDisconnected(1000));
    }

    bool markerSeen = false;
    for (int elapsed = 0; elapsed <= kMarkerWaitMs; elapsed += kMarkerPollMs) {
        if (markerContainsTelemetryEvidence(markerPath, envelope)) {
            markerSeen = true;
            break;
        }
        QTest::qWait(kMarkerPollMs);
    }

    QFile markerFile(markerPath);
    QString markerSnapshot;
    if (markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        markerSnapshot = QString::fromUtf8(markerFile.readAll());
    }
    QVERIFY2(markerSeen,
        qPrintable(QStringLiteral("Telemetry marker evidence missing in %1, content=%2")
                       .arg(markerPath, markerSnapshot)));

    if (!markerAlreadyEnabled) {
        QFile::remove(enablePath);
    }
}

QTEST_MAIN(HealthdTcpSmokeTest)

#include "healthd_tcp_smoke_test.moc"
