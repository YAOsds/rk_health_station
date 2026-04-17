#include "action/rule_based_action_classifier.h"

#include <QtTest/QTest>

namespace {
PosePerson makePose(float shoulderY, float hipY, float shoulderHalfWidth = 10.0f, float hipHalfWidth = 5.0f) {
    PosePerson person;
    person.score = 0.95f;
    person.keypoints.resize(17);

    auto setKeypoint = [&person](int index, float x, float y, float conf = 1.0f) {
        PoseKeypoint keypoint;
        keypoint.x = x;
        keypoint.y = y;
        keypoint.score = conf;
        person.keypoints[index] = keypoint;
    };

    setKeypoint(5, 50.0f - shoulderHalfWidth, shoulderY);
    setKeypoint(6, 50.0f + shoulderHalfWidth, shoulderY);
    setKeypoint(11, 50.0f - hipHalfWidth, hipY);
    setKeypoint(12, 50.0f + hipHalfWidth, hipY);
    return person;
}

QVector<PosePerson> makeSyntheticFallSequence() {
    QVector<PosePerson> sequence;
    sequence.reserve(45);
    for (int frame = 0; frame < 30; ++frame) {
        sequence.push_back(makePose(40.0f, 80.0f, 4.0f, 4.0f));
    }
    for (int frame = 30; frame < 45; ++frame) {
        sequence.push_back(makePose(118.0f, 124.0f, 24.0f, 4.0f));
    }
    return sequence;
}
}

class RuleBasedActionClassifierTest : public QObject {
    Q_OBJECT

private slots:
    void returnsMonitoringWhenWindowIsEmpty();
    void emitsFallWhenHipDropsAndTorsoTurnsFlat();
};

void RuleBasedActionClassifierTest::returnsMonitoringWhenWindowIsEmpty() {
    RuleBasedActionClassifier classifier;
    QString error;
    const ActionClassification result = classifier.classify({}, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(result.label, QStringLiteral("monitoring"));
}

void RuleBasedActionClassifierTest::emitsFallWhenHipDropsAndTorsoTurnsFlat() {
    RuleBasedActionClassifier classifier;
    QString error;
    const QVector<PosePerson> sequence = makeSyntheticFallSequence();
    const ActionClassification result = classifier.classify(sequence, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(result.label == QStringLiteral("fall") || result.label == QStringLiteral("lie"));
}

QTEST_MAIN(RuleBasedActionClassifierTest)
#include "rule_based_action_classifier_test.moc"
