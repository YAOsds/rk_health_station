#include "protocol/fall_ipc.h"

#include <QtTest/QTest>

class FallProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void roundTripsRuntimeStatusJson();
    void roundTripsClassificationJson();
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

QTEST_MAIN(FallProtocolTest)
#include "fall_protocol_test.moc"
