#include "analysis/shared_memory_frame_ring.h"
#include "ingest/shared_memory_frame_reader.h"

#include <QtTest/QTest>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

class SharedMemoryFrameReaderTest : public QObject {
    Q_OBJECT

private slots:
    void rejectsDescriptorForOverwrittenSlot();
    void rejectsDmaBufDescriptorBeforeOpeningSharedMemory();
    void clearsPacketTransportMetadataForSharedMemoryRead();
    void remapsWhenRingIsRecreatedForSameCamera();
    void rejectsRingHeaderThatExceedsMappedSize();
    void rejectsMappingSmallerThanRingHeader();
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

void SharedMemoryFrameReaderTest::rejectsDmaBufDescriptorBeforeOpeningSharedMemory() {
    SharedMemoryFrameReader reader(QStringLiteral("/rk_missing_for_dmabuf_test"));

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 1;
    descriptor.timestampMs = 10;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 4;
    descriptor.height = 4;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.slotIndex = 0;
    descriptor.sequence = 2;
    descriptor.payloadBytes = 4 * 4 * 3;
    descriptor.payloadTransport = AnalysisPayloadTransport::DmaBuf;
    descriptor.dmaBufPlaneCount = 1;
    descriptor.dmaBufOffset = 0;
    descriptor.dmaBufStrideBytes = 4 * 3;

    AnalysisFramePacket decoded;
    QString error;
    QVERIFY(!reader.read(descriptor, &decoded, &error));
    QCOMPARE(error, QStringLiteral("analysis_payload_transport_unsupported"));
}

void SharedMemoryFrameReaderTest::clearsPacketTransportMetadataForSharedMemoryRead() {
    SharedMemoryFrameRingWriter writer(QStringLiteral("front_cam"), 1, 4 * 4 * 3);
    QVERIFY(writer.initialize());

    AnalysisFramePacket frame;
    frame.frameId = 3;
    frame.timestampMs = 30;
    frame.cameraId = QStringLiteral("front_cam");
    frame.width = 4;
    frame.height = 4;
    frame.pixelFormat = AnalysisPixelFormat::Rgb;
    frame.payload = QByteArray(4 * 4 * 3, '\x33');

    const SharedFramePublishResult publish = writer.publish(frame);

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = frame.frameId;
    descriptor.timestampMs = frame.timestampMs;
    descriptor.cameraId = frame.cameraId;
    descriptor.width = frame.width;
    descriptor.height = frame.height;
    descriptor.pixelFormat = frame.pixelFormat;
    descriptor.slotIndex = publish.slotIndex;
    descriptor.sequence = publish.sequence;
    descriptor.payloadBytes = frame.payload.size();

    AnalysisFramePacket decoded;
    decoded.payloadTransport = AnalysisPayloadTransport::DmaBuf;
    decoded.dmaBufPlaneCount = 1;
    decoded.dmaBufOffset = 8;
    decoded.dmaBufStrideBytes = 128;

    SharedMemoryFrameReader reader;
    QString error;
    QVERIFY(reader.read(descriptor, &decoded, &error));
    QCOMPARE(decoded.payloadTransport, AnalysisPayloadTransport::SharedMemory);
    QCOMPARE(decoded.dmaBufPlaneCount, 0u);
    QCOMPARE(decoded.dmaBufOffset, 0u);
    QCOMPARE(decoded.dmaBufStrideBytes, 0u);
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

void SharedMemoryFrameReaderTest::rejectsMappingSmallerThanRingHeader() {
    const QString shmName = QStringLiteral("/rk_reader_short_header_test");
    ::shm_unlink(shmName.toUtf8().constData());
    const int fd = ::shm_open(shmName.toUtf8().constData(), O_CREAT | O_RDWR, 0600);
    QVERIFY(fd >= 0);
    QVERIFY(::ftruncate(fd, 1) == 0);
    ::close(fd);

    SharedMemoryFrameReader reader(shmName);
    AnalysisFrameDescriptor descriptor;
    descriptor.cameraId = QStringLiteral("front_cam");

    AnalysisFramePacket decoded;
    QString error;
    QVERIFY(!reader.read(descriptor, &decoded, &error));
    QCOMPARE(error, QStringLiteral("analysis_ring_invalid_header"));
    ::shm_unlink(shmName.toUtf8().constData());
}

void SharedMemoryFrameReaderTest::rejectsRingHeaderThatExceedsMappedSize() {
    const QString shmName = QStringLiteral("/rk_reader_bad_header_test");
    ::shm_unlink(shmName.toUtf8().constData());
    const int fd = ::shm_open(shmName.toUtf8().constData(), O_CREAT | O_RDWR, 0600);
    QVERIFY(fd >= 0);
    QVERIFY(::ftruncate(fd, sizeof(SharedFrameRingHeader)) == 0);

    void *mapped = ::mmap(nullptr, sizeof(SharedFrameRingHeader), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    QVERIFY(mapped != MAP_FAILED);
    auto *header = static_cast<SharedFrameRingHeader *>(mapped);
    *header = SharedFrameRingHeader();
    header->slotCount = 1;
    header->slotStride = static_cast<quint32>(sizeof(SharedFrameSlotHeader) + 16);
    header->maxFrameBytes = 16;
    ::munmap(mapped, sizeof(SharedFrameRingHeader));
    ::close(fd);

    SharedMemoryFrameReader reader(shmName);
    AnalysisFrameDescriptor descriptor;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.slotIndex = 1;

    AnalysisFramePacket decoded;
    QString error;
    QVERIFY(!reader.read(descriptor, &decoded, &error));
    QCOMPARE(error, QStringLiteral("analysis_ring_invalid_header"));
    ::shm_unlink(shmName.toUtf8().constData());
}

QTEST_MAIN(SharedMemoryFrameReaderTest)
#include "shared_memory_frame_reader_test.moc"
