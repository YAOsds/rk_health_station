#include "protocol/video_ipc.h"

#include <QtTest/QTest>

class VideoProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsStatusPayload();
    void roundTripsTestInputStatusPayload();
    void roundTripsCommandPayload();
    void roundTripsStartTestInputCommandPayload();
    void rejectsInvalidCameraState();
};

void VideoProtocolTest::roundTripsStatusPayload() {
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.displayName = QStringLiteral("Front Camera");
    status.devicePath = QStringLiteral("/dev/video11");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.storageDir = QStringLiteral("/home/elf/videosurv/");
    status.lastSnapshotPath = QStringLiteral("/home/elf/videosurv/snapshot_20260415_101530.jpg");
    status.currentRecordPath = QStringLiteral("/home/elf/videosurv/record_20260415_101600.mp4");
    status.lastError.clear();
    status.recording = true;

    const QJsonObject json = videoChannelStatusToJson(status);
    VideoChannelStatus decoded;
    QVERIFY(videoChannelStatusFromJson(json, &decoded));
    QCOMPARE(decoded.cameraId, status.cameraId);
    QCOMPARE(decoded.previewUrl, status.previewUrl);
    QCOMPARE(decoded.storageDir, status.storageDir);
    QCOMPARE(decoded.cameraState, status.cameraState);
    QCOMPARE(decoded.recording, status.recording);
}

void VideoProtocolTest::roundTripsTestInputStatusPayload() {
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.inputMode = QStringLiteral("test_file");
    status.testFilePath = QStringLiteral("/tmp/fall-demo.mp4");
    status.testPlaybackState = QStringLiteral("finished");

    const QJsonObject json = videoChannelStatusToJson(status);
    VideoChannelStatus decoded;
    QVERIFY(videoChannelStatusFromJson(json, &decoded));
    QCOMPARE(decoded.inputMode, QStringLiteral("test_file"));
    QCOMPARE(decoded.testFilePath, QStringLiteral("/tmp/fall-demo.mp4"));
    QCOMPARE(decoded.testPlaybackState, QStringLiteral("finished"));
}

void VideoProtocolTest::roundTripsCommandPayload() {
    VideoCommand command;
    command.action = QStringLiteral("set_storage_dir");
    command.requestId = QStringLiteral("video-1");
    command.cameraId = QStringLiteral("front_cam");
    command.payload.insert(QStringLiteral("storage_dir"), QStringLiteral("/home/elf/videosurv/"));

    const QJsonObject json = videoCommandToJson(command);
    VideoCommand decoded;
    QVERIFY(videoCommandFromJson(json, &decoded));
    QCOMPARE(decoded.action, command.action);
    QCOMPARE(decoded.cameraId, command.cameraId);
    QCOMPARE(decoded.payload.value(QStringLiteral("storage_dir")).toString(), QStringLiteral("/home/elf/videosurv/"));
}

void VideoProtocolTest::roundTripsStartTestInputCommandPayload() {
    VideoCommand command;
    command.action = QStringLiteral("start_test_input");
    command.requestId = QStringLiteral("video-2");
    command.cameraId = QStringLiteral("front_cam");
    command.payload.insert(QStringLiteral("file_path"), QStringLiteral("/tmp/fall-demo.mp4"));

    const QJsonObject json = videoCommandToJson(command);
    VideoCommand decoded;
    QVERIFY(videoCommandFromJson(json, &decoded));
    QCOMPARE(decoded.action, QStringLiteral("start_test_input"));
    QCOMPARE(decoded.payload.value(QStringLiteral("file_path")).toString(),
        QStringLiteral("/tmp/fall-demo.mp4"));
}

void VideoProtocolTest::rejectsInvalidCameraState() {
    const QJsonObject json{{QStringLiteral("camera_state"), QStringLiteral("broken_state")}};
    VideoChannelStatus decoded;
    QVERIFY(!videoChannelStatusFromJson(json, &decoded));
}

QTEST_MAIN(VideoProtocolTest)

#include "video_protocol_test.moc"
