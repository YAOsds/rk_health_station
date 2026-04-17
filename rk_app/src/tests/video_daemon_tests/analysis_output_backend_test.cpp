#include "analysis/gstreamer_analysis_output_backend.h"
#include "protocol/analysis_stream_protocol.h"

#include <QHostAddress>
#include <QLocalSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QTest>

class AnalysisOutputBackendTest : public QObject {
    Q_OBJECT

private slots:
    void resolvesAnalysisSocketFromEnvironment();
    void forwardsPreviewFrameToLocalSocket();
};

void AnalysisOutputBackendTest::resolvesAnalysisSocketFromEnvironment() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", "/tmp/rk_video_analysis.sock");
    GstreamerAnalysisOutputBackend backend;
    QCOMPARE(backend.socketPath(), QStringLiteral("/tmp/rk_video_analysis.sock"));
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}

void AnalysisOutputBackendTest::forwardsPreviewFrameToLocalSocket() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_backend_test.sock"));

    QTcpServer previewServer;
    QVERIFY(previewServer.listen(QHostAddress::LocalHost));

    GstreamerAnalysisOutputBackend backend;
    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:%1?transport=tcp_mjpeg&boundary=rkpreview")
                            .arg(previewServer.serverPort());

    QString error;
    QVERIFY(backend.start(status, &error));
    QVERIFY(error.isEmpty());

    QVERIFY(previewServer.waitForNewConnection(2000));
    QTcpSocket *previewSocket = previewServer.nextPendingConnection();
    QVERIFY(previewSocket != nullptr);

    QLocalSocket client;
    client.connectToServer(QStringLiteral("/tmp/rk_video_analysis_backend_test.sock"));
    QVERIFY(client.waitForConnected(2000));

    QByteArray multipart;
    multipart += "--rkpreview\r\n";
    multipart += "Content-Type: image/jpeg\r\n";
    multipart += "Content-Length: 10\r\n\r\n";
    multipart += "jpeg-bytes";
    multipart += "\r\n";
    previewSocket->write(multipart);
    previewSocket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() > 0 || client.waitForReadyRead(50), 2000);
    AnalysisFramePacket packet;
    QVERIFY(decodeAnalysisFramePacket(client.readAll(), &packet));
    QCOMPARE(packet.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(packet.payload, QByteArray("jpeg-bytes"));

    backend.stop(QStringLiteral("front_cam"), &error);
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}

QTEST_MAIN(AnalysisOutputBackendTest)
#include "analysis_output_backend_test.moc"
