#include "domain/fall_event_policy.h"

#include <QtTest/QTest>

class FallEventPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void confirmsFallAfterRepeatedLieState();
};

void FallEventPolicyTest::confirmsFallAfterRepeatedLieState() {
    FallEventPolicy policy;

    QVERIFY(!policy.update(QStringLiteral("fall"), 0.90).has_value());
    QVERIFY(!policy.update(QStringLiteral("lie"), 0.93).has_value());
    const auto event = policy.update(QStringLiteral("lie"), 0.95);
    QVERIFY(event.has_value());
    QCOMPARE(event->eventType, QStringLiteral("fall_confirmed"));
}

QTEST_MAIN(FallEventPolicyTest)
#include "fall_event_policy_test.moc"
