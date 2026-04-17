#include "protocol/fall_ipc.h"

#include <QtTest/QTest>

class FallProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsRuntimeStatusJson();
    void roundTripsClassificationJson();
    void roundTripsClassificationBatch();
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

QTEST_MAIN(FallProtocolTest)
#include "fall_protocol_test.moc"
