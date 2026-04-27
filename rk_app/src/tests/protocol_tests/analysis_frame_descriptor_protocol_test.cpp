#include "models/fall_models.h"
#include "protocol/analysis_frame_descriptor_protocol.h"

#include <QtTest/QTest>

class AnalysisFrameDescriptorProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsDescriptor();
    void rejectsInvalidSlotIndex();
    void roundTripsPosePreprocessMetadata();
    void roundTripsDmaBufTransportMetadata();
};

void AnalysisFrameDescriptorProtocolTest::roundTripsDescriptor() {
    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 77;
    descriptor.timestampMs = 1777000000123;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 640;
    descriptor.height = 640;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.slotIndex = 2;
    descriptor.sequence = 18;
    descriptor.payloadBytes = 640 * 640 * 3;

    const QByteArray encoded = encodeAnalysisFrameDescriptor(descriptor);
    AnalysisFrameDescriptor decoded;
    QVERIFY(decodeAnalysisFrameDescriptor(encoded, &decoded));
    QCOMPARE(decoded.slotIndex, 2u);
    QCOMPARE(decoded.sequence, 18u);
    QCOMPARE(decoded.payloadBytes, 640u * 640u * 3u);
}

void AnalysisFrameDescriptorProtocolTest::roundTripsPosePreprocessMetadata() {
    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 99;
    descriptor.timestampMs = 1777000000333;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 640;
    descriptor.height = 640;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.slotIndex = 3;
    descriptor.sequence = 20;
    descriptor.payloadBytes = 640 * 640 * 3;
    descriptor.posePreprocessed = true;
    descriptor.poseXPad = 0;
    descriptor.poseYPad = 80;
    descriptor.poseScale = 1.0f;

    const QByteArray encoded = encodeAnalysisFrameDescriptor(descriptor);
    AnalysisFrameDescriptor decoded;
    QVERIFY(decodeAnalysisFrameDescriptor(encoded, &decoded));
    QVERIFY(decoded.posePreprocessed);
    QCOMPARE(decoded.poseXPad, 0);
    QCOMPARE(decoded.poseYPad, 80);
    QCOMPARE(decoded.poseScale, 1.0f);
}

void AnalysisFrameDescriptorProtocolTest::roundTripsDmaBufTransportMetadata() {
    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 120;
    descriptor.timestampMs = 1777000000444;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 640;
    descriptor.height = 640;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.slotIndex = 4;
    descriptor.sequence = 30;
    descriptor.payloadBytes = 640 * 640 * 3;
    descriptor.payloadTransport = AnalysisPayloadTransport::DmaBuf;
    descriptor.dmaBufPlaneCount = 1;
    descriptor.dmaBufOffset = 0;
    descriptor.dmaBufStrideBytes = 640 * 3;

    const QByteArray encoded = encodeAnalysisFrameDescriptor(descriptor);
    AnalysisFrameDescriptor decoded;
    QVERIFY(decodeAnalysisFrameDescriptor(encoded, &decoded));
    QCOMPARE(decoded.payloadTransport, AnalysisPayloadTransport::DmaBuf);
    QCOMPARE(decoded.dmaBufPlaneCount, 1u);
    QCOMPARE(decoded.dmaBufOffset, 0u);
    QCOMPARE(decoded.dmaBufStrideBytes, 640u * 3u);
}

void AnalysisFrameDescriptorProtocolTest::rejectsInvalidSlotIndex() {
    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 88;
    descriptor.timestampMs = 1777000000222;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 640;
    descriptor.height = 640;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.slotIndex = 9999;
    descriptor.sequence = 1;
    descriptor.payloadBytes = 640 * 640 * 3;

    const QByteArray encoded = encodeAnalysisFrameDescriptor(descriptor);
    AnalysisFrameDescriptor decoded;
    QVERIFY(!decodeAnalysisFrameDescriptor(encoded, &decoded));
}

QTEST_MAIN(AnalysisFrameDescriptorProtocolTest)
#include "analysis_frame_descriptor_protocol_test.moc"
