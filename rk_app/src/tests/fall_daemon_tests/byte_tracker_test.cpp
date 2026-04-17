#include "tracking/byte_tracker.h"

#include <QtTest/QTest>

namespace {
PosePerson makePerson(float left, float top, float width, float height, float score = 0.9f) {
    PosePerson person;
    person.box = QRectF(left, top, width, height);
    person.score = score;
    person.keypoints.resize(17);
    for (PoseKeypoint &keypoint : person.keypoints) {
        keypoint.score = 0.9f;
    }
    return person;
}

FallRuntimeConfig makeConfig() {
    FallRuntimeConfig config;
    config.maxTracks = 5;
    config.sequenceLength = 45;
    config.trackHighThresh = 0.35;
    config.trackLowThresh = 0.10;
    config.newTrackThresh = 0.45;
    config.matchThresh = 0.80;
    config.lostTimeoutMs = 800;
    config.minValidKeypoints = 8;
    config.minBoxArea = 1024.0;
    return config;
}
}

class ByteTrackerTest : public QObject {
    Q_OBJECT

private slots:
    void keepsStableIdsAcrossSmallMotion();
    void fillsActionSequenceAcrossSteadyUpdates();
    void fillsTwoSequencesForSeparatedPeople();
    void keepsTwoTracksAliveThroughCrossing();
    void usesLowScoreDetectionsToRecoverLostTrack();
    void removesTrackAfterTimeout();
    void sortsOutputLeftToRight();
    void limitsTracksToFive();
};

void ByteTrackerTest::keepsStableIdsAcrossSmallMotion() {
    ByteTracker tracker(makeConfig());

    const auto first = tracker.update({makePerson(10, 10, 40, 80), makePerson(200, 10, 40, 80)}, 1000);
    QCOMPARE(first.size(), 2);

    const auto second = tracker.update({makePerson(14, 12, 40, 80), makePerson(204, 12, 40, 80)}, 1033);
    QCOMPARE(second.size(), 2);
    QCOMPARE(second.at(0).trackId, first.at(0).trackId);
    QCOMPARE(second.at(1).trackId, first.at(1).trackId);
}

void ByteTrackerTest::fillsActionSequenceAcrossSteadyUpdates() {
    ByteTracker tracker(makeConfig());

    QVector<TrackedPerson> tracks;
    for (int frame = 0; frame < 45; ++frame) {
        tracks = tracker.update({makePerson(10, 10, 40, 80)}, 1000 + (frame * 33));
    }

    QCOMPARE(tracks.size(), 1);
    QVERIFY(tracks.first().action.sequence.isFull());
}

void ByteTrackerTest::fillsTwoSequencesForSeparatedPeople() {
    ByteTracker tracker(makeConfig());

    QVector<TrackedPerson> tracks;
    for (int frame = 0; frame < 45; ++frame) {
        tracks = tracker.update(
            {makePerson(10, 10, 40, 80), makePerson(200, 10, 40, 80)}, 1000 + (frame * 33));
    }

    QCOMPARE(tracks.size(), 2);
    QVERIFY(tracks.at(0).action.sequence.isFull());
    QVERIFY(tracks.at(1).action.sequence.isFull());
}

void ByteTrackerTest::keepsTwoTracksAliveThroughCrossing() {
    ByteTracker tracker(makeConfig());

    QVector<TrackedPerson> tracks;
    for (int frame = 0; frame < 50; ++frame) {
        const float leftX = 20.0f + (frame * 4.0f);
        const float rightX = 220.0f - (frame * 4.0f);
        if (frame % 2 == 0) {
            tracks = tracker.update(
                {makePerson(rightX, 10, 40, 80), makePerson(leftX, 10, 40, 80)}, 1000 + (frame * 33));
        } else {
            tracks = tracker.update(
                {makePerson(leftX, 10, 40, 80), makePerson(rightX, 10, 40, 80)}, 1000 + (frame * 33));
        }
    }

    QCOMPARE(tracks.size(), 2);
    QVERIFY(tracks.at(0).action.sequence.isFull());
    QVERIFY(tracks.at(1).action.sequence.isFull());
}

void ByteTrackerTest::usesLowScoreDetectionsToRecoverLostTrack() {
    ByteTracker tracker(makeConfig());

    const auto first = tracker.update({makePerson(10, 10, 40, 80, 0.95f)}, 1000);
    QCOMPARE(first.size(), 1);
    const int trackId = first.first().trackId;

    const auto lost = tracker.update({}, 1100);
    QCOMPARE(lost.size(), 1);
    QCOMPARE(lost.first().state, ByteTrackState::Lost);

    const auto recovered = tracker.update({makePerson(12, 10, 40, 80, 0.20f)}, 1200);
    QCOMPARE(recovered.size(), 1);
    QCOMPARE(recovered.first().trackId, trackId);
    QCOMPARE(recovered.first().state, ByteTrackState::Tracked);
}

void ByteTrackerTest::removesTrackAfterTimeout() {
    ByteTracker tracker(makeConfig());

    QCOMPARE(tracker.update({makePerson(10, 10, 40, 80)}, 1000).size(), 1);
    QCOMPARE(tracker.update({}, 1500).size(), 1);
    QCOMPARE(tracker.update({}, 1901).size(), 0);
}

void ByteTrackerTest::sortsOutputLeftToRight() {
    ByteTracker tracker(makeConfig());

    const auto tracks = tracker.update({makePerson(200, 10, 40, 80), makePerson(10, 10, 40, 80)}, 1000);
    QCOMPARE(tracks.size(), 2);
    QVERIFY(tracks.at(0).latestPose.box.center().x() < tracks.at(1).latestPose.box.center().x());
}

void ByteTrackerTest::limitsTracksToFive() {
    ByteTracker tracker(makeConfig());
    QVector<PosePerson> people;
    for (int index = 0; index < 7; ++index) {
        people.push_back(makePerson(float(index * 60), 10, 40, 80, 0.95f - 0.01f * index));
    }

    const auto tracks = tracker.update(people, 1000);
    QCOMPARE(tracks.size(), 5);
}

QTEST_MAIN(ByteTrackerTest)
#include "byte_tracker_test.moc"
