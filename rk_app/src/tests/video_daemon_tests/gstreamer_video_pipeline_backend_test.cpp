#include "pipeline/gstreamer_video_pipeline_backend.h"
#include "analysis/shared_memory_frame_ring.h"
#include "runtime_config/app_runtime_config.h"

#include <fcntl.h>
#include <QFile>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <QtTest/QTest>

#include <cerrno>
#include <future>
#include <thread>

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

class GstreamerVideoPipelineBackendTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void rejectsPipelineThatExitsDuringPreviewStartup();
    void rejectsInProcessGstreamerWhenNotBuilt();
    void rejectsInProcessGstreamerWhenConfiguredInRuntimeConfig();
    void fallsBackToExternalPipelineForTestInputWhenInprocessRequested();
    void returnsTcpMjpegPreviewUrlForRunningPreview();
    void usesGenericFileDecodePipelineForTestInput();
    void forwardsRgbFramesToAnalysisSource();
    void usesBurstTolerantSharedMemoryRingSize();
    void capsAnalysisTapRateAtStableBaselineFps();
    void usesRgaAnalysisTapWhenRequested();
    void convertsRgaNv12FramesToRgbDescriptors();
    void publishesRgaDmaOutputWhenRequested();
    void publishesRgaDmaInputOutputWhenProvided();
    void rejectsRgaDmaInputOutputWhenConverterFails();
    void selectsUyvyInputFormatForGstDmabufPath();
    void selectsUyvyInputFormatForConfigDrivenGstDmabufPath();
    void computesStrideBytesForPackedUyvyInput();
    void capturesSnapshotFromPreviewStream();
    void encodesRecordingFromPreviewStream();
    void reusesPreviewStreamForRecording();
};

void GstreamerVideoPipelineBackendTest::cleanup() {
    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
    qunsetenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND");
    qunsetenv("RK_VIDEO_PIPELINE_BACKEND");
    qunsetenv("RK_VIDEO_RGA_OUTPUT_DMABUF");
    qunsetenv("RK_VIDEO_GST_DMABUF_INPUT");
    qunsetenv("RK_VIDEO_GST_FORCE_DMABUF_IO");
}


void GstreamerVideoPipelineBackendTest::rejectsInProcessGstreamerWhenNotBuilt() {
    qputenv("RK_VIDEO_PIPELINE_BACKEND", "inproc_gst");

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    GstreamerVideoPipelineBackend backend;
    QString previewUrl;
    QString error;
    QVERIFY(!backend.startPreview(status, &previewUrl, &error));
    QCOMPARE(error, QStringLiteral("inprocess_gstreamer_not_built"));
    QVERIFY(previewUrl.isEmpty());

    qunsetenv("RK_VIDEO_PIPELINE_BACKEND");
}

void GstreamerVideoPipelineBackendTest::rejectsInProcessGstreamerWhenConfiguredInRuntimeConfig() {
    AppRuntimeConfig config;
    config.video.pipelineBackend = QStringLiteral("inproc_gst");

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    GstreamerVideoPipelineBackend backend(config);
    QString previewUrl;
    QString error;
    QVERIFY(!backend.startPreview(status, &previewUrl, &error));
    QCOMPARE(error, QStringLiteral("inprocess_gstreamer_not_built"));
    QVERIFY(previewUrl.isEmpty());
}

void GstreamerVideoPipelineBackendTest::fallsBackToExternalPipelineForTestInputWhenInprocessRequested() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString capturePath = tempDir.filePath(QStringLiteral("launcher-args.txt"));
    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(QStringLiteral(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" > '%1'\n"
        "sleep 2\n")
            .arg(capturePath)
            .toUtf8());
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());
    qputenv("RK_VIDEO_PIPELINE_BACKEND", "inproc_gst");

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.inputMode = QStringLiteral("test_file");
    status.testFilePath = QStringLiteral("/home/elf/Videos/video.mp4");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    GstreamerVideoPipelineBackend backend;
    QString previewUrl;
    QString error;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(QFile::exists(capturePath));

    QFile captured(capturePath);
    QVERIFY(captured.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString arguments = QString::fromUtf8(captured.readAll());
    QVERIFY(arguments.contains(QStringLiteral("filesrc")));
    QVERIFY(arguments.contains(QStringLiteral("decodebin")));
    QVERIFY(arguments.contains(QStringLiteral("mppjpegenc")));

    qunsetenv("RK_VIDEO_PIPELINE_BACKEND");
    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

void GstreamerVideoPipelineBackendTest::rejectsPipelineThatExitsDuringPreviewStartup() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write("#!/bin/sh\nexit 1\n");
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    GstreamerVideoPipelineBackend backend;
    QString previewUrl;
    QString error;
    QVERIFY(!backend.startPreview(status, &previewUrl, &error));
    QVERIFY(previewUrl.isEmpty());
    QVERIFY(!error.isEmpty());

    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

void GstreamerVideoPipelineBackendTest::returnsTcpMjpegPreviewUrlForRunningPreview() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString capturePath = tempDir.filePath(QStringLiteral("launcher-args.txt"));
    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(QStringLiteral(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" > '%1'\n"
        "sleep 2\n")
            .arg(capturePath)
            .toUtf8());
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    GstreamerVideoPipelineBackend backend;
    QString previewUrl;
    QString error;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    QCOMPARE(
        previewUrl, QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview"));
    QVERIFY(QFile::exists(capturePath));

    QFile captured(capturePath);
    QVERIFY(captured.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString arguments = QString::fromUtf8(captured.readAll());
    QVERIFY(arguments.contains(QStringLiteral("video/x-raw,format=NV12")));
    QVERIFY(arguments.contains(QStringLiteral("mppjpegenc")));
    QVERIFY(arguments.contains(QStringLiteral("rc-mode=fixqp")));
    QVERIFY(arguments.contains(QStringLiteral("q-factor=95")));
    QVERIFY(!arguments.contains(QStringLiteral(" ! jpegenc ! ")));
    QVERIFY(arguments.contains(QStringLiteral("multipartmux")));
    QVERIFY(arguments.contains(QStringLiteral("tcpserversink")));

    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

void GstreamerVideoPipelineBackendTest::usesGenericFileDecodePipelineForTestInput() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString capturePath = tempDir.filePath(QStringLiteral("launcher-args.txt"));
    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(QStringLiteral(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" > '%1'\n"
        "sleep 2\n")
            .arg(capturePath)
            .toUtf8());
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.inputMode = QStringLiteral("test_file");
    status.testFilePath = QStringLiteral("/home/elf/Videos/video.mp4");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;

    GstreamerVideoPipelineBackend backend;
    QString previewUrl;
    QString error;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    QVERIFY(QFile::exists(capturePath));

    QFile captured(capturePath);
    QVERIFY(captured.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString arguments = QString::fromUtf8(captured.readAll());
    QVERIFY(arguments.contains(QStringLiteral("decodebin")));
    QVERIFY(arguments.contains(QStringLiteral("name=dec")));
    QVERIFY(arguments.contains(QStringLiteral("video/x-raw,format=NV12")));
    QVERIFY(arguments.contains(QStringLiteral("mppjpegenc")));
    QVERIFY(arguments.contains(QStringLiteral("rc-mode=fixqp")));
    QVERIFY(arguments.contains(QStringLiteral("q-factor=95")));
    QVERIFY(!arguments.contains(QStringLiteral(" ! jpegenc ! ")));
    QVERIFY(arguments.contains(QStringLiteral("audioconvert")));
    QVERIFY(!arguments.contains(QStringLiteral("qtdemux")));

    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

void GstreamerVideoPipelineBackendTest::forwardsRgbFramesToAnalysisSource() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(
        "#!/bin/sh\n"
        "dd if=/dev/zero bs=1228800 count=1 2>/dev/null\n"
        "sleep 2\n");
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 4;
    status.previewProfile.height = 2;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    RecordingAnalysisFrameSource analysisSource;
    GstreamerVideoPipelineBackend backend;
    backend.setAnalysisFrameSource(&analysisSource);

    QString previewUrl;
    QString error;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    QTRY_COMPARE_WITH_TIMEOUT(analysisSource.descriptors.size(), 1, 2000);
    QCOMPARE(analysisSource.descriptors.first().cameraId, QStringLiteral("front_cam"));
    QCOMPARE(analysisSource.descriptors.first().width, 640);
    QCOMPARE(analysisSource.descriptors.first().height, 640);
    QCOMPARE(analysisSource.descriptors.first().pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(analysisSource.descriptors.first().payloadBytes, 640u * 640u * 3u);

    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

void GstreamerVideoPipelineBackendTest::usesBurstTolerantSharedMemoryRingSize() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(
        "#!/bin/sh\n"
        "dd if=/dev/zero bs=1228800 count=1 2>/dev/null\n"
        "sleep 2\n");
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 4;
    status.previewProfile.height = 2;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    RecordingAnalysisFrameSource analysisSource;
    GstreamerVideoPipelineBackend backend;
    backend.setAnalysisFrameSource(&analysisSource);

    QString previewUrl;
    QString error;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    QTRY_COMPARE_WITH_TIMEOUT(analysisSource.descriptors.size(), 1, 2000);

    const QString shmName = sharedMemoryNameForCamera(QStringLiteral("front_cam"));
    const int fd = ::shm_open(shmName.toUtf8().constData(), O_RDONLY, 0);
    QVERIFY(fd >= 0);

    struct stat statBuffer;
    QCOMPARE(::fstat(fd, &statBuffer), 0);
    void *mapped = ::mmap(nullptr, static_cast<size_t>(statBuffer.st_size), PROT_READ, MAP_SHARED, fd, 0);
    QVERIFY(mapped != MAP_FAILED);

    const auto *header = static_cast<const SharedFrameRingHeader *>(mapped);
    QCOMPARE(header->slotCount, 32);

    QVERIFY(::munmap(mapped, static_cast<size_t>(statBuffer.st_size)) == 0);
    QVERIFY(::close(fd) == 0);
    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

void GstreamerVideoPipelineBackendTest::capsAnalysisTapRateAtStableBaselineFps() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString capturePath = tempDir.filePath(QStringLiteral("launcher-args.txt"));
    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(QStringLiteral(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" > '%1'\n"
        "sleep 2\n")
            .arg(capturePath)
            .toUtf8());
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());
    qunsetenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND");

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    RecordingAnalysisFrameSource analysisSource;
    GstreamerVideoPipelineBackend backend;
    backend.setAnalysisFrameSource(&analysisSource);

    QString previewUrl;
    QString error;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    QVERIFY(QFile::exists(capturePath));

    QFile captured(capturePath);
    QVERIFY(captured.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString arguments = QString::fromUtf8(captured.readAll());
    QVERIFY(arguments.contains(QStringLiteral("videoconvert")));
    QVERIFY(arguments.contains(QStringLiteral("videoscale")));
    QVERIFY(arguments.contains(QStringLiteral("video/x-raw,format=RGB")));
    QVERIFY(arguments.contains(QStringLiteral("mppjpegenc")));
    QVERIFY(arguments.contains(QStringLiteral("rc-mode=fixqp")));
    QVERIFY(arguments.contains(QStringLiteral("q-factor=95")));
    QVERIFY(!arguments.contains(QStringLiteral(" ! jpegenc ! ")));
    QVERIFY(arguments.contains(QStringLiteral("multipartmux")));
    QVERIFY(arguments.contains(QStringLiteral("videorate")));
    QVERIFY(arguments.contains(QStringLiteral("drop-only=true")));
    QVERIFY(arguments.contains(QStringLiteral("fdsink")));
    QVERIFY(arguments.contains(QStringLiteral("fd=1")));
    QVERIFY(arguments.contains(QStringLiteral("framerate=15/1")));
    QVERIFY(!arguments.contains(QStringLiteral("framerate=10/1")));

    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}


void GstreamerVideoPipelineBackendTest::usesRgaAnalysisTapWhenRequested() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString capturePath = tempDir.filePath(QStringLiteral("launcher-args.txt"));
    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(QStringLiteral(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" > '%1'\n"
        "sleep 2\n")
            .arg(capturePath)
            .toUtf8());
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());
    qputenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND", "rga");

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    RecordingAnalysisFrameSource analysisSource;
    GstreamerVideoPipelineBackend backend;
    backend.setAnalysisFrameSource(&analysisSource);

    QString previewUrl;
    QString error;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    QVERIFY(QFile::exists(capturePath));

    QFile captured(capturePath);
    QVERIFY(captured.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString arguments = QString::fromUtf8(captured.readAll());
    QVERIFY(arguments.contains(QStringLiteral("video/x-raw,format=NV12,width=640,height=480,framerate=15/1")));
    QVERIFY(!arguments.contains(QStringLiteral("videoconvert")));
    QVERIFY(!arguments.contains(QStringLiteral("videoscale")));
    QVERIFY(arguments.contains(QStringLiteral("fdsink")));
    QVERIFY(arguments.contains(QStringLiteral("fd=1")));

    qunsetenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND");
    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}


void GstreamerVideoPipelineBackendTest::convertsRgaNv12FramesToRgbDescriptors() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(
        "#!/bin/sh\n"
        "dd if=/dev/zero bs=460800 count=1 2>/dev/null\n"
        "sleep 2\n");
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());
    qputenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND", "rga");

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    RecordingAnalysisFrameSource analysisSource;
    FillingAnalysisFrameConverter converter;
    GstreamerVideoPipelineBackend backend;
    backend.setAnalysisFrameSource(&analysisSource);
    backend.setAnalysisFrameConverter(&converter);

    QString previewUrl;
    QString error;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    QTRY_COMPARE_WITH_TIMEOUT(analysisSource.descriptors.size(), 1, 2000);
    QCOMPARE(converter.calls, 1);
    QCOMPARE(converter.lastInputBytes, 640 * 480 * 3 / 2);
    QCOMPARE(converter.lastSrcWidth, 640);
    QCOMPARE(converter.lastSrcHeight, 480);
    QCOMPARE(converter.lastDstWidth, 640);
    QCOMPARE(converter.lastDstHeight, 640);
    QCOMPARE(analysisSource.descriptors.first().width, 640);
    QCOMPARE(analysisSource.descriptors.first().height, 640);
    QCOMPARE(analysisSource.descriptors.first().pixelFormat, AnalysisPixelFormat::Rgb);
    QCOMPARE(analysisSource.descriptors.first().payloadBytes, 640u * 640u * 3u);
    QVERIFY(analysisSource.descriptors.first().posePreprocessed);
    QCOMPARE(analysisSource.descriptors.first().poseXPad, 0);
    QCOMPARE(analysisSource.descriptors.first().poseYPad, 80);
    QCOMPARE(analysisSource.descriptors.first().poseScale, 1.0f);

    qunsetenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND");
    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

void GstreamerVideoPipelineBackendTest::publishesRgaDmaOutputWhenRequested() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(
        "#!/bin/sh\n"
        "dd if=/dev/zero bs=460800 count=1 2>/dev/null\n"
        "sleep 2\n");
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());
    qputenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND", "rga");
    qputenv("RK_VIDEO_RGA_OUTPUT_DMABUF", "1");

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");

    RecordingAnalysisFrameSource analysisSource;
    analysisSource.dmabufSupported = true;
    DmaOutputAnalysisFrameConverter converter;
    GstreamerVideoPipelineBackend backend;
    backend.setAnalysisFrameSource(&analysisSource);
    backend.setAnalysisFrameConverter(&converter);

    QString previewUrl;
    QString error;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    QTRY_COMPARE_WITH_TIMEOUT(analysisSource.dmabufDescriptors.size(), 1, 2000);
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

    qunsetenv("RK_VIDEO_RGA_OUTPUT_DMABUF");
    qunsetenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND");
    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

void GstreamerVideoPipelineBackendTest::publishesRgaDmaInputOutputWhenProvided() {
    qputenv("RK_VIDEO_RGA_OUTPUT_DMABUF", "1");

    RecordingAnalysisFrameSource analysisSource;
    analysisSource.dmabufSupported = true;
    DmaInputOutputAnalysisFrameConverter converter;
    GstreamerVideoPipelineBackend backend;
    backend.setAnalysisFrameSource(&analysisSource);
    backend.setAnalysisFrameConverter(&converter);

    GstreamerVideoPipelineBackend::ActivePipeline pipeline;
    pipeline.cameraId = QStringLiteral("front_cam");
    pipeline.analysisConvertBackend = GstreamerVideoPipelineBackend::AnalysisConvertBackend::Rga;
    pipeline.analysisInputWidth = 640;
    pipeline.analysisInputHeight = 480;
    pipeline.analysisInputFrameBytes = 640 * 480 * 3 / 2;
    pipeline.analysisOutputWidth = 640;
    pipeline.analysisOutputHeight = 640;
    pipeline.analysisOutputFrameBytes = 640 * 640 * 3;
    backend.pipelines_.insert(QStringLiteral("front_cam"), pipeline);

    AnalysisDmaBuffer input;
    input.fd = ::open("/dev/zero", O_RDONLY | O_CLOEXEC);
    QVERIFY(input.fd >= 0);
    input.payloadBytes = 640u * 480u * 3u / 2u;
    input.offset = 0;
    input.strideBytes = 640;

    QVERIFY(backend.processAnalysisFrameDma(QStringLiteral("front_cam"), input));
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
    QCOMPARE(descriptor.poseYPad, 80);

    backend.pipelines_.clear();
    qunsetenv("RK_VIDEO_RGA_OUTPUT_DMABUF");
}

void GstreamerVideoPipelineBackendTest::rejectsRgaDmaInputOutputWhenConverterFails() {
    qputenv("RK_VIDEO_RGA_OUTPUT_DMABUF", "1");

    RecordingAnalysisFrameSource analysisSource;
    analysisSource.dmabufSupported = true;
    FailingDmaInputOutputAnalysisFrameConverter converter;
    GstreamerVideoPipelineBackend backend;
    backend.setAnalysisFrameSource(&analysisSource);
    backend.setAnalysisFrameConverter(&converter);

    GstreamerVideoPipelineBackend::ActivePipeline pipeline;
    pipeline.cameraId = QStringLiteral("front_cam");
    pipeline.analysisConvertBackend = GstreamerVideoPipelineBackend::AnalysisConvertBackend::Rga;
    pipeline.analysisInputWidth = 640;
    pipeline.analysisInputHeight = 480;
    pipeline.analysisInputFrameBytes = 640 * 480 * 3 / 2;
    pipeline.analysisOutputWidth = 640;
    pipeline.analysisOutputHeight = 640;
    pipeline.analysisOutputFrameBytes = 640 * 640 * 3;
    backend.pipelines_.insert(QStringLiteral("front_cam"), pipeline);

    AnalysisDmaBuffer input;
    input.fd = ::open("/dev/zero", O_RDONLY | O_CLOEXEC);
    QVERIFY(input.fd >= 0);
    input.payloadBytes = 640u * 480u * 3u / 2u;
    input.offset = 0;
    input.strideBytes = 640;

    QVERIFY(!backend.processAnalysisFrameDma(QStringLiteral("front_cam"), input));
    QVERIFY(::close(input.fd) == 0);

    QCOMPARE(converter.dmaInputCalls, 1);
    QCOMPARE(converter.byteArrayCalls, 0);
    QCOMPARE(analysisSource.dmabufDescriptors.size(), 0);
    QCOMPARE(analysisSource.descriptors.size(), 0);
    QCOMPARE(backend.pipelines_[QStringLiteral("front_cam")].nextFrameId, 1ull);

    backend.pipelines_.clear();
    qunsetenv("RK_VIDEO_RGA_OUTPUT_DMABUF");
}

void GstreamerVideoPipelineBackendTest::selectsUyvyInputFormatForGstDmabufPath() {
    qputenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND", "rga");
    qputenv("RK_VIDEO_RGA_OUTPUT_DMABUF", "1");
    qputenv("RK_VIDEO_GST_DMABUF_INPUT", "1");

    GstreamerVideoPipelineBackend backend;
    QCOMPARE(backend.inProcessAnalysisInputFormatForBackend(
                 GstreamerVideoPipelineBackend::AnalysisConvertBackend::Rga),
        AnalysisFrameInputFormat::Nv12);
    QCOMPARE(backend.inProcessAnalysisInputFormatForBackend(
                 GstreamerVideoPipelineBackend::AnalysisConvertBackend::GstreamerCpu),
        AnalysisFrameInputFormat::Nv12);

    qputenv("RK_VIDEO_GST_FORCE_DMABUF_IO", "1");
    GstreamerVideoPipelineBackend forceDmaIoBackend;
    QCOMPARE(forceDmaIoBackend.inProcessAnalysisInputFormatForBackend(
                 GstreamerVideoPipelineBackend::AnalysisConvertBackend::Rga),
        AnalysisFrameInputFormat::Uyvy);
    QCOMPARE(forceDmaIoBackend.inProcessAnalysisInputFormatForBackend(
                 GstreamerVideoPipelineBackend::AnalysisConvertBackend::GstreamerCpu),
        AnalysisFrameInputFormat::Nv12);

    qunsetenv("RK_VIDEO_GST_DMABUF_INPUT");
    GstreamerVideoPipelineBackend noDmaInputBackend;
    QCOMPARE(noDmaInputBackend.inProcessAnalysisInputFormatForBackend(
                 GstreamerVideoPipelineBackend::AnalysisConvertBackend::Rga),
        AnalysisFrameInputFormat::Nv12);

    qunsetenv("RK_VIDEO_GST_FORCE_DMABUF_IO");
    qunsetenv("RK_VIDEO_RGA_OUTPUT_DMABUF");
    qunsetenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND");
}

void GstreamerVideoPipelineBackendTest::selectsUyvyInputFormatForConfigDrivenGstDmabufPath() {
    AppRuntimeConfig config;
    config.video.analysisConvertBackend = QStringLiteral("rga");
    config.analysis.rgaOutputDmabuf = true;
    config.analysis.gstDmabufInput = true;
    config.analysis.gstForceDmabufIo = true;

    GstreamerVideoPipelineBackend backend(config);
    QCOMPARE(backend.inProcessAnalysisInputFormatForBackend(
                 GstreamerVideoPipelineBackend::AnalysisConvertBackend::Rga),
        AnalysisFrameInputFormat::Uyvy);
    QCOMPARE(backend.inProcessAnalysisInputFormatForBackend(
                 GstreamerVideoPipelineBackend::AnalysisConvertBackend::GstreamerCpu),
        AnalysisFrameInputFormat::Nv12);
}

void GstreamerVideoPipelineBackendTest::computesStrideBytesForPackedUyvyInput() {
    GstreamerVideoPipelineBackend backend;
    QCOMPARE(backend.strideBytesForAnalysisInputFormat(AnalysisFrameInputFormat::Nv12, 640), 640);
    QCOMPARE(backend.strideBytesForAnalysisInputFormat(AnalysisFrameInputFormat::Uyvy, 640), 1280);
    QCOMPARE(backend.strideBytesForAnalysisInputFormat(AnalysisFrameInputFormat::Uyvy, 0), 0);
}

void GstreamerVideoPipelineBackendTest::capturesSnapshotFromPreviewStream() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QByteArray jpegBytes = sampleJpegBytes();
    const QByteArray multipartPayload = buildMultipartFrame(QByteArrayLiteral("rkpreview"), jpegBytes);

    std::promise<quint16> portPromise;
    std::future<quint16> portFuture = portPromise.get_future();
    std::promise<bool> servedPromise;
    std::future<bool> servedFuture = servedPromise.get_future();
    std::thread serverThread([payload = multipartPayload,
                              port = std::move(portPromise),
                              served = std::move(servedPromise)]() mutable {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            port.set_value(0);
            served.set_value(false);
            return;
        }
        port.set_value(server.serverPort());
        if (!server.waitForNewConnection(5000)) {
            served.set_value(false);
            return;
        }
        QTcpSocket *socket = server.nextPendingConnection();
        if (socket == nullptr) {
            served.set_value(false);
            return;
        }
        socket->write(payload);
        socket->waitForBytesWritten(5000);
        socket->disconnectFromHost();
        socket->waitForDisconnected(1000);
        delete socket;
        served.set_value(true);
    });

    const quint16 port = portFuture.get();
    QVERIFY(port > 0);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:%1?transport=tcp_mjpeg&boundary=rkpreview").arg(port);
    status.devicePath = QStringLiteral("/dev/video11");
    status.snapshotProfile.width = 1920;
    status.snapshotProfile.height = 1080;
    status.snapshotProfile.pixelFormat = QStringLiteral("NV12");

    GstreamerVideoPipelineBackend backend;
    QString error;
    const QString outputPath = tempDir.filePath(QStringLiteral("snapshot.jpg"));
    QVERIFY(backend.captureSnapshot(status, outputPath, &error));
    QVERIFY(error.isEmpty());

    serverThread.join();
    QVERIFY(servedFuture.get());

    QFile output(outputPath);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QCOMPARE(output.readAll(), jpegBytes);
}

void GstreamerVideoPipelineBackendTest::encodesRecordingFromPreviewStream() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString capturePath = tempDir.filePath(QStringLiteral("launcher-args.txt"));
    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(QStringLiteral(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" > '%1'\n"
        "sleep 2\n")
            .arg(capturePath)
            .toUtf8());
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");
    status.recordProfile.width = 1280;
    status.recordProfile.height = 720;
    status.recordProfile.fps = 30;
    status.recordProfile.pixelFormat = QStringLiteral("NV12");

    GstreamerVideoPipelineBackend backend;
    QString error;
    QString previewUrl;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    status.previewUrl = previewUrl;
    QVERIFY(backend.startRecording(status, tempDir.filePath(QStringLiteral("record.mp4")), &error));
    QVERIFY(QFile::exists(capturePath));

    QFile captured(capturePath);
    QVERIFY(captured.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString arguments = QString::fromUtf8(captured.readAll());
    QVERIFY(arguments.contains(QStringLiteral("tcpclientsrc")));
    QVERIFY(arguments.contains(QStringLiteral("multipartdemux")));
    QVERIFY(arguments.contains(QStringLiteral("jpegparse")));
    QVERIFY(arguments.contains(QStringLiteral("jpegdec")));
    QVERIFY(arguments.contains(QStringLiteral("mpph264enc")));
    QVERIFY(!arguments.contains(QStringLiteral("v4l2src")));

    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

void GstreamerVideoPipelineBackendTest::reusesPreviewStreamForRecording() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString capturePath = tempDir.filePath(QStringLiteral("launcher-args.txt"));
    const QString launcherPath = tempDir.filePath(QStringLiteral("fake-gst-launch.sh"));
    QFile launcher(launcherPath);
    QVERIFY(launcher.open(QIODevice::WriteOnly | QIODevice::Text));
    launcher.write(QStringLiteral(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" > '%1'\n"
        "sleep 2\n")
            .arg(capturePath)
            .toUtf8());
    launcher.close();
    QVERIFY(QFile::setPermissions(launcherPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    qputenv("RK_VIDEO_GST_LAUNCH_BIN", launcherPath.toUtf8());

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.devicePath = QStringLiteral("/dev/video11");
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.previewProfile.fps = 30;
    status.previewProfile.pixelFormat = QStringLiteral("NV12");
    status.recordProfile.width = 1280;
    status.recordProfile.height = 720;
    status.recordProfile.fps = 30;
    status.recordProfile.pixelFormat = QStringLiteral("NV12");

    GstreamerVideoPipelineBackend backend;
    QString error;
    QString previewUrl;
    QVERIFY(backend.startPreview(status, &previewUrl, &error));
    status.previewUrl = previewUrl;
    QVERIFY(backend.startRecording(status, tempDir.filePath(QStringLiteral("record.mp4")), &error));
    QVERIFY(QFile::exists(capturePath));

    QFile captured(capturePath);
    QVERIFY(captured.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString arguments = QString::fromUtf8(captured.readAll());
    QVERIFY(arguments.contains(QStringLiteral("tcpclientsrc")));
    QVERIFY(arguments.contains(QStringLiteral("multipartdemux")));
    QVERIFY(arguments.contains(QStringLiteral("jpegparse")));
    QVERIFY(!arguments.contains(QStringLiteral("v4l2src")));

    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

QTEST_MAIN(GstreamerVideoPipelineBackendTest)
#include "gstreamer_video_pipeline_backend_test.moc"
