#include "app/fall_daemon_app.h"
#include "protocol/analysis_stream_protocol.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

class FallEndToEndStatusTest : public QObject {
    Q_OBJECT

private slots:
    void publishesMonitoringStateWhenStartedWithoutModels();
    void reportsModelReadinessAfterStartup();
    void reportsInputConnectedAfterAnalysisSocketConnects();
    void updatesInferTimestampAfterFrameArrives();
    void startsWithRuleBackendWhenConfiguredExplicitly();
    void streamsClassificationAfterWindowFills();
};

class SinglePoseEstimator : public PoseEstimator {
public:
    bool loadModel(const QString &path, QString *error) override {
        Q_UNUSED(path);
        if (error) {
            error->clear();
        }
        return true;
    }

    QVector<PosePerson> infer(const AnalysisFramePacket &frame, QString *error) override {
        Q_UNUSED(frame);
        if (error) {
            error->clear();
        }

        PosePerson person;
        person.score = 0.95;
        person.keypoints.resize(17);
        for (int index = 0; index < person.keypoints.size(); ++index) {
            person.keypoints[index].x = 100.0 + index;
            person.keypoints[index].y = 200.0 + index;
            person.keypoints[index].score = 0.9;
        }
        person.box = QRectF(80.0, 120.0, 120.0, 240.0);
        return {person};
    }
};

void FallEndToEndStatusTest::publishesMonitoringStateWhenStartedWithoutModels() {
    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e.sock"));

    FallDaemonApp app;
    QVERIFY(app.start());

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("/tmp/rk_fall_e2e.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 2000);

    const QByteArray payload = socket.readAll();
    QVERIFY(payload.contains("monitoring"));
    QVERIFY(payload.contains("front_cam"));

    qunsetenv("RK_FALL_SOCKET_NAME");
}

void FallEndToEndStatusTest::reportsModelReadinessAfterStartup() {
    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_ready.sock"));

    FallDaemonApp app;
    QVERIFY(app.start());

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_ready.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 2000);

    const QByteArray payload = socket.readAll();
    QVERIFY(payload.contains("\"pose_model_ready\":true"));
    QVERIFY(payload.contains("\"action_model_ready\":true"));

    qunsetenv("RK_FALL_SOCKET_NAME");
}

void FallEndToEndStatusTest::reportsInputConnectedAfterAnalysisSocketConnects() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e.sock")));

    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_input.sock"));
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e.sock"));

    FallDaemonApp app;
    QVERIFY(app.start());

    QVERIFY(analysisServer.waitForNewConnection(2000));

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_input.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 2000);

    const QByteArray payload = socket.readAll();
    QVERIFY(payload.contains("\"input_connected\":true"));

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}

void FallEndToEndStatusTest::updatesInferTimestampAfterFrameArrives() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e_frame.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e_frame.sock")));

    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_frame.sock"));
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e_frame.sock"));

    FallDaemonApp app;
    QVERIFY(app.start());

    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    AnalysisFramePacket packet;
    packet.frameId = 31;
    packet.cameraId = QStringLiteral("front_cam");
    packet.width = 640;
    packet.height = 640;
    packet.payload = QByteArray("jpeg-bytes");
    analysisSocket->write(encodeAnalysisFramePacket(packet));
    analysisSocket->flush();

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_frame.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 2000);

    const QJsonObject json = QJsonDocument::fromJson(socket.readAll().trimmed()).object();
    QVERIFY(json.value(QStringLiteral("last_infer_ts")).toDouble() > 0.0);

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}

void FallEndToEndStatusTest::startsWithRuleBackendWhenConfiguredExplicitly() {
    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_rule_backend.sock"));
    qputenv("RK_FALL_ACTION_BACKEND", QByteArray("rule_based"));

    FallDaemonApp app;
    QVERIFY(app.start());

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("/tmp/rk_fall_rule_backend.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 2000);

    const QByteArray payload = socket.readAll();
    QVERIFY(payload.contains("\"action_model_ready\":true"));

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_FALL_ACTION_BACKEND");
}

void FallEndToEndStatusTest::streamsClassificationAfterWindowFills() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e_stream.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e_stream.sock")));

    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_stream.sock"));
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e_stream.sock"));

    FallDaemonApp app(std::make_unique<SinglePoseEstimator>());
    QVERIFY(app.start());
    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    QLocalSocket subscriber;
    subscriber.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_stream.sock"));
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();
    QTest::qWait(50);

    for (quint64 frameId = 1; frameId <= 45; ++frameId) {
        AnalysisFramePacket packet;
        packet.frameId = frameId;
        packet.cameraId = QStringLiteral("front_cam");
        packet.width = 640;
        packet.height = 640;
        packet.payload = QByteArray("jpeg-bytes");
        analysisSocket->write(encodeAnalysisFramePacket(packet));
        analysisSocket->flush();
    }

    QTRY_VERIFY_WITH_TIMEOUT(subscriber.bytesAvailable() > 0 || subscriber.waitForReadyRead(50), 2000);
    const QJsonObject json = QJsonDocument::fromJson(subscriber.readAll().trimmed()).object();
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification"));
    QVERIFY(!json.value(QStringLiteral("state")).toString().isEmpty());

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}

QTEST_MAIN(FallEndToEndStatusTest)
#include "fall_end_to_end_status_test.moc"
