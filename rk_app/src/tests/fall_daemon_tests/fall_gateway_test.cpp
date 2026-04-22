#include "ipc/fall_gateway.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QtTest/QTest>

class FallGatewayTest : public QObject {
    Q_OBJECT

private slots:
    void returnsRuntimeStatus();
    void pushesClassificationToSubscribers();
    void broadcastsClassificationBatchToSubscribers();
};

void FallGatewayTest::returnsRuntimeStatus() {
    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_test.sock"));

    FallRuntimeStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.inputConnected = true;
    status.poseModelReady = false;
    status.actionModelReady = false;
    status.latestState = QStringLiteral("monitoring");

    FallGateway gateway(status);
    gateway.setSocketName(QStringLiteral("/tmp/rk_fall_test.sock"));
    QVERIFY(gateway.start());

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("/tmp/rk_fall_test.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 2000);
    QVERIFY(socket.readAll().contains("monitoring"));

    gateway.stop();
    qunsetenv("RK_FALL_SOCKET_NAME");
}

void FallGatewayTest::pushesClassificationToSubscribers() {
    const QString socketName = QStringLiteral("/tmp/rk_fall_subscribe_test.sock");
    QLocalServer::removeServer(socketName);

    FallGateway gateway{FallRuntimeStatus()};
    gateway.setSocketName(socketName);
    QVERIFY(gateway.start());

    QLocalSocket subscriber;
    subscriber.connectToServer(socketName);
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();
    QTest::qWait(50);

    FallClassificationResult result;
    result.cameraId = QStringLiteral("front_cam");
    result.timestampMs = 1776356876397;
    result.state = QStringLiteral("fall");
    result.confidence = 0.93;
    gateway.publishClassification(result);

    QTRY_VERIFY_WITH_TIMEOUT(subscriber.bytesAvailable() > 0 || subscriber.waitForReadyRead(50), 2000);
    const QJsonObject json = QJsonDocument::fromJson(subscriber.readAll().trimmed()).object();
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification"));
    QCOMPARE(json.value(QStringLiteral("state")).toString(), QStringLiteral("fall"));

    gateway.stop();
}

void FallGatewayTest::broadcastsClassificationBatchToSubscribers() {
    const QString socketName = QStringLiteral("/tmp/rk_fall_gateway_batch_test.sock");
    QLocalServer::removeServer(socketName);

    FallGateway gateway{FallRuntimeStatus()};
    gateway.setSocketName(socketName);
    QVERIFY(gateway.start());

    QLocalSocket subscriber;
    subscriber.connectToServer(socketName);
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();
    QTest::qWait(50);

    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1776367000000;
    FallClassificationEntry first;
    first.state = QStringLiteral("stand");
    first.confidence = 0.91;
    batch.results.push_back(first);

    FallClassificationEntry second;
    second.state = QStringLiteral("fall");
    second.confidence = 0.96;
    batch.results.push_back(second);

    gateway.publishClassificationBatch(batch);

    QTRY_VERIFY_WITH_TIMEOUT(subscriber.bytesAvailable() > 0 || subscriber.waitForReadyRead(50), 2000);
    const QJsonObject json = QJsonDocument::fromJson(subscriber.readAll().trimmed()).object();
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification_batch"));
    QCOMPARE(json.value(QStringLiteral("person_count")).toInt(), 2);

    gateway.stop();
}

QTEST_MAIN(FallGatewayTest)
#include "fall_gateway_test.moc"
