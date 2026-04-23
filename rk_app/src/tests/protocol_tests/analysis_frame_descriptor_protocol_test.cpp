#include "models/fall_models.h"
#include "protocol/analysis_frame_descriptor_protocol.h"

#include <QtTest/QTest>

class AnalysisFrameDescriptorProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsDescriptor();
    void rejectsInvalidSlotIndex();
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
