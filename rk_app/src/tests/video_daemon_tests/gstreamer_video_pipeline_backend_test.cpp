#include "pipeline/gstreamer_video_pipeline_backend.h"

#include <QFile>
#include <QTemporaryDir>
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

class GstreamerVideoPipelineBackendTest : public QObject {
    Q_OBJECT

private slots:
    void rejectsPipelineThatExitsDuringPreviewStartup();
    void returnsTcpMjpegPreviewUrlForRunningPreview();
    void usesGenericFileDecodePipelineForTestInput();
    void forwardsRgbFramesToAnalysisSource();
    void capsAnalysisTapRateAtStableBaselineFps();
};

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
    QVERIFY(arguments.contains(QStringLiteral("jpegenc")));
    QVERIFY(arguments.contains(QStringLiteral("multipartmux")));
    QVERIFY(arguments.contains(QStringLiteral("videorate")));
    QVERIFY(arguments.contains(QStringLiteral("drop-only=true")));
    QVERIFY(arguments.contains(QStringLiteral("fdsink")));
    QVERIFY(arguments.contains(QStringLiteral("fd=1")));
    QVERIFY(arguments.contains(QStringLiteral("framerate=15/1")));
    QVERIFY(!arguments.contains(QStringLiteral("framerate=10/1")));

    qunsetenv("RK_VIDEO_GST_LAUNCH_BIN");
}

QTEST_MAIN(GstreamerVideoPipelineBackendTest)
#include "gstreamer_video_pipeline_backend_test.moc"
