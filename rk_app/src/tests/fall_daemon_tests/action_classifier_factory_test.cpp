#include "action/action_classifier_factory.h"
#include "runtime/runtime_config.h"
#include "runtime_config/app_runtime_config.h"


#include <QtTest/QTest>

class ActionClassifierFactoryTest : public QObject {
    Q_OBJECT

private slots:
    void mapsDefaultBackendFromAppRuntimeConfig();
    void mapsRuleBackendFromAppRuntimeConfig();
    void selectsLstmModelPathForDefaultBackend();
    void mapsByteTrackTuningFromAppRuntimeConfig();
};

void ActionClassifierFactoryTest::mapsDefaultBackendFromAppRuntimeConfig() {
    const AppRuntimeConfig appConfig = buildDefaultAppRuntimeConfig();
    const FallRuntimeConfig config = loadFallRuntimeConfig(appConfig);
    QCOMPARE(config.actionBackend, ActionBackendKind::LstmRknn);
}

void ActionClassifierFactoryTest::mapsRuleBackendFromAppRuntimeConfig() {
    AppRuntimeConfig appConfig = buildDefaultAppRuntimeConfig();
    appConfig.fallDetection.actionBackend = QStringLiteral("rule_based");

    const FallRuntimeConfig config = loadFallRuntimeConfig(appConfig);
    QCOMPARE(config.actionBackend, ActionBackendKind::RuleBased);
}

void ActionClassifierFactoryTest::selectsLstmModelPathForDefaultBackend() {
    const AppRuntimeConfig appConfig = buildDefaultAppRuntimeConfig();
    const FallRuntimeConfig config = loadFallRuntimeConfig(appConfig);
    QCOMPARE(actionModelPathForConfig(config), config.lstmModelPath);
}

void ActionClassifierFactoryTest::mapsByteTrackTuningFromAppRuntimeConfig() {
    AppRuntimeConfig appConfig = buildDefaultAppRuntimeConfig();
    appConfig.fallDetection.sequenceLength = 60;
    appConfig.fallDetection.trackHighThresh = 0.35;
    appConfig.fallDetection.trackLowThresh = 0.10;
    appConfig.fallDetection.newTrackThresh = 0.45;
    appConfig.fallDetection.matchThresh = 0.80;
    appConfig.fallDetection.lostTimeoutMs = 800;
    appConfig.fallDetection.maxTracks = 5;
    appConfig.fallDetection.minValidKeypoints = 8;
    appConfig.fallDetection.minBoxArea = 4096.0;

    const FallRuntimeConfig config = loadFallRuntimeConfig(appConfig);
    QCOMPARE(config.sequenceLength, 60);
    QCOMPARE(config.trackHighThresh, 0.35);
    QCOMPARE(config.trackLowThresh, 0.10);
    QCOMPARE(config.newTrackThresh, 0.45);
    QCOMPARE(config.matchThresh, 0.80);
    QCOMPARE(config.lostTimeoutMs, 800);
    QCOMPARE(config.maxTracks, 5);
    QCOMPARE(config.minValidKeypoints, 8);
    QCOMPARE(config.minBoxArea, 4096.0);
}

QTEST_MAIN(ActionClassifierFactoryTest)
#include "action_classifier_factory_test.moc"
