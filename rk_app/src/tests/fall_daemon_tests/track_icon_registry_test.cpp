#include "tracking/track_icon_registry.h"

#include <QtTest/QTest>

class TrackIconRegistryTest : public QObject {
    Q_OBJECT

private slots:
    void keepsStableIconIdsForLiveTracks();
};

void TrackIconRegistryTest::keepsStableIconIdsForLiveTracks() {
    TrackIconRegistry registry(5);

    QCOMPARE(registry.iconIdForTrack(10), 1);
    QCOMPARE(registry.iconIdForTrack(20), 2);
    QCOMPARE(registry.iconIdForTrack(10), 1);

    registry.reconcileActiveTracks({20, 30});
    QCOMPARE(registry.iconIdForTrack(20), 2);
    QCOMPARE(registry.iconIdForTrack(30), 1);
}

QTEST_MAIN(TrackIconRegistryTest)
#include "track_icon_registry_test.moc"
