#include "analysis/shared_memory_frame_ring.h"
#include "ingest/shared_memory_frame_reader.h"

#include <QtTest/QTest>

class SharedMemoryFrameReaderTest : public QObject {
    Q_OBJECT

private slots:
    void rejectsDescriptorForOverwrittenSlot();
    void remapsWhenRingIsRecreatedForSameCamera();
};

void SharedMemoryFrameReaderTest::rejectsDescriptorForOverwrittenSlot() {
    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 1, 4 * 4 * 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket first;
    first.frameId = 1;
    first.timestampMs = 10;
    first.cameraId = QStringLiteral("front_cam");
    first.width = 4;
    first.height = 4;
    first.pixelFormat = AnalysisPixelFormat::Rgb;
    first.payload = QByteArray(4 * 4 * 3, '\x11');

    const SharedFramePublishResult firstResult = writer.publish(first);

    AnalysisFramePacket second = first;
    second.frameId = 2;
    second.payload = QByteArray(4 * 4 * 3, '\x22');
    writer.publish(second);

    SharedMemoryFrameReader reader;
    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = first.frameId;
    descriptor.timestampMs = first.timestampMs;
    descriptor.cameraId = first.cameraId;
    descriptor.width = first.width;
    descriptor.height = first.height;
    descriptor.pixelFormat = first.pixelFormat;
    descriptor.slotIndex = firstResult.slotIndex;
    descriptor.sequence = firstResult.sequence;
    descriptor.payloadBytes = first.payload.size();

    AnalysisFramePacket decoded;
    QString error;
    QVERIFY(!reader.read(descriptor, &decoded, &error));
    QCOMPARE(error, QStringLiteral("analysis_slot_overwritten"));
}

void SharedMemoryFrameReaderTest::remapsWhenRingIsRecreatedForSameCamera() {
    SharedMemoryFrameReader reader;

    AnalysisFrameDescriptor firstDescriptor;
    {
        SharedMemoryFrameRingWriter firstWriter(QStringLiteral("front_cam"), 1, 4 * 4 * 3);
        QVERIFY(firstWriter.initialize());

        AnalysisFramePacket first;
        first.frameId = 1;
        first.timestampMs = 10;
        first.cameraId = QStringLiteral("front_cam");
        first.width = 4;
        first.height = 4;
        first.pixelFormat = AnalysisPixelFormat::Rgb;
        first.payload = QByteArray(4 * 4 * 3, '\x11');

        const SharedFramePublishResult firstPublish = firstWriter.publish(first);

        firstDescriptor.frameId = first.frameId;
        firstDescriptor.timestampMs = first.timestampMs;
        firstDescriptor.cameraId = first.cameraId;
        firstDescriptor.width = first.width;
        firstDescriptor.height = first.height;
        firstDescriptor.pixelFormat = first.pixelFormat;
        firstDescriptor.slotIndex = firstPublish.slotIndex;
        firstDescriptor.sequence = firstPublish.sequence;
        firstDescriptor.payloadBytes = first.payload.size();

        AnalysisFramePacket firstDecoded;
        QString firstError;
        QVERIFY(reader.read(firstDescriptor, &firstDecoded, &firstError));
        QCOMPARE(firstError, QString());
        QCOMPARE(firstDecoded.payload, first.payload);
    }

    SharedMemoryFrameRingWriter secondWriter(QStringLiteral("front_cam"), 1, 4 * 4 * 3);
    QVERIFY(secondWriter.initialize());

    AnalysisFramePacket second;
    second.frameId = 2;
    second.timestampMs = 20;
    second.cameraId = QStringLiteral("front_cam");
    second.width = 4;
    second.height = 4;
    second.pixelFormat = AnalysisPixelFormat::Rgb;
    second.payload = QByteArray(4 * 4 * 3, '\x22');

    const SharedFramePublishResult secondPublish = secondWriter.publish(second);

    AnalysisFrameDescriptor secondDescriptor;
    secondDescriptor.frameId = second.frameId;
    secondDescriptor.timestampMs = second.timestampMs;
    secondDescriptor.cameraId = second.cameraId;
    secondDescriptor.width = second.width;
    secondDescriptor.height = second.height;
    secondDescriptor.pixelFormat = second.pixelFormat;
    secondDescriptor.slotIndex = secondPublish.slotIndex;
    secondDescriptor.sequence = secondPublish.sequence;
    secondDescriptor.payloadBytes = second.payload.size();

    AnalysisFramePacket decoded;
    QString error;
    QVERIFY(reader.read(secondDescriptor, &decoded, &error));
    QCOMPARE(error, QString());
    QCOMPARE(decoded.frameId, second.frameId);
    QCOMPARE(decoded.timestampMs, second.timestampMs);
    QCOMPARE(decoded.payload, second.payload);
}

QTEST_MAIN(SharedMemoryFrameReaderTest)
#include "shared_memory_frame_reader_test.moc"
