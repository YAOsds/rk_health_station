#include "pipeline/dma_buffer_allocator.h"
#include "pipeline/gst_command_builder.h"
#include "pipeline/multipart_jpeg_parser.h"
#include "pipeline/preview_stream_reader.h"

#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <future>
#include <thread>
#include <unistd.h>

namespace {
QByteArray buildMultipartFrame(const QByteArray &boundary, const QByteArray &jpegBytes) {
    QByteArray payload;
    payload.append("--");
    payload.append(boundary);
    payload.append("\r\n");
    payload.append("Content-Type: image/jpeg\r\n");
    payload.append("Content-Length: ");
    payload.append(QByteArray::number(jpegBytes.size()));
    payload.append("\r\n\r\n");
    payload.append(jpegBytes);
    payload.append("\r\n");
    return payload;
}

QByteArray buildTextMultipartFrame(const QByteArray &boundary, const QByteArray &payloadBytes) {
    QByteArray payload;
    payload.append("--");
    payload.append(boundary);
    payload.append("\r\n");
    payload.append("Content-Type: text/plain\r\n");
    payload.append("Content-Length: ");
    payload.append(QByteArray::number(payloadBytes.size()));
    payload.append("\r\n\r\n");
    payload.append(payloadBytes);
    payload.append("\r\n");
    return payload;
}

QByteArray sampleJpegBytes() {
    return QByteArray::fromHex(
        "ffd8ffe000104a46494600010100000100010000ffdb0084000906071010101010100f0f100f0f0f0f0f0f"
        "0f0f0f0f0f0f0f1511161615111515181d2820181a251b151521312125292b2e2e2e171f3338332c37282d2e"
        "2b010a0a0a0e0d0e1a10101a2d1f1f252d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d"
        "2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2dffc00011080001000103012200021101031101ffc4001700"
        "00030100000000000000000000000000010203ffc40014100100000000000000000000000000000000ffda00"
        "0c03010002100310000001ea00ffc400191001000301010000000000000000000000010002111221ffda0008"
        "010100013f00a74697ffc400161101010100000000000000000000000000001101ffda0008010201013f015f"
        "ffc400161101010100000000000000000000000000001121ffda0008010301013f0157ffd9");
}
}

class GstreamerPipelineComponentsTest : public QObject {
    Q_OBJECT

private slots:
    void extractsSingleJpegFromMultipartPayload();
    void ignoresNonJpegMultipartChunk();
    void rejectsInvalidPreviewUrl();
    void readsJpegFrameFromTcpMultipartPreview();
    void buildsExternalPreviewCommandWithoutAnalysisTap();
    void buildsExternalPreviewCommandWithRgaAnalysisTap();
    void buildsPreviewStreamRecordingCommand();
    void quotesShellArgumentsWithSingleQuotes();
    void allocatesMemfdBufferWhenHeapPathIsMemfd();
};

void GstreamerPipelineComponentsTest::extractsSingleJpegFromMultipartPayload() {
    MultipartJpegParser parser;
    QByteArray streamBuffer = buildMultipartFrame(QByteArrayLiteral("rkpreview"), sampleJpegBytes());
    QByteArray jpegBytes;

    QVERIFY(parser.takeFrame(
        &streamBuffer, QByteArrayLiteral("--rkpreview"), &jpegBytes));
    QCOMPARE(jpegBytes, sampleJpegBytes());
    QVERIFY(streamBuffer.isEmpty());
}

void GstreamerPipelineComponentsTest::ignoresNonJpegMultipartChunk() {
    MultipartJpegParser parser;
    QByteArray streamBuffer = buildTextMultipartFrame(
        QByteArrayLiteral("rkpreview"), QByteArrayLiteral("not-a-jpeg"));
    QByteArray jpegBytes;

    QVERIFY(!parser.takeFrame(
        &streamBuffer, QByteArrayLiteral("--rkpreview"), &jpegBytes));
    QVERIFY(jpegBytes.isEmpty());
}

void GstreamerPipelineComponentsTest::rejectsInvalidPreviewUrl() {
    PreviewStreamReader reader;
    PreviewStreamReader::PreviewStreamConfig config;
    QString error;

    QVERIFY(!reader.parsePreviewUrl(
        QStringLiteral("http://127.0.0.1/not-preview"), &config, &error));
    QCOMPARE(error, QStringLiteral("invalid_preview_url"));
}

void GstreamerPipelineComponentsTest::readsJpegFrameFromTcpMultipartPreview() {
    const QByteArray jpegBytes = sampleJpegBytes();
    const QByteArray multipartPayload = buildMultipartFrame(QByteArrayLiteral("rkpreview"), jpegBytes);

    std::promise<quint16> portPromise;
    std::future<quint16> portFuture = portPromise.get_future();
    std::thread serverThread([payload = multipartPayload, port = std::move(portPromise)]() mutable {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            port.set_value(0);
            return;
        }
        port.set_value(server.serverPort());
        if (!server.waitForNewConnection(5000)) {
            return;
        }
        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket) {
            return;
        }
        socket->write(payload);
        socket->waitForBytesWritten(5000);
        socket->disconnectFromHost();
        delete socket;
    });

    const quint16 port = portFuture.get();
    QVERIFY(port > 0);

    PreviewStreamReader reader;
    QByteArray capturedJpeg;
    QString error;
    QVERIFY(reader.readJpegFrame(
        QStringLiteral("tcp://127.0.0.1:%1?transport=tcp_mjpeg&boundary=rkpreview").arg(port),
        &capturedJpeg,
        &error));
    QCOMPARE(capturedJpeg, jpegBytes);
    QVERIFY(error.isEmpty());

    serverThread.join();
}

void GstreamerPipelineComponentsTest::buildsExternalPreviewCommandWithoutAnalysisTap() {
    AppRuntimeConfig config;
    GstCommandBuilder builder(config);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    const QString command = builder.buildPreviewCommand(status, false);
    QVERIFY(command.contains(QStringLiteral("v4l2src device='/dev/video11'")));
    QVERIFY(command.contains(QStringLiteral("video/x-raw,format=NV12,width=640,height=480,framerate=30/1")));
    QVERIFY(command.contains(QStringLiteral("mppjpegenc rc-mode=fixqp q-factor=95")));
    QVERIFY(command.contains(QStringLiteral("multipartmux boundary=rkpreview")));
    QVERIFY(command.contains(QStringLiteral("tcpserversink host=127.0.0.1 port=5602")));
    QVERIFY(!command.contains(QStringLiteral("fdsink fd=1")));
}

void GstreamerPipelineComponentsTest::buildsExternalPreviewCommandWithRgaAnalysisTap() {
    AppRuntimeConfig config;
    config.video.analysisConvertBackend = QStringLiteral("rga");
    GstCommandBuilder builder(config);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    const QString command = builder.buildPreviewCommand(status, true);
    QVERIFY(command.contains(QStringLiteral("tee name=t")));
    QVERIFY(command.contains(QStringLiteral("videorate drop-only=true")));
    QVERIFY(command.contains(QStringLiteral("video/x-raw,format=NV12,width=640,height=480,framerate=15/1")));
    QVERIFY(command.contains(QStringLiteral("fdsink fd=1 sync=false")));
}

void GstreamerPipelineComponentsTest::buildsPreviewStreamRecordingCommand() {
    AppRuntimeConfig config;
    GstCommandBuilder builder(config);
    QString error;

    const QString command = builder.buildPreviewStreamRecordingCommand(
        QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview"),
        QStringLiteral("/tmp/out.mp4"),
        &error);

    QVERIFY(error.isEmpty());
    QVERIFY(command.contains(QStringLiteral("tcpclientsrc host='127.0.0.1' port=5602")));
    QVERIFY(command.contains(QStringLiteral("\"multipart/x-mixed-replace,boundary=rkpreview\"")));
    QVERIFY(command.contains(QStringLiteral("multipartdemux single-stream=true")));
    QVERIFY(command.contains(QStringLiteral("filesink location='/tmp/out.mp4'")));
}

void GstreamerPipelineComponentsTest::quotesShellArgumentsWithSingleQuotes() {
    AppRuntimeConfig config;
    config.video.gstLaunchBin = QStringLiteral("/tmp/it's-gst");
    GstCommandBuilder builder(config);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.snapshotProfile.width = 1920;
    status.snapshotProfile.height = 1080;
    status.snapshotProfile.pixelFormat = QStringLiteral("NV12");

    const QString command = builder.buildSnapshotCommand(
        status, QStringLiteral("/tmp/it's-output.jpg"));
    QVERIFY(command.contains(QStringLiteral("'/tmp/it'\\''s-gst'")));
    QVERIFY(command.contains(QStringLiteral("'/tmp/it'\\''s-output.jpg'")));
}

void GstreamerPipelineComponentsTest::allocatesMemfdBufferWhenHeapPathIsMemfd() {
    DmaBufferAllocator allocator;
    QString error;

    const int fd = allocator.allocate(QStringLiteral("memfd"), 4096, &error);
    QVERIFY2(fd >= 0, qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(::close(fd) == 0);
}

QTEST_MAIN(GstreamerPipelineComponentsTest)
#include "gstreamer_pipeline_components_test.moc"
