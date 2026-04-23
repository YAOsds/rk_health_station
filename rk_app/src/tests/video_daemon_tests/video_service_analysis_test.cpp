#include "analysis/analysis_output_backend.h"
#include "core/video_service.h"
#include "pipeline/video_pipeline_backend.h"

#include <QtTest/QTest>

class FakeAnalysisOutputBackend : public AnalysisOutputBackend {
public:
    bool start(const VideoChannelStatus &status, QString *error) override {
        lastStartedCameraId = status.cameraId;
        if (error) {
            error->clear();
        }
        started = true;
        return true;
    }

    bool stop(const QString &cameraId, QString *error) override {
        lastStoppedCameraId = cameraId;
        if (error) {
            error->clear();
        }
        stopped = true;
        return true;
    }

    AnalysisChannelStatus statusForCamera(const QString &cameraId) const override {
        AnalysisChannelStatus status;
        status.cameraId = cameraId;
        status.enabled = started && !stopped;
        status.streamConnected = started && !stopped;
        status.outputFormat = QStringLiteral("rgb");
        status.width = 640;
        status.height = 640;
        status.fps = 15;
        return status;
    }

    bool acceptsFrames(const QString &cameraId) const override {
        return started && !stopped && cameraId == lastStartedCameraId;
    }

    void publishDescriptor(const AnalysisFrameDescriptor &descriptor) override {
        publishedDescriptors += 1;
        lastPublishedDescriptor = descriptor;
    }

    bool started = false;
    bool stopped = false;
    QString lastStartedCameraId;
    QString lastStoppedCameraId;
    int publishedDescriptors = 0;
    AnalysisFrameDescriptor lastPublishedDescriptor;
};

class FakeVideoPipelineBackend : public VideoPipelineBackend {
public:
    void setObserver(VideoPipelineObserver *observer) override { observer_ = observer; }
    void setAnalysisFrameSource(AnalysisFrameSource *source) override { source_ = source; }

    bool startPreview(const VideoChannelStatus &status, QString *previewUrl, QString *error) override {
        if (!source_ || !source_->acceptsFrames(status.cameraId)) {
            if (error) {
                *error = QStringLiteral("analysis_source_not_ready");
            }
            return false;
        }
        startPreviewCalled = true;
        lastCameraId = status.cameraId;
        if (previewUrl) {
            *previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
        }
        if (error) {
            error->clear();
        }
        return true;
    }

    bool stopPreview(const QString &cameraId, QString *error) override {
        lastCameraId = cameraId;
        if (error) {
            error->clear();
        }
        return true;
    }

    bool captureSnapshot(const VideoChannelStatus &, const QString &, QString *error) override {
        if (error) {
            error->clear();
        }
        return true;
    }

    bool startRecording(const VideoChannelStatus &status, const QString &, QString *error) override {
        if (!source_ || !source_->acceptsFrames(status.cameraId)) {
            if (error) {
                *error = QStringLiteral("analysis_source_not_ready");
            }
            return false;
        }
        startRecordingCalled = true;
        lastCameraId = status.cameraId;
        if (error) {
            error->clear();
        }
        return true;
    }

    bool stopRecording(const QString &cameraId, QString *error) override {
        lastCameraId = cameraId;
        if (error) {
            error->clear();
        }
        return true;
    }

    VideoPipelineObserver *observer_ = nullptr;
    AnalysisFrameSource *source_ = nullptr;
    bool startPreviewCalled = false;
    bool startRecordingCalled = false;
    QString lastCameraId;
};

class VideoServiceAnalysisTest : public QObject {
    Q_OBJECT

private slots:
    void analysisIsDisabledByDefault();
    void enablesAnalysisBeforeStartingPreviewWhenConfigured();
    void reportsRgbAnalysisStatus();
};

void VideoServiceAnalysisTest::analysisIsDisabledByDefault() {
    FakeAnalysisOutputBackend analysisBackend;
    FakeVideoPipelineBackend pipelineBackend;
    VideoService service(&pipelineBackend, &analysisBackend, nullptr);

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.cameraState, VideoCameraState::Idle);
    QVERIFY(!analysisBackend.started);
    const AnalysisChannelStatus analysisStatus = service.analysisStatusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(analysisStatus.cameraId, QStringLiteral("front_cam"));
    QVERIFY(!analysisStatus.enabled);
}

void VideoServiceAnalysisTest::enablesAnalysisBeforeStartingPreviewWhenConfigured() {
    qputenv("RK_VIDEO_ANALYSIS_ENABLED", QByteArray("1"));

    FakeAnalysisOutputBackend analysisBackend;
    FakeVideoPipelineBackend pipelineBackend;
    VideoService service(&pipelineBackend, &analysisBackend, nullptr);

    const VideoCommandResult result = service.startPreview(QStringLiteral("front_cam"));
    QVERIFY(result.ok);
    QVERIFY(analysisBackend.started);
    QVERIFY(pipelineBackend.startPreviewCalled);
    QCOMPARE(analysisBackend.lastStartedCameraId, QStringLiteral("front_cam"));

    qunsetenv("RK_VIDEO_ANALYSIS_ENABLED");
}

void VideoServiceAnalysisTest::reportsRgbAnalysisStatus() {
    qputenv("RK_VIDEO_ANALYSIS_ENABLED", QByteArray("1"));

    FakeAnalysisOutputBackend analysisBackend;
    FakeVideoPipelineBackend pipelineBackend;
    VideoService service(&pipelineBackend, &analysisBackend, nullptr);

    const VideoCommandResult result = service.startPreview(QStringLiteral("front_cam"));
    QVERIFY(result.ok);

    const AnalysisChannelStatus status = service.analysisStatusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.outputFormat, QStringLiteral("rgb"));
    QCOMPARE(status.width, 640);
    QCOMPARE(status.height, 640);
    QCOMPARE(status.fps, 15);

    qunsetenv("RK_VIDEO_ANALYSIS_ENABLED");
}

QTEST_MAIN(VideoServiceAnalysisTest)
#include "video_service_analysis_test.moc"
