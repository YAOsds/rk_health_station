#include "action/action_classifier_factory.h"
#include "runtime/runtime_config.h"

#include <QtTest/QTest>

class ActionClassifierFactoryTest : public QObject {
    Q_OBJECT

private slots:
    void defaultsToLstmRknn();
    void parsesRuleBackendFromEnv();
    void selectsLstmModelPathForDefaultBackend();
    void loadsByteTrackTuningFromEnvironment();
};

void ActionClassifierFactoryTest::defaultsToLstmRknn() {
    qunsetenv("RK_FALL_ACTION_BACKEND");
    const FallRuntimeConfig config = loadFallRuntimeConfig();
    QCOMPARE(config.actionBackend, ActionBackendKind::LstmRknn);
}

void ActionClassifierFactoryTest::parsesRuleBackendFromEnv() {
    qputenv("RK_FALL_ACTION_BACKEND", QByteArray("rule_based"));
    const FallRuntimeConfig config = loadFallRuntimeConfig();
    QCOMPARE(config.actionBackend, ActionBackendKind::RuleBased);
    qunsetenv("RK_FALL_ACTION_BACKEND");
}

void ActionClassifierFactoryTest::selectsLstmModelPathForDefaultBackend() {
    qunsetenv("RK_FALL_ACTION_BACKEND");
    const FallRuntimeConfig config = loadFallRuntimeConfig();
    QCOMPARE(actionModelPathForConfig(config), config.lstmModelPath);
}

void ActionClassifierFactoryTest::loadsByteTrackTuningFromEnvironment() {
    qputenv("RK_FALL_SEQUENCE_LENGTH", QByteArray("60"));
    qputenv("RK_FALL_TRACK_HIGH_THRESH", QByteArray("0.35"));
    qputenv("RK_FALL_TRACK_LOW_THRESH", QByteArray("0.10"));
    qputenv("RK_FALL_NEW_TRACK_THRESH", QByteArray("0.45"));
    qputenv("RK_FALL_MATCH_THRESH", QByteArray("0.80"));
    qputenv("RK_FALL_LOST_TIMEOUT_MS", QByteArray("800"));
    qputenv("RK_FALL_MAX_TRACKS", QByteArray("5"));
    qputenv("RK_FALL_MIN_VALID_KEYPOINTS", QByteArray("8"));
    qputenv("RK_FALL_MIN_BOX_AREA", QByteArray("4096"));

    const FallRuntimeConfig config = loadFallRuntimeConfig();
    QCOMPARE(config.sequenceLength, 60);
    QCOMPARE(config.trackHighThresh, 0.35);
    QCOMPARE(config.trackLowThresh, 0.10);
    QCOMPARE(config.newTrackThresh, 0.45);
    QCOMPARE(config.matchThresh, 0.80);
    QCOMPARE(config.lostTimeoutMs, 800);
    QCOMPARE(config.maxTracks, 5);
    QCOMPARE(config.minValidKeypoints, 8);
    QCOMPARE(config.minBoxArea, 4096.0);

    qunsetenv("RK_FALL_SEQUENCE_LENGTH");
    qunsetenv("RK_FALL_TRACK_HIGH_THRESH");
    qunsetenv("RK_FALL_TRACK_LOW_THRESH");
    qunsetenv("RK_FALL_NEW_TRACK_THRESH");
    qunsetenv("RK_FALL_MATCH_THRESH");
    qunsetenv("RK_FALL_LOST_TIMEOUT_MS");
    qunsetenv("RK_FALL_MAX_TRACKS");
    qunsetenv("RK_FALL_MIN_VALID_KEYPOINTS");
    qunsetenv("RK_FALL_MIN_BOX_AREA");
}

QTEST_MAIN(ActionClassifierFactoryTest)
#include "action_classifier_factory_test.moc"
