#include "pipeline/dma_buffer_allocator.h"
#include "pipeline/gst_command_builder.h"
#include "pipeline/multipart_jpeg_parser.h"
#include "pipeline/preview_stream_reader.h"
#include "pipeline/pipeline_session.h"
#include "pipeline/analysis_frame_publisher.h"
#include "analysis/analysis_frame_source.h"
#include "analysis/shared_memory_frame_ring.h"

#include <fcntl.h>
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

class RecordingAnalysisFrameSource : public AnalysisFrameSource {
public:
    bool acceptsFrames(const QString &cameraId) const override {
        return enabled && cameraId == QStringLiteral("front_cam");
    }

    bool supportsDmaBufFrames() const override {
        return dmabufSupported;
    }

    void publishDescriptor(const AnalysisFrameDescriptor &descriptor) override {
        descriptors.append(descriptor);
    }

    void publishDmaBufDescriptor(const AnalysisFrameDescriptor &descriptor, int fd) override {
        dmabufDescriptors.append(descriptor);
        lastDmaBufFdWasValid = ::fcntl(fd, F_GETFD) >= 0;
    }

    bool enabled = true;
    bool dmabufSupported = false;
    bool lastDmaBufFdWasValid = false;
    QVector<AnalysisFrameDescriptor> descriptors;
    QVector<AnalysisFrameDescriptor> dmabufDescriptors;
};

class FillingAnalysisFrameConverter : public AnalysisFrameConverter {
public:
    bool convertNv12ToRgb(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        QByteArray *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override {
        Q_UNUSED(error);
        calls++;
        lastInputBytes = nv12.size();
        lastSrcWidth = srcWidth;
        lastSrcHeight = srcHeight;
        lastDstWidth = dstWidth;
        lastDstHeight = dstHeight;
        rgb->fill('\x7f', dstWidth * dstHeight * 3);
        if (metadata) {
            metadata->posePreprocessed = true;
            metadata->poseXPad = 0;
            metadata->poseYPad = 80;
            metadata->poseScale = 1.0f;
        }
        return true;
    }

    int calls = 0;
    int lastInputBytes = 0;
    int lastSrcWidth = 0;
    int lastSrcHeight = 0;
    int lastDstWidth = 0;
    int lastDstHeight = 0;
};

class DmaOutputAnalysisFrameConverter : public AnalysisFrameConverter {
public:
    bool convertNv12ToRgb(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        QByteArray *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override {
        Q_UNUSED(nv12);
        Q_UNUSED(srcWidth);
        Q_UNUSED(srcHeight);
        Q_UNUSED(dstWidth);
        Q_UNUSED(dstHeight);
        Q_UNUSED(metadata);
        Q_UNUSED(error);
        byteArrayCalls++;
        rgb->fill('\x3f', dstWidth * dstHeight * 3);
        return true;
    }

    bool convertNv12ToRgbDma(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override {
        Q_UNUSED(error);
        dmaCalls++;
        lastInputBytes = nv12.size();
        lastSrcWidth = srcWidth;
        lastSrcHeight = srcHeight;
        lastDstWidth = dstWidth;
        lastDstHeight = dstHeight;
        rgb->fd = ::open("/dev/zero", O_RDONLY | O_CLOEXEC);
        rgb->payloadBytes = static_cast<quint32>(dstWidth * dstHeight * 3);
        rgb->offset = 0;
        rgb->strideBytes = static_cast<quint32>(dstWidth * 3);
        if (metadata) {
            metadata->posePreprocessed = true;
            metadata->poseXPad = 0;
            metadata->poseYPad = 80;
            metadata->poseScale = 1.0f;
        }
        return rgb->fd >= 0;
    }

    int byteArrayCalls = 0;
    int dmaCalls = 0;
    int lastInputBytes = 0;
    int lastSrcWidth = 0;
    int lastSrcHeight = 0;
    int lastDstWidth = 0;
    int lastDstHeight = 0;
};

class DmaInputOutputAnalysisFrameConverter : public AnalysisFrameConverter {
public:
    bool convertNv12ToRgb(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        QByteArray *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override {
        Q_UNUSED(nv12);
        Q_UNUSED(srcWidth);
        Q_UNUSED(srcHeight);
        Q_UNUSED(dstWidth);
        Q_UNUSED(dstHeight);
        Q_UNUSED(metadata);
        Q_UNUSED(error);
        byteArrayCalls++;
        rgb->fill('\x2a', dstWidth * dstHeight * 3);
        return true;
    }

    bool convertNv12DmaToRgbDma(const AnalysisDmaBuffer &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override {
        Q_UNUSED(error);
        dmaInputCalls++;
        lastInputFd = nv12.fd;
        lastInputBytes = static_cast<int>(nv12.payloadBytes);
        lastInputStride = static_cast<int>(nv12.strideBytes);
        lastSrcWidth = srcWidth;
        lastSrcHeight = srcHeight;
        lastDstWidth = dstWidth;
        lastDstHeight = dstHeight;
        rgb->fd = ::open("/dev/zero", O_RDONLY | O_CLOEXEC);
        rgb->payloadBytes = static_cast<quint32>(dstWidth * dstHeight * 3);
        rgb->offset = 0;
        rgb->strideBytes = static_cast<quint32>(dstWidth * 3);
        if (metadata) {
            metadata->posePreprocessed = true;
            metadata->poseXPad = 0;
            metadata->poseYPad = 80;
            metadata->poseScale = 1.0f;
        }
        return rgb->fd >= 0;
    }

    int byteArrayCalls = 0;
    int dmaInputCalls = 0;
    int lastInputFd = -1;
    int lastInputBytes = 0;
    int lastInputStride = 0;
    int lastSrcWidth = 0;
    int lastSrcHeight = 0;
    int lastDstWidth = 0;
    int lastDstHeight = 0;
};

class FailingDmaInputOutputAnalysisFrameConverter : public AnalysisFrameConverter {
public:
    bool convertNv12ToRgb(const QByteArray &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        QByteArray *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override {
        Q_UNUSED(nv12);
        Q_UNUSED(srcWidth);
        Q_UNUSED(srcHeight);
        Q_UNUSED(dstWidth);
        Q_UNUSED(dstHeight);
        Q_UNUSED(rgb);
        Q_UNUSED(metadata);
        Q_UNUSED(error);
        byteArrayCalls++;
        return true;
    }

    bool convertNv12DmaToRgbDma(const AnalysisDmaBuffer &nv12,
        int srcWidth,
        int srcHeight,
        int dstWidth,
        int dstHeight,
        AnalysisDmaBuffer *rgb,
        AnalysisFrameConversionMetadata *metadata,
        QString *error) override {
        Q_UNUSED(nv12);
        Q_UNUSED(srcWidth);
        Q_UNUSED(srcHeight);
        Q_UNUSED(dstWidth);
        Q_UNUSED(dstHeight);
        Q_UNUSED(rgb);
        Q_UNUSED(metadata);
        dmaInputCalls++;
        if (error) {
            *error = QStringLiteral("synthetic_dma_failure");
        }
        return false;
    }

    int byteArrayCalls = 0;
    int dmaInputCalls = 0;
};

PipelineSession buildAnalysisSession() {
    PipelineSession session;
    session.cameraId = QStringLiteral("front_cam");
    session.analysisConvertBackend = AnalysisConvertBackend::Rga;
    session.analysisInputFormat = AnalysisFrameInputFormat::Nv12;
    session.analysisInputWidth = 640;
    session.analysisInputHeight = 480;
    session.analysisInputFrameBytes = 640 * 480 * 3 / 2;
    session.analysisOutputWidth = 640;
    session.analysisOutputHeight = 640;
    session.analysisOutputFrameBytes = 640 * 640 * 3;
    session.frameRing = new SharedMemoryFrameRingWriter(
        session.cameraId, 32, static_cast<quint32>(session.analysisOutputFrameBytes));
    QString error;
    if (!session.frameRing->initialize(&error)) {
        qFatal("failed to initialize ring: %s", qPrintable(error));
    }
    return session;
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
    void publishesSharedMemoryDescriptorForRgbFrames();
    void convertsRgaNv12FramesToRgbDescriptors();
    void publishesRgaDmaOutputWhenRequested();
    void publishesRgaDmaInputOutputWhenProvided();
    void rejectsRgaDmaInputOutputWhenConverterFails();
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

void GstreamerPipelineComponentsTest::publishesSharedMemoryDescriptorForRgbFrames() {
    AppRuntimeConfig config;
    DmaBufferAllocator allocator;
    RecordingAnalysisFrameSource analysisSource;
    AnalysisFramePublisher publisher(config, &allocator);
    publisher.setFrameSource(&analysisSource);

    PipelineSession session = buildAnalysisSession();
    session.analysisConvertBackend = AnalysisConvertBackend::GstreamerCpu;

    publisher.publishFrameBytes(&session, QByteArray(session.analysisOutputFrameBytes, '\0'));

    QCOMPARE(analysisSource.descriptors.size(), 1);
    QCOMPARE(analysisSource.dmabufDescriptors.size(), 0);
    const AnalysisFrameDescriptor descriptor = analysisSource.descriptors.first();
    QCOMPARE(descriptor.payloadTransport, AnalysisPayloadTransport::SharedMemory);
    QCOMPARE(descriptor.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(descriptor.width, 640);
    QCOMPARE(descriptor.height, 640);
    QCOMPARE(descriptor.pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(descriptor.payloadBytes, 640u * 640u * 3u);
    QVERIFY(descriptor.sequence > 0);

    delete session.frameRing;
    session.frameRing = nullptr;
}

void GstreamerPipelineComponentsTest::convertsRgaNv12FramesToRgbDescriptors() {
    AppRuntimeConfig config;
    DmaBufferAllocator allocator;
    RecordingAnalysisFrameSource analysisSource;
    FillingAnalysisFrameConverter converter;
    AnalysisFramePublisher publisher(config, &allocator);
    publisher.setFrameSource(&analysisSource);
    publisher.setFrameConverter(&converter);

    PipelineSession session = buildAnalysisSession();
    publisher.publishFrameBytes(&session, QByteArray(session.analysisInputFrameBytes, '\0'));

    QCOMPARE(converter.calls, 1);
    QCOMPARE(analysisSource.descriptors.size(), 1);
    QCOMPARE(converter.lastInputBytes, 640 * 480 * 3 / 2);
    QCOMPARE(converter.lastSrcWidth, 640);
    QCOMPARE(converter.lastSrcHeight, 480);
    QCOMPARE(converter.lastDstWidth, 640);
    QCOMPARE(converter.lastDstHeight, 640);
    const AnalysisFrameDescriptor descriptor = analysisSource.descriptors.first();
    QCOMPARE(descriptor.width, 640);
    QCOMPARE(descriptor.height, 640);
    QCOMPARE(descriptor.pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(descriptor.payloadBytes, 640u * 640u * 3u);
    QVERIFY(descriptor.posePreprocessed);
    QCOMPARE(descriptor.poseXPad, 0);
    QCOMPARE(descriptor.poseYPad, 80);
    QCOMPARE(descriptor.poseScale, 1.0f);

    delete session.frameRing;
    session.frameRing = nullptr;
}

void GstreamerPipelineComponentsTest::publishesRgaDmaOutputWhenRequested() {
    AppRuntimeConfig config;
    config.analysis.rgaOutputDmabuf = true;
    DmaBufferAllocator allocator;
    RecordingAnalysisFrameSource analysisSource;
    analysisSource.dmabufSupported = true;
    DmaOutputAnalysisFrameConverter converter;
    AnalysisFramePublisher publisher(config, &allocator);
    publisher.setFrameSource(&analysisSource);
    publisher.setFrameConverter(&converter);

    PipelineSession session = buildAnalysisSession();
    publisher.publishFrameBytes(&session, QByteArray(session.analysisInputFrameBytes, '\0'));

    QCOMPARE(analysisSource.dmabufDescriptors.size(), 1);
    QCOMPARE(analysisSource.descriptors.size(), 0);
    QCOMPARE(converter.dmaCalls, 1);
    QCOMPARE(converter.byteArrayCalls, 0);
    QCOMPARE(converter.lastInputBytes, 640 * 480 * 3 / 2);
    QCOMPARE(converter.lastSrcWidth, 640);
    QCOMPARE(converter.lastSrcHeight, 480);
    QCOMPARE(converter.lastDstWidth, 640);
    QCOMPARE(converter.lastDstHeight, 640);
    QVERIFY(analysisSource.lastDmaBufFdWasValid);
    const AnalysisFrameDescriptor descriptor = analysisSource.dmabufDescriptors.first();
    QCOMPARE(descriptor.payloadTransport, AnalysisPayloadTransport::DmaBuf);
    QCOMPARE(descriptor.width, 640);
    QCOMPARE(descriptor.height, 640);
    QCOMPARE(descriptor.pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(descriptor.payloadBytes, 640u * 640u * 3u);
    QCOMPARE(descriptor.dmaBufPlaneCount, 1u);
    QCOMPARE(descriptor.dmaBufOffset, 0u);
    QCOMPARE(descriptor.dmaBufStrideBytes, 640u * 3u);
    QVERIFY(descriptor.posePreprocessed);
    QCOMPARE(descriptor.poseXPad, 0);
    QCOMPARE(descriptor.poseYPad, 80);
    QCOMPARE(descriptor.poseScale, 1.0f);

    delete session.frameRing;
    session.frameRing = nullptr;
}

void GstreamerPipelineComponentsTest::publishesRgaDmaInputOutputWhenProvided() {
    AppRuntimeConfig config;
    config.analysis.rgaOutputDmabuf = true;
    DmaBufferAllocator allocator;
    RecordingAnalysisFrameSource analysisSource;
    analysisSource.dmabufSupported = true;
    DmaInputOutputAnalysisFrameConverter converter;
    AnalysisFramePublisher publisher(config, &allocator);
    publisher.setFrameSource(&analysisSource);
    publisher.setFrameConverter(&converter);

    PipelineSession session = buildAnalysisSession();
    AnalysisDmaBuffer input;
    input.fd = ::open("/dev/zero", O_RDONLY | O_CLOEXEC);
    QVERIFY(input.fd >= 0);
    input.payloadBytes = 640u * 480u * 3u / 2u;
    input.offset = 0;
    input.strideBytes = 640;

    QVERIFY(publisher.publishFrameDma(&session, input));
    QVERIFY(::close(input.fd) == 0);
    QCOMPARE(analysisSource.dmabufDescriptors.size(), 1);
    QCOMPARE(analysisSource.descriptors.size(), 0);
    QCOMPARE(converter.dmaInputCalls, 1);
    QCOMPARE(converter.byteArrayCalls, 0);
    QCOMPARE(converter.lastInputBytes, 640 * 480 * 3 / 2);
    QCOMPARE(converter.lastInputStride, 640);
    QCOMPARE(converter.lastSrcWidth, 640);
    QCOMPARE(converter.lastSrcHeight, 480);
    QCOMPARE(converter.lastDstWidth, 640);
    QCOMPARE(converter.lastDstHeight, 640);
    QVERIFY(analysisSource.lastDmaBufFdWasValid);
    const AnalysisFrameDescriptor descriptor = analysisSource.dmabufDescriptors.first();
    QCOMPARE(descriptor.payloadTransport, AnalysisPayloadTransport::DmaBuf);
    QCOMPARE(descriptor.width, 640);
    QCOMPARE(descriptor.height, 640);
    QCOMPARE(descriptor.pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(descriptor.payloadBytes, 640u * 640u * 3u);
    QCOMPARE(descriptor.dmaBufPlaneCount, 1u);
    QCOMPARE(descriptor.dmaBufOffset, 0u);
    QCOMPARE(descriptor.dmaBufStrideBytes, 640u * 3u);
    QVERIFY(descriptor.posePreprocessed);
    QCOMPARE(descriptor.poseXPad, 0);
    QCOMPARE(descriptor.poseYPad, 80);
    QCOMPARE(descriptor.poseScale, 1.0f);

    delete session.frameRing;
    session.frameRing = nullptr;
}

void GstreamerPipelineComponentsTest::rejectsRgaDmaInputOutputWhenConverterFails() {
    AppRuntimeConfig config;
    config.analysis.rgaOutputDmabuf = true;
    DmaBufferAllocator allocator;
    RecordingAnalysisFrameSource analysisSource;
    analysisSource.dmabufSupported = true;
    FailingDmaInputOutputAnalysisFrameConverter converter;
    AnalysisFramePublisher publisher(config, &allocator);
    publisher.setFrameSource(&analysisSource);
    publisher.setFrameConverter(&converter);

    PipelineSession session = buildAnalysisSession();
    AnalysisDmaBuffer input;
    input.fd = ::open("/dev/zero", O_RDONLY | O_CLOEXEC);
    QVERIFY(input.fd >= 0);
    input.payloadBytes = 640u * 480u * 3u / 2u;
    input.offset = 0;
    input.strideBytes = 640;

    QVERIFY(!publisher.publishFrameDma(&session, input));
    QVERIFY(::close(input.fd) == 0);
    QCOMPARE(converter.dmaInputCalls, 1);
    QCOMPARE(converter.byteArrayCalls, 0);
    QCOMPARE(analysisSource.dmabufDescriptors.size(), 0);
    QCOMPARE(analysisSource.descriptors.size(), 0);
    QCOMPARE(session.nextFrameId, 1ull);

    delete session.frameRing;
    session.frameRing = nullptr;
}

QTEST_MAIN(GstreamerPipelineComponentsTest)
#include "gstreamer_pipeline_components_test.moc"
