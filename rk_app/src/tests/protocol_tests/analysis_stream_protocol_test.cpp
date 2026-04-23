#include "protocol/analysis_stream_protocol.h"

#include <QtTest/QTest>

class AnalysisStreamProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsFramePacket();
    void roundTripsRgbFramePacket();
    void extractsFirstPacketFromConcatenatedStream();
    void takesFirstPacketFromMixedFormatStream();
    void rejectsInvalidRgbPayloadSize();
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

void AnalysisStreamProtocolTest::roundTripsRgbFramePacket() {
    AnalysisFramePacket packet;
    packet.frameId = 1001;
    packet.timestampMs = 1777000000000;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 4;
    packet.height = 3;
    packet.pixelFormat = AnalysisPixelFormat::Rgb;
    packet.payload = QByteArray(4 * 3 * 3, '\x7f');

    const QByteArray encoded = encodeAnalysisFramePacket(packet);
    AnalysisFramePacket decoded;
    QVERIFY(decodeAnalysisFramePacket(encoded, &decoded));
    QCOMPARE(decoded.frameId, packet.frameId);
    QCOMPARE(decoded.cameraId, packet.cameraId);
    QCOMPARE(decoded.width, 4);
    QCOMPARE(decoded.height, 3);
    QCOMPARE(decoded.pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(decoded.payload.size(), packet.payload.size());
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

void AnalysisStreamProtocolTest::takesFirstPacketFromMixedFormatStream() {
    AnalysisFramePacket first;
    first.frameId = 1;
    first.cameraId = QStringLiteral("front_cam");
    first.width = 640;
    first.height = 480;
    first.pixelFormat = AnalysisPixelFormat::Jpeg;
    first.payload = QByteArray("jpeg-bytes");

    AnalysisFramePacket second;
    second.frameId = 2;
    second.cameraId = QStringLiteral("front_cam");
    second.width = 4;
    second.height = 3;
    second.pixelFormat = AnalysisPixelFormat::Rgb;
    second.payload = QByteArray(4 * 3 * 3, '\x33');

    QByteArray stream = encodeAnalysisFramePacket(first) + encodeAnalysisFramePacket(second);

    AnalysisFramePacket decoded;
    QVERIFY(takeFirstAnalysisFramePacket(&stream, &decoded));
    QCOMPARE(decoded.frameId, first.frameId);
    QCOMPARE(decoded.pixelFormat, AnalysisPixelFormat::Jpeg);
    QCOMPARE(decoded.payload, first.payload);

    QVERIFY(takeFirstAnalysisFramePacket(&stream, &decoded));
    QCOMPARE(decoded.frameId, second.frameId);
    QCOMPARE(decoded.pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(decoded.payload, second.payload);
    QVERIFY(stream.isEmpty());
}

void AnalysisStreamProtocolTest::rejectsInvalidRgbPayloadSize() {
    AnalysisFramePacket packet;
    packet.frameId = 77;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 4;
    packet.height = 4;
    packet.pixelFormat = AnalysisPixelFormat::Rgb;
    packet.payload = QByteArray(4 * 4 * 3 - 1, '\x01');

    const QByteArray encoded = encodeAnalysisFramePacket(packet);
    AnalysisFramePacket decoded;
    QVERIFY(!decodeAnalysisFramePacket(encoded, &decoded));
}

QTEST_MAIN(AnalysisStreamProtocolTest)
#include "analysis_stream_protocol_test.moc"
