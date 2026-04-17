#include "action/target_selector.h"

#include <QtTest/QTest>

class TargetSelectorTest : public QObject {
    Q_OBJECT

private slots:
    void picksHighestScorePerson();
};

void TargetSelectorTest::picksHighestScorePerson() {
    PosePerson low;
    low.score = 0.25f;

    PosePerson high;
    high.score = 0.90f;

    PosePerson mid;
    mid.score = 0.50f;

    TargetSelector selector;
    const PosePerson selected = selector.selectPrimary({low, high, mid});
    QCOMPARE(selected.score, high.score);
}

QTEST_MAIN(TargetSelectorTest)
#include "target_selector_test.moc"
