#include "analysis/gstreamer_analysis_output_backend.h"
#include "protocol/analysis_frame_descriptor_protocol.h"
#include "protocol/unix_fd_passing.h"
#include "runtime_config/app_runtime_config.h"

#include <QHostAddress>
#include <QLocalSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QTest>

#include <fcntl.h>
#include <linux/memfd.h>
#include <poll.h>
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

class AnalysisOutputBackendTest : public QObject {
    Q_OBJECT

private slots:
    void resolvesAnalysisSocketFromEnvironment();
    void resolvesAnalysisSocketAndDmabufTransportFromRuntimeConfig();
    void publishesDescriptorToLocalSocket();
    void publishesDmaBufDescriptorAndFd();
};

void AnalysisOutputBackendTest::resolvesAnalysisSocketFromEnvironment() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", "/tmp/rk_video_analysis.sock");
    GstreamerAnalysisOutputBackend backend;
    QCOMPARE(backend.socketPath(), QStringLiteral("/tmp/rk_video_analysis.sock"));
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}

void AnalysisOutputBackendTest::resolvesAnalysisSocketAndDmabufTransportFromRuntimeConfig() {
    AppRuntimeConfig config;
    config.ipc.analysisSocketPath = QStringLiteral("/tmp/rk_video_analysis_config_test.sock");
    config.analysis.transport = QStringLiteral("dmabuf");

    GstreamerAnalysisOutputBackend backend(config);
    QCOMPARE(backend.socketPath(), QStringLiteral("/tmp/rk_video_analysis_config_test.sock"));

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.previewProfile.fps = 20;

    QString error;
    QVERIFY(backend.start(status, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(backend.supportsDmaBufFrames());
    backend.stop(QStringLiteral("front_cam"), &error);
}

void AnalysisOutputBackendTest::publishesDescriptorToLocalSocket() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_backend_test.sock"));

    GstreamerAnalysisOutputBackend backend;
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 20;

    QString error;
    QVERIFY(backend.start(status, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(backend.acceptsFrames(QStringLiteral("front_cam")));

    const AnalysisChannelStatus startedStatus = backend.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(startedStatus.outputFormat, QStringLiteral("rgb"));
    QCOMPARE(startedStatus.width, 640);
    QCOMPARE(startedStatus.height, 640);

    QLocalSocket client;
    client.connectToServer(QStringLiteral("/tmp/rk_video_analysis_backend_test.sock"));
    QVERIFY(client.waitForConnected(2000));
    QTRY_VERIFY_WITH_TIMEOUT(
        backend.statusForCamera(QStringLiteral("front_cam")).streamConnected, 2000);

    AnalysisFrameDescriptor pushed;
    pushed.frameId = 5;
    pushed.timestampMs = 1234;
    pushed.cameraId = QStringLiteral("front_cam");
    pushed.width = 640;
    pushed.height = 640;
    pushed.pixelFormat = AnalysisPixelFormat::Rgb;
    pushed.slotIndex = 1;
    pushed.sequence = 4;
    pushed.payloadBytes = 640 * 640 * 3;
    const QByteArray expectedEncoded = encodeAnalysisFrameDescriptor(pushed);
    backend.publishDescriptor(pushed);

    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() >= expectedEncoded.size(), 2000);
    AnalysisFrameDescriptor descriptor;
    QVERIFY(decodeAnalysisFrameDescriptor(client.readAll(), &descriptor));
    QCOMPARE(descriptor.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(descriptor.pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(descriptor.slotIndex, pushed.slotIndex);
    QCOMPARE(descriptor.sequence, pushed.sequence);

    const AnalysisChannelStatus analysisStatus = backend.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(analysisStatus.outputFormat, QStringLiteral("rgb"));
    QCOMPARE(analysisStatus.width, 640);
    QCOMPARE(analysisStatus.height, 640);

    backend.stop(QStringLiteral("front_cam"), &error);
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}


void AnalysisOutputBackendTest::publishesDmaBufDescriptorAndFd() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_backend_dmabuf_test.sock"));
    qputenv("RK_VIDEO_ANALYSIS_TRANSPORT", "dmabuf");

    GstreamerAnalysisOutputBackend backend;
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.previewProfile.fps = 15;

    QString error;
    QVERIFY(backend.start(status, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(backend.supportsDmaBufFrames());

    const int fdClient = connectUnixStreamSocket(
        QStringLiteral("/tmp/rk_video_analysis_backend_dmabuf_test.sock.fd"), &error);
    QVERIFY2(fdClient >= 0, qPrintable(error));

    QLocalSocket client;
    client.connectToServer(QStringLiteral("/tmp/rk_video_analysis_backend_dmabuf_test.sock"));
    QVERIFY(client.waitForConnected(2000));
    QTRY_VERIFY_WITH_TIMEOUT(
        backend.statusForCamera(QStringLiteral("front_cam")).streamConnected, 2000);

    const int fd = createMemFd("rk_analysis_backend_dmabuf_test");
    QVERIFY(fd >= 0);
    QVERIFY(::ftruncate(fd, 36) == 0);

    AnalysisFrameDescriptor descriptor;
    descriptor.frameId = 12;
    descriptor.timestampMs = 12345;
    descriptor.cameraId = QStringLiteral("front_cam");
    descriptor.width = 4;
    descriptor.height = 3;
    descriptor.pixelFormat = AnalysisPixelFormat::Rgb;
    descriptor.sequence = 12;
    descriptor.payloadBytes = 36;
    descriptor.dmaBufPlaneCount = 1;
    descriptor.dmaBufOffset = 0;
    descriptor.dmaBufStrideBytes = 12;
    backend.publishDmaBufDescriptor(descriptor, fd);

    pollfd pollDescriptor{};
    pollDescriptor.fd = fdClient;
    pollDescriptor.events = POLLIN;
    QVERIFY(::poll(&pollDescriptor, 1, 2000) > 0);
    QString fdError;
    const int receivedFd = receiveFileDescriptor(fdClient, &fdError);
    QVERIFY2(receivedFd >= 0, qPrintable(fdError));
    ::close(receivedFd);

    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() > 0, 2000);
    AnalysisFrameDescriptor decoded;
    QVERIFY(decodeAnalysisFrameDescriptor(client.readAll(), &decoded));
    QCOMPARE(decoded.payloadTransport, AnalysisPayloadTransport::DmaBuf);
    QCOMPARE(decoded.frameId, descriptor.frameId);
    QCOMPARE(decoded.dmaBufStrideBytes, 12u);

    ::close(fd);
    ::close(fdClient);
    backend.stop(QStringLiteral("front_cam"), &error);
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
    qunsetenv("RK_VIDEO_ANALYSIS_TRANSPORT");
}

QTEST_MAIN(AnalysisOutputBackendTest)
#include "analysis_output_backend_test.moc"
