#include "analysis/shared_memory_frame_ring.h"
#include "ingest/analysis_stream_client.h"
#include "protocol/analysis_frame_descriptor_protocol.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QtTest/QTest>

namespace {
AnalysisFrameDescriptor publishPacketDescriptor(
    SharedMemoryFrameRingWriter *writer, const AnalysisFramePacket &packet) {
    const SharedFramePublishResult publish = writer->publish(packet);

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = packet.frameId;
    descriptor.timestampMs = packet.timestampMs;
    descriptor.cameraId = packet.cameraId;
    descriptor.width = packet.width;
    descriptor.height = packet.height;
    descriptor.pixelFormat = packet.pixelFormat;
    descriptor.slotIndex = publish.slotIndex;
    descriptor.sequence = publish.sequence;
    descriptor.payloadBytes = publish.payloadBytes;
    return descriptor;
}
}

class AnalysisStreamClientTest : public QObject {
    Q_OBJECT

private slots:
    void decodesIncomingFramePackets();
    void decodesRgbFramePackets();
    void emitsEveryPacketFromSingleReadBurst();
    void reconnectsAfterServerRestarts();
    void clearsPartialPacketBufferBeforeReconnect();
};

void AnalysisStreamClientTest::decodesIncomingFramePackets() {
    QLocalServer server;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_test.sock"));
    QVERIFY(server.listen(QStringLiteral("/tmp/rk_video_analysis_test.sock")));

    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 4, 640 * 640 * 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket packet;
    packet.frameId = 11;
    packet.timestampMs = 100;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 640;
    packet.width = 640;
    packet.height = 640;
    packet.payload = QByteArray("jpeg-bytes");

    AnalysisStreamClient client(QStringLiteral("/tmp/rk_video_analysis_test.sock"));
    QSignalSpy spy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    client.start();

    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);
    socket->write(encodeAnalysisFrameDescriptor(publishPacketDescriptor(&writer, packet)));
    socket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 1, 2000);
}

void AnalysisStreamClientTest::decodesRgbFramePackets() {
    QLocalServer server;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_rgb_test.sock"));
    QVERIFY(server.listen(QStringLiteral("/tmp/rk_video_analysis_rgb_test.sock")));

    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 4, 4 * 3 * 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket packet;
    packet.frameId = 55;
    packet.timestampMs = 200;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 4;
    packet.height = 3;
    packet.pixelFormat = AnalysisPixelFormat::Rgb;
    packet.payload = QByteArray(4 * 3 * 3, '\x33');

    AnalysisStreamClient client(QStringLiteral("/tmp/rk_video_analysis_rgb_test.sock"));
    QSignalSpy spy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    client.start();

    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);
    socket->write(encodeAnalysisFrameDescriptor(publishPacketDescriptor(&writer, packet)));
    socket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 1, 2000);
    const AnalysisFramePacket decoded = qvariant_cast<AnalysisFramePacket>(spy.takeFirst().at(0));
    QCOMPARE(decoded.pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(decoded.payload, packet.payload);
}

void AnalysisStreamClientTest::emitsEveryPacketFromSingleReadBurst() {
    QLocalServer server;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_test2.sock"));
    QVERIFY(server.listen(QStringLiteral("/tmp/rk_video_analysis_test2.sock")));

    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 4, 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket first;
    first.frameId = 21;
    first.timestampMs = 300;
    first.cameraId = QStringLiteral("front_cam");
    first.width = 1;
    first.height = 1;
    first.pixelFormat = AnalysisPixelFormat::Jpeg;
    first.payload = QByteArray("one");

    AnalysisFramePacket second;
    second.frameId = 22;
    second.timestampMs = 301;
    second.cameraId = QStringLiteral("front_cam");
    second.width = 1;
    second.height = 1;
    second.pixelFormat = AnalysisPixelFormat::Jpeg;
    second.payload = QByteArray("two");

    AnalysisStreamClient client(QStringLiteral("/tmp/rk_video_analysis_test2.sock"));
    QSignalSpy spy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    client.start();

    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);
    socket->write(encodeAnalysisFrameDescriptor(publishPacketDescriptor(&writer, first))
        + encodeAnalysisFrameDescriptor(publishPacketDescriptor(&writer, second)));
    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 2000);
    const AnalysisFramePacket firstDelivered = qvariant_cast<AnalysisFramePacket>(spy.takeFirst().at(0));
    QCOMPARE(firstDelivered.frameId, first.frameId);
    QCOMPARE(firstDelivered.payload, first.payload);

    const AnalysisFramePacket secondDelivered = qvariant_cast<AnalysisFramePacket>(spy.takeFirst().at(0));
    QCOMPARE(secondDelivered.frameId, second.frameId);
    QCOMPARE(secondDelivered.payload, second.payload);
}

void AnalysisStreamClientTest::reconnectsAfterServerRestarts() {
    const QString socketName = QStringLiteral("/tmp/rk_video_analysis_reconnect_test.sock");
    QLocalServer::removeServer(socketName);

    QLocalServer firstServer;
    QVERIFY(firstServer.listen(socketName));

    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 4, 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket first;
    first.frameId = 31;
    first.timestampMs = 400;
    first.cameraId = QStringLiteral("front_cam");
    first.width = 1;
    first.height = 1;
    first.pixelFormat = AnalysisPixelFormat::Jpeg;
    first.payload = QByteArray("one");

    AnalysisFramePacket second;
    second.frameId = 32;
    second.timestampMs = 401;
    second.cameraId = QStringLiteral("front_cam");
    second.width = 1;
    second.height = 1;
    second.pixelFormat = AnalysisPixelFormat::Jpeg;
    second.payload = QByteArray("two");

    AnalysisStreamClient client(socketName);
    QSignalSpy frameSpy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    QSignalSpy statusSpy(&client, SIGNAL(statusChanged(bool)));
    client.start();

    QVERIFY(firstServer.waitForNewConnection(2000));
    QLocalSocket *firstSocket = firstServer.nextPendingConnection();
    QVERIFY(firstSocket != nullptr);
    firstSocket->write(encodeAnalysisFrameDescriptor(publishPacketDescriptor(&writer, first)));
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
    secondSocket->write(encodeAnalysisFrameDescriptor(publishPacketDescriptor(&writer, second)));
    secondSocket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() == 2, 3000);
}

void AnalysisStreamClientTest::clearsPartialPacketBufferBeforeReconnect() {
    const QString socketName = QStringLiteral("/tmp/rk_video_analysis_partial_reconnect_test.sock");
    QLocalServer::removeServer(socketName);

    QLocalServer firstServer;
    QVERIFY(firstServer.listen(socketName));

    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 4, 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket first;
    first.frameId = 41;
    first.timestampMs = 500;
    first.cameraId = QStringLiteral("front_cam");
    first.width = 1;
    first.height = 1;
    first.pixelFormat = AnalysisPixelFormat::Jpeg;
    first.payload = QByteArray("one");

    AnalysisFramePacket second;
    second.frameId = 42;
    second.timestampMs = 501;
    second.cameraId = QStringLiteral("front_cam");
    second.width = 1;
    second.height = 1;
    second.pixelFormat = AnalysisPixelFormat::Jpeg;
    second.payload = QByteArray("two");

    const QByteArray firstEncoded = encodeAnalysisFrameDescriptor(publishPacketDescriptor(&writer, first));
    const int splitIndex = firstEncoded.size() / 2;

    AnalysisStreamClient client(socketName);
    QSignalSpy frameSpy(&client, SIGNAL(frameReceived(AnalysisFramePacket)));
    client.start();

    QVERIFY(firstServer.waitForNewConnection(2000));
    QLocalSocket *firstSocket = firstServer.nextPendingConnection();
    QVERIFY(firstSocket != nullptr);
    firstSocket->write(firstEncoded.left(splitIndex));
    firstSocket->flush();
    firstSocket->disconnectFromServer();
    QTRY_VERIFY_WITH_TIMEOUT(firstSocket->state() == QLocalSocket::UnconnectedState, 2000);

    delete firstSocket;
    firstServer.close();
    QLocalServer::removeServer(socketName);

    QLocalServer secondServer;
    QVERIFY(secondServer.listen(socketName));
    QTRY_VERIFY_WITH_TIMEOUT(secondServer.hasPendingConnections(), 3000);
    QLocalSocket *secondSocket = secondServer.nextPendingConnection();
    QVERIFY(secondSocket != nullptr);
    secondSocket->write(encodeAnalysisFrameDescriptor(publishPacketDescriptor(&writer, second)));
    secondSocket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() == 1, 3000);
}

QTEST_MAIN(AnalysisStreamClientTest)
#include "analysis_stream_client_test.moc"
