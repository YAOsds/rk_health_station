#include "analysis/analysis_output_backend.h"
#include "core/video_service.h"

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
        status.outputFormat = QStringLiteral("jpeg");
        status.width = 640;
        status.height = 640;
        status.fps = 10;
        return status;
    }

    bool started = false;
    bool stopped = false;
    QString lastStartedCameraId;
    QString lastStoppedCameraId;
};

class VideoServiceAnalysisTest : public QObject {
    Q_OBJECT

private slots:
    void analysisIsDisabledByDefault();
};

void VideoServiceAnalysisTest::analysisIsDisabledByDefault() {
    FakeAnalysisOutputBackend analysisBackend;
    VideoService service(nullptr, &analysisBackend, nullptr);

    const VideoChannelStatus status = service.statusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(status.cameraState, VideoCameraState::Idle);
    QVERIFY(!analysisBackend.started);
    const AnalysisChannelStatus analysisStatus = service.analysisStatusForCamera(QStringLiteral("front_cam"));
    QCOMPARE(analysisStatus.cameraId, QStringLiteral("front_cam"));
    QVERIFY(!analysisStatus.enabled);
}

QTEST_MAIN(VideoServiceAnalysisTest)
#include "video_service_analysis_test.moc"
