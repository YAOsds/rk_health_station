#include "core/video_service.h"
#include "ipc/video_gateway.h"
#include "pipeline/video_pipeline_backend.h"
#include "protocol/video_ipc.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QtTest/QTest>

class FakeVideoPipelineBackend : public VideoPipelineBackend {
public:
    void setObserver(VideoPipelineObserver *observer) override {
        observer_ = observer;
    }

    bool startPreview(const VideoChannelStatus &, QString *previewUrl, QString *error) override {
        *previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
        error->clear();
        return true;
    }

    bool stopPreview(const QString &, QString *error) override {
        error->clear();
        return true;
    }

    bool captureSnapshot(const VideoChannelStatus &, const QString &, QString *error) override {
        error->clear();
        return true;
    }

    bool startRecording(const VideoChannelStatus &, const QString &, QString *error) override {
        error->clear();
        return true;
    }

    bool stopRecording(const QString &, QString *error) override {
        error->clear();
        return true;
    }

private:
    VideoPipelineObserver *observer_ = nullptr;
};

class VideoGatewayTest : public QObject {
    Q_OBJECT

private slots:
    void returnsStatusForGetStatus();
    void routesStartTestInputCommand();
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

void VideoGatewayTest::routesStartTestInputCommand() {
    qputenv("RK_VIDEO_SOCKET_NAME", QByteArray("/tmp/rk_video_gateway_test.sock"));

    FakeVideoPipelineBackend backend;
    VideoService service(&backend, nullptr);
    VideoGateway gateway(&service);
    QVERIFY(gateway.start());

    const QString path = QDir::temp().filePath(QStringLiteral("fall-demo.mp4"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    QLocalSocket socket;
    socket.connectToServer(VideoGateway::socketName());
    QVERIFY(socket.waitForConnected(1000));

    VideoCommand command;
    command.action = QStringLiteral("start_test_input");
    command.requestId = QStringLiteral("video-2");
    command.cameraId = QStringLiteral("front_cam");
    command.payload.insert(QStringLiteral("file_path"), path);
    socket.write(QJsonDocument(videoCommandToJson(command)).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 1000);

    const QByteArray frame = socket.readAll().trimmed();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
    QVERIFY(parseError.error == QJsonParseError::NoError);

    VideoCommandResult result;
    QVERIFY(videoCommandResultFromJson(document.object(), &result));
    QVERIFY(result.ok);
    QCOMPARE(result.action, QStringLiteral("start_test_input"));

    gateway.stop();
    qunsetenv("RK_VIDEO_SOCKET_NAME");
}

QTEST_MAIN(VideoGatewayTest)
#include "video_gateway_test.moc"
