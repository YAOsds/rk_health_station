#include "tracking/track_manager.h"

#include <QtTest/QTest>

namespace {
PosePerson makePerson(float left, float top, float width, float height, float score = 0.9f) {
    PosePerson person;
    person.box = QRectF(left, top, width, height);
    person.score = score;
    person.keypoints.fill(PoseKeypoint(), 17);
    return person;
}
}

class TrackManagerTest : public QObject {
    Q_OBJECT

private slots:
    void assignsTwoStableTracks();
    void dropsTrackAfterMissThreshold();
    void limitsActiveTracksToFive();
};

void TrackManagerTest::assignsTwoStableTracks() {
    TrackManager manager(5, 10);

    const auto firstFrame = manager.update({
        makePerson(10, 10, 40, 80),
        makePerson(200, 10, 40, 80)
    }, 1000);
    QCOMPARE(firstFrame.size(), 2);

    const int leftId = firstFrame.at(0).trackId;
    const int rightId = firstFrame.at(1).trackId;

    const auto secondFrame = manager.update({
        makePerson(14, 12, 40, 80),
        makePerson(204, 12, 40, 80)
    }, 1033);
    QCOMPARE(secondFrame.size(), 2);
    QCOMPARE(secondFrame.at(0).trackId, leftId);
    QCOMPARE(secondFrame.at(1).trackId, rightId);
}

void TrackManagerTest::dropsTrackAfterMissThreshold() {
    TrackManager manager(5, 2);
    QCOMPARE(manager.update({makePerson(10, 10, 40, 80)}, 1000).size(), 1);
    QCOMPARE(manager.update({}, 1033).size(), 1);
    QCOMPARE(manager.update({}, 1066).size(), 1);
    QCOMPARE(manager.update({}, 1099).size(), 0);
}

void TrackManagerTest::limitsActiveTracksToFive() {
    TrackManager manager(5, 10);
    QVector<PosePerson> people;
    for (int i = 0; i < 7; ++i) {
        people.push_back(makePerson(float(i * 60), 10, 40, 80, 0.95f - 0.01f * i));
    }

    const auto tracks = manager.update(people, 1000);
    QCOMPARE(tracks.size(), 5);
}

QTEST_MAIN(TrackManagerTest)
#include "track_manager_test.moc"
