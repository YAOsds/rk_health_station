#include "protocol/analysis_stream_protocol.h"

#include <QtTest/QTest>

class AnalysisStreamProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsFramePacket();
    void extractsFirstPacketFromConcatenatedStream();
};

void AnalysisStreamProtocolTest::roundTripsFramePacket() {
    AnalysisFramePacket packet;
    packet.frameId = 7;
    packet.timestampMs = 123456;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 640;
    packet.pixelFormat = AnalysisPixelFormat::Jpeg;
    packet.payload = QByteArray::fromHex("FFD8FFD9");

    const QByteArray encoded = encodeAnalysisFramePacket(packet);
    AnalysisFramePacket decoded;
    QVERIFY(decodeAnalysisFramePacket(encoded, &decoded));
    QCOMPARE(decoded.frameId, packet.frameId);
    QCOMPARE(decoded.cameraId, packet.cameraId);
    QCOMPARE(decoded.pixelFormat, packet.pixelFormat);
    QCOMPARE(decoded.payload, packet.payload);
}

void AnalysisStreamProtocolTest::extractsFirstPacketFromConcatenatedStream() {
    AnalysisFramePacket first;
    first.frameId = 1;
    first.cameraId = QStringLiteral("front_cam");
    first.payload = QByteArray("one");

    AnalysisFramePacket second;
    second.frameId = 2;
    second.cameraId = QStringLiteral("front_cam");
    second.payload = QByteArray("two");

    QByteArray stream = encodeAnalysisFramePacket(first) + encodeAnalysisFramePacket(second);

    AnalysisFramePacket decoded;
    QVERIFY(takeFirstAnalysisFramePacket(&stream, &decoded));
    QCOMPARE(decoded.frameId, first.frameId);
    QCOMPARE(decoded.payload, first.payload);

    QVERIFY(takeFirstAnalysisFramePacket(&stream, &decoded));
    QCOMPARE(decoded.frameId, second.frameId);
    QCOMPARE(decoded.payload, second.payload);
    QVERIFY(stream.isEmpty());
}

QTEST_MAIN(AnalysisStreamProtocolTest)
#include "analysis_stream_protocol_test.moc"
