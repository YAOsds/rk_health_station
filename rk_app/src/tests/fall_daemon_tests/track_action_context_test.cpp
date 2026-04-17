#include "domain/fall_event_policy.h"
#include "tracking/track_action_context.h"

#include <QtTest/QTest>

namespace {
PosePerson makePose() {
    PosePerson person;
    person.box = QRectF(10, 10, 40, 80);
    person.score = 0.95f;
    person.keypoints.resize(17);
    for (PoseKeypoint &keypoint : person.keypoints) {
        keypoint.score = 0.9f;
    }
    return person;
}
}

class TrackActionContextTest : public QObject {
    Q_OBJECT

private slots:
    void sequencePushesOnlyWhenMatched();
    void fallConfirmationDoesNotLeakAcrossTracks();
};

void TrackActionContextTest::sequencePushesOnlyWhenMatched() {
    TrackActionContext context(3);
    const PosePerson pose = makePose();

    context.onMatched(pose);
    context.onLost();
    context.onMatched(pose);

    QCOMPARE(context.sequence.values().size(), 2);
}

void TrackActionContextTest::fallConfirmationDoesNotLeakAcrossTracks() {
    FallEventPolicy left;
    FallEventPolicy right;

    QVERIFY(!left.update(QStringLiteral("fall"), 0.9).has_value());
    QVERIFY(!left.update(QStringLiteral("fall"), 0.9).has_value());
    QVERIFY(!right.update(QStringLiteral("stand"), 0.9).has_value());
    QVERIFY(left.update(QStringLiteral("fall"), 0.9).has_value());
    QVERIFY(!right.update(QStringLiteral("fall"), 0.9).has_value());
}

QTEST_MAIN(TrackActionContextTest)
#include "track_action_context_test.moc"
