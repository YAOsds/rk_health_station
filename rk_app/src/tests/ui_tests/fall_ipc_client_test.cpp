#include "ipc_client/fall_ipc_client.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QtTest/QTest>

class FallIpcClientTest : public QObject {
    Q_OBJECT

private slots:
    void receivesRealtimeClassification();
    void usesEnvironmentSocketNameByDefault();
};

void FallIpcClientTest::receivesRealtimeClassification() {
    const QString socketName = QStringLiteral("/tmp/rk_fall_ui_client_test.sock");
    QLocalServer::removeServer(socketName);

    QLocalServer server;
    QVERIFY(server.listen(socketName));

    FallIpcClient client(socketName);
    QSignalSpy classificationSpy(&client, SIGNAL(classificationUpdated(FallClassificationResult)));

    QVERIFY(client.connectToBackend());
    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(socket->bytesAvailable() > 0 || socket->waitForReadyRead(50), 2000);
    QVERIFY(socket->readAll().contains("subscribe_classification"));

    const QByteArray line = QByteArrayLiteral(
        "{\"type\":\"classification\",\"camera_id\":\"front_cam\",\"ts\":1776359310534,\"state\":\"fall\",\"confidence\":0.93}\n");
    socket->write(line);
    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(classificationSpy.count(), 1, 2000);
    const FallClassificationResult result = classificationSpy.takeFirst().at(0).value<FallClassificationResult>();
    QCOMPARE(result.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(result.state, QStringLiteral("fall"));
    QCOMPARE(result.confidence, 0.93);
}

void FallIpcClientTest::usesEnvironmentSocketNameByDefault() {
    const QByteArray envName = QByteArrayLiteral("RK_FALL_SOCKET_NAME");
    const QByteArray socketPath = QByteArrayLiteral("/tmp/rk_fall_ui_env_test.sock");
    qputenv(envName.constData(), socketPath);
    QLocalServer::removeServer(QString::fromUtf8(socketPath));

    QLocalServer server;
    QVERIFY(server.listen(QString::fromUtf8(socketPath)));

    FallIpcClient client;
    QVERIFY(client.connectToBackend());
    QVERIFY(server.waitForNewConnection(2000));

    qunsetenv(envName.constData());
}

QTEST_MAIN(FallIpcClientTest)
#include "fall_ipc_client_test.moc"
