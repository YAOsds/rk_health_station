#include "core/video_service.h"
#include "ipc/video_gateway.h"
#include "protocol/video_ipc.h"

#include <QJsonDocument>
#include <QLocalSocket>
#include <QtTest/QTest>

class VideoGatewayTest : public QObject {
    Q_OBJECT

private slots:
    void returnsStatusForGetStatus();
};

void VideoGatewayTest::returnsStatusForGetStatus() {
    qputenv("RK_VIDEO_SOCKET_NAME", QByteArray("/tmp/rk_video_gateway_test.sock"));

    VideoService service;
    VideoGateway gateway(&service);
    QVERIFY(gateway.start());

    QLocalSocket socket;
    socket.connectToServer(VideoGateway::socketName());
    QVERIFY(socket.waitForConnected(1000));

    VideoCommand command;
    command.action = QStringLiteral("get_status");
    command.requestId = QStringLiteral("video-1");
    command.cameraId = QStringLiteral("front_cam");
    socket.write(QJsonDocument(videoCommandToJson(command)).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 1000);
    QVERIFY(socket.readAll().contains("front_cam"));

    gateway.stop();
    qunsetenv("RK_VIDEO_SOCKET_NAME");
}

QTEST_MAIN(VideoGatewayTest)
#include "video_gateway_test.moc"
