#include "tracking/association.h"

#include <QtTest/QTest>

class AssociationTest : public QObject {
    Q_OBJECT

private slots:
    void computesIoUCost();
    void rejectsMatchesThatMoveTooFar();
    void prefersHigherIoUPairing();
};

void AssociationTest::computesIoUCost() {
    QCOMPARE(iou(QRectF(0, 0, 100, 100), QRectF(0, 0, 100, 100)), 1.0);
}

void AssociationTest::rejectsMatchesThatMoveTooFar() {
    AssociationConfig config;
    config.matchThresh = 0.8;
    config.maxCenterDistanceFactor = 1.5;

    QVERIFY(!passesMotionGate(QRectF(0, 0, 100, 200), QRectF(400, 0, 100, 200), config));
}

void AssociationTest::prefersHigherIoUPairing() {
    QVector<QRectF> tracks{QRectF(0, 0, 100, 200), QRectF(300, 0, 100, 200)};
    QVector<QRectF> detections{QRectF(5, 0, 100, 200), QRectF(295, 0, 100, 200)};
    const auto pairs = greedyAssociate(tracks, detections, AssociationConfig());
    QCOMPARE(pairs.size(), 2);
    QCOMPARE(pairs.at(0).trackIndex, 0);
    QCOMPARE(pairs.at(0).detectionIndex, 0);
    QCOMPARE(pairs.at(1).trackIndex, 1);
    QCOMPARE(pairs.at(1).detectionIndex, 1);
}

QTEST_MAIN(AssociationTest)
#include "association_test.moc"
