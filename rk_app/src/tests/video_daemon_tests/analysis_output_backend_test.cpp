#include "analysis/gstreamer_analysis_output_backend.h"
#include "protocol/analysis_frame_descriptor_protocol.h"

#include <QHostAddress>
#include <QLocalSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QTest>

class AnalysisOutputBackendTest : public QObject {
    Q_OBJECT

private slots:
    void resolvesAnalysisSocketFromEnvironment();
    void publishesDescriptorToLocalSocket();
};

void AnalysisOutputBackendTest::resolvesAnalysisSocketFromEnvironment() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", "/tmp/rk_video_analysis.sock");
    GstreamerAnalysisOutputBackend backend;
    QCOMPARE(backend.socketPath(), QStringLiteral("/tmp/rk_video_analysis.sock"));
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
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

QTEST_MAIN(AnalysisOutputBackendTest)
#include "analysis_output_backend_test.moc"
