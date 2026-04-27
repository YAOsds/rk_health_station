#include "ingest/dmabuf_frame_reader.h"

#include <QtTest/QTest>

#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
int createMemFd(const char *name) {
#ifdef SYS_memfd_create
    return static_cast<int>(::syscall(SYS_memfd_create, name, MFD_CLOEXEC));
#else
    Q_UNUSED(name);
    return -1;
#endif
}
}

class DmaBufFrameReaderTest : public QObject {
    Q_OBJECT

private slots:
    void mapsRgbPayloadFromPassedFd();
    void rejectsSharedMemoryDescriptors();
};

void DmaBufFrameReaderTest::mapsRgbPayloadFromPassedFd() {
    const int payloadBytes = 4 * 3 * 3;
    const int fd = createMemFd("rk_dmabuf_reader_test");
    QVERIFY(fd >= 0);
    QVERIFY(::ftruncate(fd, payloadBytes) == 0);
    void *mapped = ::mmap(nullptr, payloadBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    QVERIFY(mapped != MAP_FAILED);
    memset(mapped, 0x42, payloadBytes);
    ::munmap(mapped, payloadBytes);

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 71;
    descriptor.timestampMs = 1777000000710;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 4;
    descriptor.height = 3;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.payloadTransport = AnalysisPayloadTransport::DmaBuf;
    descriptor.dmaBufPlaneCount = 1;
    descriptor.dmaBufOffset = 0;
    descriptor.dmaBufStrideBytes = 4 * 3;
    descriptor.payloadBytes = payloadBytes;

    DmaBufFrameReader reader;
    AnalysisFramePacket packet;
    QString error;
    QVERIFY(reader.read(descriptor, fd, &packet, &error));
    QCOMPARE(error, QString());
    QCOMPARE(packet.payloadTransport, AnalysisPayloadTransport::DmaBuf);
    QCOMPARE(packet.frameId, descriptor.frameId);
    QCOMPARE(packet.pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(packet.dmaBufPlaneCount, 1u);
    QCOMPARE(packet.dmaBufStrideBytes, 12u);
    QCOMPARE(packet.payload, QByteArray(payloadBytes, '\x42'));
    QVERIFY(packet.dmaBufPayload);

    ::close(fd);
}

void DmaBufFrameReaderTest::rejectsSharedMemoryDescriptors() {
    DmaBufFrameReader reader;
    AnalysisFrameDescriptor descriptor;
    descriptor.payloadTransport = AnalysisPayloadTransport::SharedMemory;

    AnalysisFramePacket packet;
    QString error;
    QVERIFY(!reader.read(descriptor, -1, &packet, &error));
    QCOMPARE(error, QStringLiteral("analysis_payload_transport_not_dmabuf"));
}

QTEST_MAIN(DmaBufFrameReaderTest)
#include "dmabuf_frame_reader_test.moc"
