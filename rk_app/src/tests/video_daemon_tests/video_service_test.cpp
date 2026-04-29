#include "core/video_service.h"
#include "pipeline/video_pipeline_backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QTest>

class FakeVideoPipelineBackend : public VideoPipelineBackend {
public:
    void setObserver(VideoPipelineObserver *observer) override {
        observer_ = observer;
    }

    void setAnalysisFrameSource(AnalysisFrameSource *source) override {
        source_ = source;
    }

    bool startPreview(const VideoChannelStatus &, QString *previewUrl, QString *error) override {
        *previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
        error->clear();
        previewRunning = true;
        startPreviewCalls += 1;
        return true;
    }

    bool stopPreview(const QString &, QString *error) override {
        previewRunning = false;
        error->clear();
        stopPreviewCalls += 1;
        return true;
    }

    bool captureSnapshot(const VideoChannelStatus &, const QString &outputPath, QString *error) override {
        lastSnapshotPath = outputPath;
        error->clear();
        captureSnapshotCalls += 1;
        return true;
    }

    bool startRecording(const VideoChannelStatus &, const QString &outputPath, QString *error) override {
        activeRecordPath = outputPath;
        previewRunning = true;
        error->clear();
        startRecordingCalls += 1;
        return true;
    }

    bool stopRecording(const QString &, QString *error) override {
        activeRecordPath.clear();
        error->clear();
        stopRecordingCalls += 1;
        return true;
    }

    void emitPlaybackFinished(const QString &cameraId) {
        if (observer_ != nullptr) {
            observer_->onPipelinePlaybackFinished(cameraId);
        }
    }

    bool previewRunning = false;
    QString activeRecordPath;
    QString lastSnapshotPath;
    int startPreviewCalls = 0;
    int stopPreviewCalls = 0;
    int captureSnapshotCalls = 0;
    int startRecordingCalls = 0;
    int stopRecordingCalls = 0;

private:
    VideoPipelineObserver *observer_ = nullptr;
    AnalysisFrameSource *source_ = nullptr;
};

class VideoServiceTest : public QObject {
    Q_OBJECT

private slots:
    void usesDefaultStorageDirForFrontCamera();
    void rejectsNonWritableStorageDir();
    void startsRecordingAndUpdatesStatus();
    void stopsRecordingAndReturnsToPreviewing();
    void capturesSnapshotAndReturnsAbsolutePath();
    void capturesSnapshotWithoutRestartingPreview();
    void startsRecordingWithoutStoppingPreview();
    void startsTestInputAndDisablesCameraOnlyOperations();
    void writesPlaybackStartMarkerWhenTestInputStarts();
    void keepsTestModeAfterPlaybackFinished();
    void restoresCameraModeWhenStoppingTestInput();
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

void VideoServiceTest::capturesSnapshotWithoutRestartingPreview() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);

    QVERIFY(service.startPreview(QStringLiteral("front_cam")).ok);
    QCOMPARE(backend.startPreviewCalls, 1);
    QCOMPARE(backend.stopPreviewCalls, 0);

    const VideoCommandResult result = service.takeSnapshot(QStringLiteral("front_cam"));
    QVERIFY(result.ok);
    QCOMPARE(backend.captureSnapshotCalls, 1);
    QCOMPARE(backend.stopPreviewCalls, 0);
    QCOMPARE(backend.startPreviewCalls, 1);

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.cameraState, VideoCameraState::Previewing);
    QCOMPARE(status.previewUrl,
        QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview"));
}

void VideoServiceTest::startsRecordingWithoutStoppingPreview() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);

    QVERIFY(service.startPreview(QStringLiteral("front_cam")).ok);
    QCOMPARE(backend.startPreviewCalls, 1);
    QCOMPARE(backend.stopPreviewCalls, 0);

    const VideoCommandResult result = service.startRecording(QStringLiteral("front_cam"));
    QVERIFY(result.ok);
    QCOMPARE(backend.startRecordingCalls, 1);
    QCOMPARE(backend.stopPreviewCalls, 0);

    const VideoChannelStatus recordingStatus = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(recordingStatus.cameraState, VideoCameraState::Recording);
    QCOMPARE(recordingStatus.previewUrl,
        QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview"));

    QVERIFY(service.stopRecording(QStringLiteral("front_cam")).ok);
    QCOMPARE(backend.stopRecordingCalls, 1);

    const VideoChannelStatus stoppedStatus = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(stoppedStatus.cameraState, VideoCameraState::Previewing);
    QCOMPARE(stoppedStatus.previewUrl,
        QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview"));
}

void VideoServiceTest::startsTestInputAndDisablesCameraOnlyOperations() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);
    const QString path = QDir::temp().filePath(QStringLiteral("fall-demo.mp4"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    const VideoCommandResult result = service.startTestInput(QStringLiteral("front_cam"), path);
    QVERIFY(result.ok);

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.inputMode, QStringLiteral("test_file"));
    QCOMPARE(status.testFilePath, path);
    QCOMPARE(status.testPlaybackState, QStringLiteral("playing"));
    QCOMPARE(service.takeSnapshot(QStringLiteral("front_cam")).errorCode,
        QStringLiteral("unsupported_in_test_mode"));
    QCOMPARE(service.startRecording(QStringLiteral("front_cam")).errorCode,
        QStringLiteral("unsupported_in_test_mode"));
}

void VideoServiceTest::writesPlaybackStartMarkerWhenTestInputStarts() {
    FakeVideoPipelineBackend backend;

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    AppRuntimeConfig config = buildDefaultAppRuntimeConfig();
    config.debug.videoLatencyMarkerPath = tempDir.filePath(QStringLiteral("video-latency.jsonl"));
    VideoService service(config, &backend);

    const QString path = QDir::temp().filePath(QStringLiteral("latency-demo.mp4"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    QVERIFY(service.startTestInput(QStringLiteral("front_cam"), path).ok);

    QFile marker(tempDir.filePath(QStringLiteral("video-latency.jsonl")));
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray content = marker.readAll();
    QVERIFY(content.contains("playback_started"));

}

void VideoServiceTest::keepsTestModeAfterPlaybackFinished() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);
    const QString path = QDir::temp().filePath(QStringLiteral("fall-demo.mp4"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    QVERIFY(service.startTestInput(QStringLiteral("front_cam"), path).ok);
    backend.emitPlaybackFinished(QStringLiteral("front_cam"));

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.inputMode, QStringLiteral("test_file"));
    QCOMPARE(status.testPlaybackState, QStringLiteral("finished"));
}

void VideoServiceTest::restoresCameraModeWhenStoppingTestInput() {
    FakeVideoPipelineBackend backend;
    VideoService service(&backend);
    const QString path = QDir::temp().filePath(QStringLiteral("fall-demo.mp4"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    QVERIFY(service.startTestInput(QStringLiteral("front_cam"), path).ok);
    const VideoCommandResult result = service.stopTestInput(QStringLiteral("front_cam"));
    QVERIFY(result.ok);

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.inputMode, QStringLiteral("camera"));
    QCOMPARE(status.testFilePath, QString());
    QCOMPARE(status.testPlaybackState, QStringLiteral("idle"));
}

QTEST_MAIN(VideoServiceTest)
#include "video_service_test.moc"
