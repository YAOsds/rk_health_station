#include "action/action_classifier_factory.h"
#include "runtime/runtime_config.h"

#include <QtTest/QTest>

class ActionClassifierFactoryTest : public QObject {
    Q_OBJECT

private slots:
    void defaultsToLstmRknn();
    void parsesRuleBackendFromEnv();
    void selectsLstmModelPathForDefaultBackend();
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

QTEST_MAIN(ActionClassifierFactoryTest)
#include "action_classifier_factory_test.moc"
