#include "analysis/shared_memory_frame_ring.h"

#include <QtTest/QTest>

class SharedMemoryFrameRingTest : public QObject {
    Q_OBJECT

private slots:
    void writesRgbFrameIntoNextSlot();
};

void SharedMemoryFrameRingTest::writesRgbFrameIntoNextSlot() {
    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 4, 640 * 640 * 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket frame;
    frame.frameId = 101;
    frame.timestampMs = 1777000001111;
    frame.cameraId = QStringLiteral("front_cam");
    frame.width = 640;
    frame.height = 640;
    frame.pixelFormat = AnalysisPixelFormat::Rgb;
    frame.payload = QByteArray(640 * 640 * 3, '\x5a');

    const SharedFramePublishResult result = writer.publish(frame);
    QCOMPARE(result.slotIndex, 0u);
    QCOMPARE(result.sequence, 1u);
}

QTEST_MAIN(SharedMemoryFrameRingTest)
#include "shared_memory_frame_ring_test.moc"
