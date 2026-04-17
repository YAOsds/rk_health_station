#include "core/video_service.h"
#include "pipeline/video_pipeline_backend.h"

#include <QFileInfo>
#include <QtTest/QTest>

class FakeVideoPipelineBackend : public VideoPipelineBackend {
public:
    bool startPreview(const VideoChannelStatus &, QString *previewUrl, QString *error) override {
        *previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
        error->clear();
        previewRunning = true;
        return true;
    }

    bool stopPreview(const QString &, QString *error) override {
        previewRunning = false;
        error->clear();
        return true;
    }

    bool captureSnapshot(const VideoChannelStatus &, const QString &outputPath, QString *error) override {
        lastSnapshotPath = outputPath;
        error->clear();
        return true;
    }

    bool startRecording(const VideoChannelStatus &, const QString &outputPath, QString *error) override {
        activeRecordPath = outputPath;
        previewRunning = true;
        error->clear();
        return true;
    }

    bool stopRecording(const QString &, QString *error) override {
        activeRecordPath.clear();
        error->clear();
        return true;
    }

    bool previewRunning = false;
    QString activeRecordPath;
    QString lastSnapshotPath;
};

class VideoServiceTest : public QObject {
    Q_OBJECT

private slots:
    void usesDefaultStorageDirForFrontCamera();
    void rejectsNonWritableStorageDir();
    void startsRecordingAndUpdatesStatus();
    void stopsRecordingAndReturnsToPreviewing();
    void capturesSnapshotAndReturnsAbsolutePath();
};

void VideoServiceTest::usesDefaultStorageDirForFrontCamera() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);
    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(status.devicePath, QStringLiteral("/dev/video11"));
    QCOMPARE(status.storageDir, QStringLiteral("/home/elf/videosurv/"));
    QCOMPARE(status.previewProfile.width, 640);
    QCOMPARE(status.recordProfile.width, 1280);
    QCOMPARE(status.snapshotProfile.width, 1920);
}

void VideoServiceTest::rejectsNonWritableStorageDir() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);
    VideoCommandResult result = service.applyStorageDir(
        QStringLiteral("front_cam"), QStringLiteral("/proc/forbidden"));
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, QStringLiteral("storage_dir_invalid"));
}

void VideoServiceTest::startsRecordingAndUpdatesStatus() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);

    VideoCommandResult result = service.startRecording(QStringLiteral("front_cam"));
    QVERIFY(result.ok);
    QCOMPARE(service.statusForCamera(QStringLiteral("front_cam")).cameraState, VideoCameraState::Recording);
    QVERIFY(service.statusForCamera(QStringLiteral("front_cam")).currentRecordPath.endsWith(QStringLiteral(".mp4")));
    QVERIFY(!backend.activeRecordPath.isEmpty());
}

void VideoServiceTest::stopsRecordingAndReturnsToPreviewing() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);

    QVERIFY(service.startRecording(QStringLiteral("front_cam")).ok);
    VideoCommandResult result = service.stopRecording(QStringLiteral("front_cam"));
    QVERIFY(result.ok);
    QCOMPARE(service.statusForCamera(QStringLiteral("front_cam")).cameraState, VideoCameraState::Previewing);
    QVERIFY(!service.statusForCamera(QStringLiteral("front_cam")).recording);
}

void VideoServiceTest::capturesSnapshotAndReturnsAbsolutePath() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);

    VideoCommandResult result = service.takeSnapshot(QStringLiteral("front_cam"));
    QVERIFY(result.ok);
    const QString path = result.payload.value(QStringLiteral("snapshot_path")).toString();
    QVERIFY(QFileInfo(path).isAbsolute());
    QVERIFY(path.endsWith(QStringLiteral(".jpg")));
    QCOMPARE(backend.lastSnapshotPath, path);
}

QTEST_MAIN(VideoServiceTest)
#include "video_service_test.moc"
