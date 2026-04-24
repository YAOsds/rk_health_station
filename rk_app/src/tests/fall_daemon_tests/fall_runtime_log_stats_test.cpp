#include "debug/fall_runtime_log_stats.h"

#include <QtTest/QTest>

class FallRuntimeLogStatsTest : public QObject {
    Q_OBJECT

private slots:
    void doesNotEmitSummaryBeforeInterval();
    void emitsSummaryAfterInterval();
    void tracksPeopleEmptyAndBatchCounts();
};

void FallRuntimeLogStatsTest::doesNotEmitSummaryBeforeInterval() {
    FallRuntimeLogStats stats(5000);
    stats.onFrameIngested(1000);
    stats.onInferenceComplete(1000, true, false, 30.0);
    QVERIFY(!stats.takeSummaryIfDue(
        QStringLiteral("front_cam"), QStringLiteral("stand"), 0.9, QString(), 4999).has_value());
}

void FallRuntimeLogStatsTest::emitsSummaryAfterInterval() {
    FallRuntimeLogStats stats(5000);
    for (int index = 0; index < 6; ++index) {
        const qint64 nowMs = 1000 + (index * 200);
        stats.onFrameIngested(nowMs);
        stats.onInferenceComplete(nowMs, true, true, 28.0);
    }

    const auto summary = stats.takeSummaryIfDue(
        QStringLiteral("front_cam"), QStringLiteral("stand"), 0.95, QString(), 7000);
    QVERIFY(summary.has_value());
    QVERIFY(summary->ingestFps > 0.0);
    QVERIFY(summary->inferFps > 0.0);
    QCOMPARE(summary->peopleFrames, 6);
    QCOMPARE(summary->nonEmptyBatchCount, 6);
}

void FallRuntimeLogStatsTest::tracksPeopleEmptyAndBatchCounts() {
    FallRuntimeLogStats stats(5000);
    stats.onFrameIngested(1000);
    stats.onInferenceComplete(1000, false, false, 25.0);
    stats.onFrameIngested(2000);
    stats.onInferenceComplete(2000, true, true, 35.0);

    const auto summary = stats.takeSummaryIfDue(
        QStringLiteral("front_cam"), QStringLiteral("monitoring"), 0.0, QString(), 7000);
    QVERIFY(summary.has_value());
    QCOMPARE(summary->peopleFrames, 1);
    QCOMPARE(summary->emptyFrames, 1);
    QCOMPARE(summary->nonEmptyBatchCount, 1);
    QCOMPARE(summary->avgInferMs, 30.0);
}

QTEST_MAIN(FallRuntimeLogStatsTest)
#include "fall_runtime_log_stats_test.moc"
