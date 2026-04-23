#include "analysis/shared_memory_frame_ring.h"
#include "ingest/shared_memory_frame_reader.h"

#include <QtTest/QTest>

class SharedMemoryFrameReaderTest : public QObject {
    Q_OBJECT

private slots:
    void rejectsDescriptorForOverwrittenSlot();
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

QTEST_MAIN(SharedMemoryFrameReaderTest)
#include "shared_memory_frame_reader_test.moc"
