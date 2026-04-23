#include "app/fall_daemon_app.h"
#include "protocol/analysis_stream_protocol.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QTest>

class FallEndToEndStatusTest : public QObject {
    Q_OBJECT

private slots:
    void publishesMonitoringStateWhenStartedWithoutModels();
    void reportsModelReadinessAfterStartup();
    void reportsInputConnectedAfterAnalysisSocketConnects();
    void updatesInferTimestampAfterFrameArrives();
    void startsWithRuleBackendWhenConfiguredExplicitly();
    void writesLatencyMarkersForFirstFrameAndFirstClassification();
    void streamsClassificationAfterWindowFills();
    void streamsClassificationBatchForSinglePerson();
    void streamsClassificationBatchForMultiplePeople();
    void keepsBatchOrderLeftToRightAfterCrossing();
    void isolatesFallConfirmationPerTrackedPerson();
};

namespace {
PosePerson makeRulePose(float centerX, float shoulderY, float hipY, float score = 0.95f) {
    PosePerson person;
    person.score = score;
    person.box = QRectF(centerX - 60.0f, qMin(shoulderY, hipY) - 40.0f, 120.0f, 240.0f);
    person.keypoints.resize(17);
    for (int index = 0; index < person.keypoints.size(); ++index) {
        person.keypoints[index].x = centerX;
        person.keypoints[index].y = hipY;
        person.keypoints[index].score = 0.9f;
    }

    person.keypoints[5].x = centerX - 15.0f;
    person.keypoints[5].y = shoulderY;
    person.keypoints[6].x = centerX + 15.0f;
    person.keypoints[6].y = shoulderY;
    person.keypoints[11].x = centerX - 12.0f;
    person.keypoints[11].y = hipY;
    person.keypoints[12].x = centerX + 12.0f;
    person.keypoints[12].y = hipY;
    return person;
}

QJsonObject readJsonMessage(QLocalSocket *socket, QByteArray *buffer, int timeoutMs = 250) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const int bufferedNewlineIndex = buffer->indexOf('\n');
        if (bufferedNewlineIndex >= 0) {
            const QByteArray line = buffer->left(bufferedNewlineIndex).trimmed();
            buffer->remove(0, bufferedNewlineIndex + 1);
            return QJsonDocument::fromJson(line).object();
        }
        if (socket->bytesAvailable() > 0 || socket->waitForReadyRead(50)) {
            buffer->append(socket->readAll());
            const int newlineIndex = buffer->indexOf('\n');
            if (newlineIndex >= 0) {
                const QByteArray line = buffer->left(newlineIndex).trimmed();
                buffer->remove(0, newlineIndex + 1);
                return QJsonDocument::fromJson(line).object();
            }
        }
    }
    return {};
}

QJsonObject waitForMessageType(QLocalSocket *socket, const QString &type, int timeoutMs = 2000) {
    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        const QJsonObject message = readJsonMessage(socket, &buffer, 50);
        if (message.value(QStringLiteral("type")).toString() == type) {
            return message;
        }
    }
    return {};
}

void streamJpegAnalysisFrames(
    QLocalSocket *analysisSocket, quint64 firstFrameId, quint64 lastFrameId, int waitMs = 5) {
    QVERIFY(analysisSocket != nullptr);
    for (quint64 frameId = firstFrameId; frameId <= lastFrameId; ++frameId) {
        AnalysisFramePacket packet;
        packet.frameId = frameId;
        packet.cameraId = QStringLiteral("front_cam");
        packet.width = 640;
        packet.height = 640;
        packet.payload = QByteArray("jpeg-bytes");
        analysisSocket->write(encodeAnalysisFramePacket(packet));
        analysisSocket->flush();
        QTest::qWait(waitMs);
    }
}
}

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

class MultiPoseEstimatorStub : public PoseEstimator {
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

        PosePerson left;
        left.score = 0.95f;
        left.box = QRectF(40.0, 120.0, 120.0, 240.0);
        left.keypoints.resize(17);
        for (int index = 0; index < left.keypoints.size(); ++index) {
            left.keypoints[index].x = 90.0 + index;
            left.keypoints[index].y = 210.0 + index;
            left.keypoints[index].score = 0.9f;
        }

        PosePerson right;
        right.score = 0.93f;
        right.box = QRectF(280.0, 130.0, 120.0, 240.0);
        right.keypoints.resize(17);
        for (int index = 0; index < right.keypoints.size(); ++index) {
            right.keypoints[index].x = 320.0 + index;
            right.keypoints[index].y = 220.0 + index;
            right.keypoints[index].score = 0.88f;
        }

        return {left, right};
    }
};

class CrossingPoseEstimatorStub : public PoseEstimator {
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

        const float leftX = 80.0f + (frameIndex_ * 4.0f);
        const float rightX = 280.0f - (frameIndex_ * 4.0f);
        const float lieHipY = frameIndex_ < 44 ? (120.0f + frameIndex_) : 190.0f;
        const float lieShoulderY = frameIndex_ < 44 ? 60.0f : 195.0f;

        PosePerson left = makeRulePose(leftX, lieShoulderY, lieHipY, 0.95f);
        PosePerson right = makeRulePose(rightX, 60.0f, 120.0f, 0.93f);

        ++frameIndex_;
        if (frameIndex_ % 2 == 0) {
            return {right, left};
        }
        return {left, right};
    }

private:
    int frameIndex_ = 0;
};

class MixedStatePoseEstimatorStub : public PoseEstimator {
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

        const float lieHipY = frameIndex_ < 44 ? (120.0f + frameIndex_) : 190.0f;
        const float lieShoulderY = frameIndex_ < 44 ? 60.0f : 195.0f;
        ++frameIndex_;
        return {
            makeRulePose(90.0f, lieShoulderY, lieHipY, 0.95f),
            makeRulePose(300.0f, 60.0f, 120.0f, 0.93f)
        };
    }

private:
    int frameIndex_ = 0;
};

void FallEndToEndStatusTest::publishesMonitoringStateWhenStartedWithoutModels() {
    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e.sock"));

    FallDaemonApp app(std::make_unique<SinglePoseEstimator>());
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

void FallEndToEndStatusTest::writesLatencyMarkersForFirstFrameAndFirstClassification() {
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_latency.sock"));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString markerPath = tempDir.filePath(QStringLiteral("fall-latency.jsonl"));
    qputenv("RK_FALL_LATENCY_MARKER_PATH", markerPath.toUtf8());

    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_latency.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_latency.sock")));

    FallDaemonApp app(std::make_unique<SinglePoseEstimator>());
    QVERIFY(app.start());

    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    streamJpegAnalysisFrames(analysisSocket, 1, 45);

    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(markerPath), 2000);
    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray content = marker.readAll();
    QVERIFY(content.contains("first_analysis_frame"));
    QVERIFY(content.contains("first_classification"));

    qunsetenv("RK_FALL_LATENCY_MARKER_PATH");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
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

    streamJpegAnalysisFrames(analysisSocket, 1, 45);

    const QJsonObject classificationMessage =
        waitForMessageType(&subscriber, QStringLiteral("classification"));

    QCOMPARE(classificationMessage.value(QStringLiteral("type")).toString(), QStringLiteral("classification"));
    QVERIFY(!classificationMessage.value(QStringLiteral("state")).toString().isEmpty());

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}

void FallEndToEndStatusTest::streamsClassificationBatchForSinglePerson() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e_single_batch.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e_single_batch.sock")));

    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_single_batch.sock"));
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e_single_batch.sock"));

    FallDaemonApp app(std::make_unique<SinglePoseEstimator>());
    QVERIFY(app.start());
    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    QLocalSocket subscriber;
    subscriber.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_single_batch.sock"));
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();
    QTest::qWait(50);

    streamJpegAnalysisFrames(analysisSocket, 1, 45);

    const QJsonObject batchMessage =
        waitForMessageType(&subscriber, QStringLiteral("classification_batch"));

    QCOMPARE(batchMessage.value(QStringLiteral("person_count")).toInt(), 1);
    const QJsonArray results = batchMessage.value(QStringLiteral("results")).toArray();
    QCOMPARE(results.size(), 1);
    const QJsonObject first = results.first().toObject();
    QVERIFY(first.contains(QStringLiteral("track_id")));
    QVERIFY(first.contains(QStringLiteral("icon_id")));
    QVERIFY(first.contains(QStringLiteral("anchor_x")));
    QVERIFY(first.contains(QStringLiteral("anchor_y")));
    QVERIFY(first.contains(QStringLiteral("bbox_x")));
    QVERIFY(first.contains(QStringLiteral("bbox_h")));

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}

void FallEndToEndStatusTest::streamsClassificationBatchForMultiplePeople() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e_multi.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e_multi.sock")));

    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_multi.sock"));
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e_multi.sock"));

    FallDaemonApp app(std::make_unique<MultiPoseEstimatorStub>());
    QVERIFY(app.start());
    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    QLocalSocket subscriber;
    subscriber.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_multi.sock"));
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();
    QTest::qWait(50);

    streamJpegAnalysisFrames(analysisSocket, 1, 45);

    QTRY_VERIFY_WITH_TIMEOUT(subscriber.bytesAvailable() > 0 || subscriber.waitForReadyRead(50), 2000);
    const QJsonObject json = QJsonDocument::fromJson(subscriber.readAll().trimmed()).object();
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification_batch"));
    QVERIFY(json.value(QStringLiteral("person_count")).toInt() >= 2);
    const QJsonArray results = json.value(QStringLiteral("results")).toArray();
    QVERIFY(results.size() >= 2);
    const QJsonObject first = results.first().toObject();
    QVERIFY(first.contains(QStringLiteral("track_id")));
    QVERIFY(first.contains(QStringLiteral("icon_id")));
    QVERIFY(first.contains(QStringLiteral("anchor_x")));
    QVERIFY(first.contains(QStringLiteral("anchor_y")));
    QVERIFY(first.contains(QStringLiteral("bbox_x")));
    QVERIFY(first.contains(QStringLiteral("bbox_h")));

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
}

void FallEndToEndStatusTest::keepsBatchOrderLeftToRightAfterCrossing() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e_cross.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e_cross.sock")));

    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_cross.sock"));
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e_cross.sock"));
    qputenv("RK_FALL_ACTION_BACKEND", QByteArray("rule_based"));

    FallDaemonApp app(std::make_unique<CrossingPoseEstimatorStub>());
    QVERIFY(app.start());
    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    QLocalSocket subscriber;
    subscriber.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_cross.sock"));
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();
    QTest::qWait(50);

    streamJpegAnalysisFrames(analysisSocket, 1, 50);

    QLocalSocket statusSocket;
    statusSocket.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_cross.sock"));
    QVERIFY(statusSocket.waitForConnected(2000));
    statusSocket.write("{\"action\":\"get_runtime_status\"}\n");
    statusSocket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(statusSocket.bytesAvailable() > 0 || statusSocket.waitForReadyRead(50), 2000);
    statusSocket.readAll();

    QByteArray buffer;
    bool sawOrderedCrossingBatch = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const QJsonObject message = readJsonMessage(&subscriber, &buffer);
        if (message.value(QStringLiteral("type")).toString() == QStringLiteral("classification_batch")) {
            const QJsonArray results = message.value(QStringLiteral("results")).toArray();
            if (results.size() == 2
                && results.at(0).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("monitoring")
                && results.at(1).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("lie")) {
                sawOrderedCrossingBatch = true;
                break;
            }
        }
    }

    QVERIFY(sawOrderedCrossingBatch);

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
    qunsetenv("RK_FALL_ACTION_BACKEND");
}

void FallEndToEndStatusTest::isolatesFallConfirmationPerTrackedPerson() {
    QLocalServer analysisServer;
    QLocalServer::removeServer(QStringLiteral("/tmp/rk_video_analysis_e2e_isolated.sock"));
    QVERIFY(analysisServer.listen(QStringLiteral("/tmp/rk_video_analysis_e2e_isolated.sock")));

    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_e2e_isolated.sock"));
    qputenv("RK_VIDEO_ANALYSIS_SOCKET_PATH", QByteArray("/tmp/rk_video_analysis_e2e_isolated.sock"));
    qputenv("RK_FALL_ACTION_BACKEND", QByteArray("rule_based"));

    FallDaemonApp app(std::make_unique<MixedStatePoseEstimatorStub>());
    QVERIFY(app.start());
    QVERIFY(analysisServer.waitForNewConnection(2000));
    QLocalSocket *analysisSocket = analysisServer.nextPendingConnection();
    QVERIFY(analysisSocket != nullptr);

    QLocalSocket subscriber;
    subscriber.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_isolated.sock"));
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write("{\"action\":\"subscribe_classification\"}\n");
    subscriber.flush();
    QTest::qWait(50);

    streamJpegAnalysisFrames(analysisSocket, 1, 50);

    QLocalSocket statusSocket;
    statusSocket.connectToServer(QStringLiteral("/tmp/rk_fall_e2e_isolated.sock"));
    QVERIFY(statusSocket.waitForConnected(2000));
    statusSocket.write("{\"action\":\"get_runtime_status\"}\n");
    statusSocket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(statusSocket.bytesAvailable() > 0 || statusSocket.waitForReadyRead(50), 2000);
    statusSocket.readAll();

    bool sawFallEvent = false;
    bool sawMixedBatch = false;
    QByteArray buffer;
    for (int attempt = 0; attempt < 30; ++attempt) {
        const QJsonObject message = readJsonMessage(&subscriber, &buffer);
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("fall_event")) {
            sawFallEvent = true;
        }
        if (type == QStringLiteral("classification_batch")) {
            const QJsonArray results = message.value(QStringLiteral("results")).toArray();
            if (results.size() == 2
                && results.at(0).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("lie")
                && results.at(1).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("monitoring")) {
                sawMixedBatch = true;
            }
        }
    }

    QVERIFY(sawMixedBatch);
    QVERIFY(sawFallEvent);

    qunsetenv("RK_FALL_SOCKET_NAME");
    qunsetenv("RK_VIDEO_ANALYSIS_SOCKET_PATH");
    qunsetenv("RK_FALL_ACTION_BACKEND");
}

QTEST_MAIN(FallEndToEndStatusTest)
#include "fall_end_to_end_status_test.moc"
