#include "protocol/fall_ipc.h"

#include <QJsonArray>
#include <QtTest/QTest>

class FallProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsRuntimeStatusJson();
    void roundTripsClassificationJson();
    void roundTripsClassificationBatch();
    void roundTripsClassificationBatchOverlayMetadata();
    void decodesLegacyClassificationBatchWithoutOverlayMetadata();
};

void FallProtocolTest::roundTripsRuntimeStatusJson() {
    FallRuntimeStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.inputConnected = true;
    status.poseModelReady = true;
    status.actionModelReady = false;
    status.currentFps = 9.5;
    status.latestState = QStringLiteral("monitoring");
    status.latestConfidence = 0.75;

    const QJsonObject json = fallRuntimeStatusToJson(status);
    FallRuntimeStatus decoded;
    QVERIFY(fallRuntimeStatusFromJson(json, &decoded));
    QCOMPARE(decoded.cameraId, status.cameraId);
    QCOMPARE(decoded.inputConnected, status.inputConnected);
    QCOMPARE(decoded.latestState, status.latestState);
}

void FallProtocolTest::roundTripsClassificationJson() {
    FallClassificationResult result;
    result.cameraId = QStringLiteral("front_cam");
    result.timestampMs = 1776356876397;
    result.state = QStringLiteral("fall");
    result.confidence = 0.93;

    const QJsonObject json = fallClassificationResultToJson(result);
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification"));

    FallClassificationResult decoded;
    QVERIFY(fallClassificationResultFromJson(json, &decoded));
    QCOMPARE(decoded.cameraId, result.cameraId);
    QCOMPARE(decoded.timestampMs, result.timestampMs);
    QCOMPARE(decoded.state, result.state);
    QCOMPARE(decoded.confidence, result.confidence);
}

void FallProtocolTest::roundTripsClassificationBatch() {
    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1776367000000;

    FallClassificationEntry first;
    first.state = QStringLiteral("stand");
    first.confidence = 0.91;
    batch.results.push_back(first);

    FallClassificationEntry second;
    second.state = QStringLiteral("fall");
    second.confidence = 0.96;
    batch.results.push_back(second);

    const QJsonObject json = fallClassificationBatchToJson(batch);
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("classification_batch"));
    QCOMPARE(json.value(QStringLiteral("person_count")).toInt(), 2);

    FallClassificationBatch decoded;
    QVERIFY(fallClassificationBatchFromJson(json, &decoded));
    QCOMPARE(decoded.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(decoded.results.size(), 2);
    QCOMPARE(decoded.results.at(0).state, QStringLiteral("stand"));
    QCOMPARE(decoded.results.at(1).state, QStringLiteral("fall"));
    QCOMPARE(decoded.results.at(1).confidence, 0.96);
}

void FallProtocolTest::roundTripsClassificationBatchOverlayMetadata() {
    FallClassificationBatch batch;
    batch.cameraId = QStringLiteral("front_cam");
    batch.timestampMs = 1777000000000;

    FallClassificationEntry first;
    first.trackId = 3;
    first.iconId = 1;
    first.state = QStringLiteral("stand");
    first.confidence = 0.98;
    first.anchorX = 312.0;
    first.anchorY = 118.0;
    first.bboxX = 250.0;
    first.bboxY = 90.0;
    first.bboxW = 120.0;
    first.bboxH = 260.0;
    batch.results.push_back(first);

    const QJsonObject json = fallClassificationBatchToJson(batch);
    const QJsonArray results = json.value(QStringLiteral("results")).toArray();
    QCOMPARE(results.size(), 1);

    const QJsonObject firstJson = results.first().toObject();
    QCOMPARE(firstJson.value(QStringLiteral("track_id")).toInt(), 3);
    QCOMPARE(firstJson.value(QStringLiteral("icon_id")).toInt(), 1);
    QCOMPARE(firstJson.value(QStringLiteral("anchor_x")).toDouble(), 312.0);
    QCOMPARE(firstJson.value(QStringLiteral("anchor_y")).toDouble(), 118.0);
    QCOMPARE(firstJson.value(QStringLiteral("bbox_x")).toDouble(), 250.0);
    QCOMPARE(firstJson.value(QStringLiteral("bbox_y")).toDouble(), 90.0);
    QCOMPARE(firstJson.value(QStringLiteral("bbox_w")).toDouble(), 120.0);
    QCOMPARE(firstJson.value(QStringLiteral("bbox_h")).toDouble(), 260.0);

    FallClassificationBatch decoded;
    QVERIFY(fallClassificationBatchFromJson(json, &decoded));
    QCOMPARE(decoded.results.size(), 1);
    QCOMPARE(decoded.results.first().trackId, 3);
    QCOMPARE(decoded.results.first().iconId, 1);
    QCOMPARE(decoded.results.first().anchorX, 312.0);
    QCOMPARE(decoded.results.first().anchorY, 118.0);
    QCOMPARE(decoded.results.first().bboxW, 120.0);
    QCOMPARE(decoded.results.first().bboxH, 260.0);
}

void FallProtocolTest::decodesLegacyClassificationBatchWithoutOverlayMetadata() {
    const QJsonObject json{
        {QStringLiteral("type"), QStringLiteral("classification_batch")},
        {QStringLiteral("camera_id"), QStringLiteral("front_cam")},
        {QStringLiteral("ts"), 1777000000000LL},
        {QStringLiteral("person_count"), 1},
        {QStringLiteral("results"), QJsonArray{QJsonObject{{QStringLiteral("state"), QStringLiteral("fall")}, {QStringLiteral("confidence"), 0.95}}}}
    };

    FallClassificationBatch decoded;
    QVERIFY(fallClassificationBatchFromJson(json, &decoded));
    QCOMPARE(decoded.results.size(), 1);
    QCOMPARE(decoded.results.first().trackId, -1);
    QCOMPARE(decoded.results.first().iconId, -1);
    QCOMPARE(decoded.results.first().state, QStringLiteral("fall"));
    QCOMPARE(decoded.results.first().confidence, 0.95);
}

QTEST_MAIN(FallProtocolTest)
#include "fall_protocol_test.moc"
