#include "pipeline/gstreamer_video_pipeline_backend.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

class GstreamerVideoPipelineBackendTest : public QObject {
    Q_OBJECT

private slots:
    void rejectsPipelineThatExitsDuringPreviewStartup();
    void returnsTcpMjpegPreviewUrlForRunningPreview();
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

QTEST_MAIN(GstreamerVideoPipelineBackendTest)
#include "gstreamer_video_pipeline_backend_test.moc"
