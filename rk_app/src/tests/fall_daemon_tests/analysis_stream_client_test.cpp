#include "ingest/analysis_stream_client.h"
#include "protocol/analysis_stream_protocol.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QtTest/QTest>

class AnalysisStreamClientTest : public QObject {
    Q_OBJECT

private slots:
    void decodesIncomingFramePackets();
    void decodesMultiplePacketsFromSingleRead();
    void reconnectsAfterServerRestarts();
};

void AnalysisStreamClientTest::decodesIncomingFramePackets() {
    QLocalServer server;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_test.sock"));
    QVERIFY(server.listen(QStringLiteral("/tmp/rk_video_analysis_test.sock")));

    AnalysisFramePacket packet;
    packet.frameId = 11;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 640;
    packet.payload = QByteArray("jpeg-bytes");

    AnalysisStreamClient client(QStringLiteral("/tmp/rk_video_analysis_test.sock"));
    QSignalSpy spy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    client.start();

    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);
    socket->write(encodeAnalysisFramePacket(packet));
    socket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 1, 2000);
}

void AnalysisStreamClientTest::decodesMultiplePacketsFromSingleRead() {
    QLocalServer server;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_test2.sock"));
    QVERIFY(server.listen(QStringLiteral("/tmp/rk_video_analysis_test2.sock")));

    AnalysisFramePacket first;
    first.frameId = 21;
    first.cameraId = QStringLiteral("front_cam");
    first.payload = QByteArray("one");

    AnalysisFramePacket second;
    second.frameId = 22;
    second.cameraId = QStringLiteral("front_cam");
    second.payload = QByteArray("two");

    AnalysisStreamClient client(QStringLiteral("/tmp/rk_video_analysis_test2.sock"));
    QSignalSpy spy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    client.start();

    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);
    socket->write(encodeAnalysisFramePacket(first) + encodeAnalysisFramePacket(second));
    socket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 2, 2000);
}

void AnalysisStreamClientTest::reconnectsAfterServerRestarts() {
    const QString socketName = QStringLiteral("/tmp/rk_video_analysis_reconnect_test.sock");
    QLocalServer::removeServer(socketName);

    QLocalServer firstServer;
    QVERIFY(firstServer.listen(socketName));

    AnalysisFramePacket first;
    first.frameId = 31;
    first.cameraId = QStringLiteral("front_cam");
    first.payload = QByteArray("one");

    AnalysisFramePacket second;
    second.frameId = 32;
    second.cameraId = QStringLiteral("front_cam");
    second.payload = QByteArray("two");

    AnalysisStreamClient client(socketName);
    QSignalSpy frameSpy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    QSignalSpy statusSpy(&client, SIGNAL(statusChanged(bool)));
    client.start();

    QVERIFY(firstServer.waitForNewConnection(2000));
    QLocalSocket *firstSocket = firstServer.nextPendingConnection();
    QVERIFY(firstSocket != nullptr);
    firstSocket->write(encodeAnalysisFramePacket(first));
    firstSocket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() == 1, 2000);

    firstSocket->disconnectFromServer();
    QTRY_VERIFY_WITH_TIMEOUT(statusSpy.count() >= 2, 2000);

    delete firstSocket;
    firstServer.close();
    QLocalServer::removeServer(socketName);

    QLocalServer secondServer;
    QVERIFY(secondServer.listen(socketName));
    QTRY_VERIFY_WITH_TIMEOUT(secondServer.hasPendingConnections(), 3000);
    QLocalSocket *secondSocket = secondServer.nextPendingConnection();
    QVERIFY(secondSocket != nullptr);
    secondSocket->write(encodeAnalysisFramePacket(second));
    secondSocket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() == 2, 3000);
}

QTEST_MAIN(AnalysisStreamClientTest)
#include "analysis_stream_client_test.moc"
