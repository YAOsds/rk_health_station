#include "ipc_client/fall_ipc_client.h"
#include "ipc_client/video_ipc_client.h"
#include "pages/video_monitor_page.h"

#include <QPushButton>
#include <QtTest/QTest>

class FakeVideoIpcClient : public AbstractVideoClient {
    Q_OBJECT

public:
    explicit FakeVideoIpcClient(QObject *parent = nullptr)
        : AbstractVideoClient(parent) {
    }

    bool connectToBackend() override { return true; }
    void requestStatus(const QString &) override {}
    void takeSnapshot(const QString &) override {}
    void startRecording(const QString &) override {}
    void stopRecording(const QString &) override {}
    void setStorageDir(const QString &, const QString &) override {}
};

class FakeFallIpcClient : public AbstractFallClient {
    Q_OBJECT

public:
    explicit FakeFallIpcClient(QObject *parent = nullptr)
        : AbstractFallClient(parent) {
    }

    bool connectToBackend() override { return true; }
    void disconnectFromBackend() override {}
};

class VideoMonitorPageTest : public QObject {
    Q_OBJECT

private slots:
    void showsStatusFieldsAndButtonTransitions();
    void showsFallClassificationOverlay();
    void showsNoPersonOverlayWhenClassificationGoesStale();
};

void VideoMonitorPageTest::showsStatusFieldsAndButtonTransitions() {
    FakeVideoIpcClient client;
    FakeFallIpcClient fallClient;
    VideoMonitorPage page(&client, &fallClient);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.storageDir = QStringLiteral("/home/elf/videosurv/");
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;

    emit client.statusReceived(status);

    QCOMPARE(page.cameraStateText(), QStringLiteral("Previewing"));
    QCOMPARE(page.storageDirText(), QStringLiteral("/home/elf/videosurv/"));
    QVERIFY(page.startRecordingButton()->isEnabled());
    QVERIFY(!page.stopRecordingButton()->isEnabled());
}

void VideoMonitorPageTest::showsFallClassificationOverlay() {
    FakeVideoIpcClient client;
    FakeFallIpcClient fallClient;
    VideoMonitorPage page(&client, &fallClient);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    emit client.statusReceived(status);
    emit fallClient.connectionChanged(true);

    FallClassificationResult result;
    result.cameraId = QStringLiteral("front_cam");
    result.state = QStringLiteral("fall");
    result.confidence = 0.93;
    result.timestampMs = 1776359310534;
    emit fallClient.classificationUpdated(result);

    QCOMPARE(page.previewOverlayText(), QStringLiteral("fall 0.93"));
}

void VideoMonitorPageTest::showsNoPersonOverlayWhenClassificationGoesStale() {
    FakeVideoIpcClient client;
    FakeFallIpcClient fallClient;
    VideoMonitorPage page(&client, &fallClient);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    emit client.statusReceived(status);
    emit fallClient.connectionChanged(true);

    QTRY_COMPARE_WITH_TIMEOUT(page.previewOverlayText(), QStringLiteral("no person"), 2500);
}

QTEST_MAIN(VideoMonitorPageTest)
#include "video_monitor_page_test.moc"
