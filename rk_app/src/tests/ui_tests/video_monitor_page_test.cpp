#include "ipc_client/fall_ipc_client.h"
#include "ipc_client/video_ipc_client.h"
#include "pages/video_monitor_page.h"

#include <QPushButton>
#include <QtTest/QTest>
#include <QStringList>

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
    void startTestInput(const QString &, const QString &) override {}
    void stopTestInput(const QString &) override {}
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
    void showsTestModeStatusAndDisablesCameraControls();
    void showsMultiPersonClassificationOverlay();
    void doesNotShowNoPersonWhileClassificationIsStillWarmingUp();
    void showsNoPersonOverlayForEmptyBatch();
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

void VideoMonitorPageTest::showsTestModeStatusAndDisablesCameraControls() {
    FakeVideoIpcClient client;
    FakeFallIpcClient fallClient;
    VideoMonitorPage page(&client, &fallClient, nullptr,
        []() { return QStringLiteral("/tmp/fall-demo.mp4"); });

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    status.inputMode = QStringLiteral("test_file");
    status.testFilePath = QStringLiteral("/tmp/fall-demo.mp4");
    status.testPlaybackState = QStringLiteral("finished");

    emit client.statusReceived(status);

    QCOMPARE(page.inputModeText(), QStringLiteral("Test Video (Finished)"));
    QCOMPARE(page.testFileText(), QStringLiteral("fall-demo.mp4"));
    QVERIFY(!page.takeSnapshotButton()->isEnabled());
    QVERIFY(!page.startRecordingButton()->isEnabled());
    QVERIFY(page.exitTestModeButton()->isEnabled());
}

void VideoMonitorPageTest::showsMultiPersonClassificationOverlay() {
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

    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1776359310534;

    FallClassificationEntry first;
    first.trackId = 3;
    first.iconId = 1;
    first.state = QStringLiteral("stand");
    first.confidence = 0.91;
    batch.results.push_back(first);

    FallClassificationEntry second;
    second.trackId = 5;
    second.iconId = 2;
    second.state = QStringLiteral("fall");
    second.confidence = 0.96;
    batch.results.push_back(second);

    emit fallClient.classificationBatchUpdated(batch);

    QCOMPARE(page.previewOverlayRows(),
        QStringList({QStringLiteral("[1] stand 0.91"), QStringLiteral("[2] fall 0.96")}));
}

void VideoMonitorPageTest::doesNotShowNoPersonWhileClassificationIsStillWarmingUp() {
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

    QTest::qWait(1700);
    QCOMPARE(page.previewOverlayRows(), QStringList());
}

void VideoMonitorPageTest::showsNoPersonOverlayForEmptyBatch() {
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

    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1776359310534;
    emit fallClient.classificationBatchUpdated(batch);

    QCOMPARE(page.previewOverlayRows(), QStringList({QStringLiteral("no person")}));
}

QTEST_MAIN(VideoMonitorPageTest)
#include "video_monitor_page_test.moc"
