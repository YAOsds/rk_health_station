#include "pipeline/gstreamer_video_pipeline_backend.h"
#include "analysis/shared_memory_frame_ring.h"

#include <fcntl.h>
#include <QFile>
#include <QTemporaryDir>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <QtTest/QTest>

class RecordingAnalysisFrameSource : public AnalysisFrameSource {
public:
    bool acceptsFrames(const QString &cameraId) const override {
        return enabled && cameraId == QStringLiteral("front_cam");
    }

    void publishDescriptor(const AnalysisFrameDescriptor &descriptor) override {
        descriptors.append(descriptor);
    }

    bool enabled = true;
    QVector<AnalysisFrameDescriptor> descriptors;
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
    void returnsTcpMjpegPreviewUrlForRunningPreview();
    void usesGenericFileDecodePipelineForTestInput();
    void forwardsRgbFramesToAnalysisSource();
    void usesBurstTolerantSharedMemoryRingSize();
    void capsAnalysisTapRateAtStableBaselineFps();
    void usesRgaAnalysisTapWhenRequested();
    void convertsRgaNv12FramesToRgbDescriptors();
    void usesHardwareJpegEncoderForRecordingPreviewBranch();
};

void GstreamerVideoPipelineBackendTest::cleanup() {
    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
    qunsetenv("RK_VIDEO_ANALYSIS_CONVERT_BACKEND");
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

void GstreamerVideoPipelineBackendTest::usesHardwareJpegEncoderForRecordingPreviewBranch() {
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
    status.recordProfile.width = 1280;
    status.recordProfile.height = 720;
    status.recordProfile.fps = 30;
    status.recordProfile.pixelFormat = QStringLiteral("NV12");

    GstreamerVideoPipelineBackend backend;
    QString error;
    QVERIFY(backend.startRecording(status, tempDir.filePath(QStringLiteral("record.mp4")), &error));
    QVERIFY(QFile::exists(capturePath));

    QFile captured(capturePath);
    QVERIFY(captured.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString arguments = QString::fromUtf8(captured.readAll());
    QVERIFY(arguments.contains(QStringLiteral("mppjpegenc")));
    QVERIFY(arguments.contains(QStringLiteral("rc-mode=fixqp")));
    QVERIFY(arguments.contains(QStringLiteral("q-factor=95")));
    QVERIFY(!arguments.contains(QStringLiteral(" ! jpegenc ! ")));
    QVERIFY(arguments.contains(QStringLiteral("video/x-raw,format=NV12,width=640,height=480")));
    QVERIFY(arguments.contains(QStringLiteral("mpph264enc")));
    QVERIFY(arguments.contains(QStringLiteral("multipartmux")));
    QVERIFY(arguments.contains(QStringLiteral("tcpserversink")));

    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

QTEST_MAIN(GstreamerVideoPipelineBackendTest)
#include "gstreamer_video_pipeline_backend_test.moc"
