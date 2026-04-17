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
    void showsMultiPersonClassificationOverlay();
    void showsNoPersonOverlayWhenClassificationGoesStale();
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
    batch.results.push_back({QStringLiteral("stand"), 0.91});
    batch.results.push_back({QStringLiteral("fall"), 0.96});
    emit fallClient.classificationBatchUpdated(batch);

    QCOMPARE(page.previewOverlayRows(),
        QStringList({QStringLiteral("stand 0.91"), QStringLiteral("fall 0.96")}));
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

    QTRY_COMPARE_WITH_TIMEOUT(page.previewOverlayRows(), QStringList({QStringLiteral("no person")}), 2500);
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
