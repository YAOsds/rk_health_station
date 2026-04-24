#include "debug/video_runtime_log_stats.h"

#include <QtTest/QTest>

class VideoRuntimeLogStatsTest : public QObject {
    Q_OBJECT

private slots:
    void doesNotEmitSummaryBeforeInterval();
    void emitsSummaryAfterIntervalWhenFramesFlow();
    void carriesDropDeltaAndLifetimeDropCount();
};

void VideoRuntimeLogStatsTest::doesNotEmitSummaryBeforeInterval() {
    VideoRuntimeLogStats stats(5000);
    stats.onDescriptorPublished(
        QStringLiteral("front_cam"), QStringLiteral("test_file"), true, 0, 1000);
    QVERIFY(!stats.takeSummaryIfDue(4999).has_value());
}

void VideoRuntimeLogStatsTest::emitsSummaryAfterIntervalWhenFramesFlow() {
    VideoRuntimeLogStats stats(5000);
    for (int index = 0; index < 10; ++index) {
        stats.onDescriptorPublished(
            QStringLiteral("front_cam"), QStringLiteral("test_file"), true, 0, 1000 + (index * 100));
    }

    const auto summary = stats.takeSummaryIfDue(6000);
    QVERIFY(summary.has_value());
    QCOMPARE(summary->cameraId, QStringLiteral("front_cam"));
    QCOMPARE(summary->inputMode, QStringLiteral("test_file"));
    QVERIFY(summary->publishFps > 0.0);
    QCOMPARE(summary->publishedFramesWindow, 10);
}

void VideoRuntimeLogStatsTest::carriesDropDeltaAndLifetimeDropCount() {
    VideoRuntimeLogStats stats(5000);
    stats.onDescriptorPublished(QStringLiteral("front_cam"), QStringLiteral("camera"), true, 3, 1000);
    stats.onDescriptorPublished(QStringLiteral("front_cam"), QStringLiteral("camera"), true, 5, 2000);

    const auto summary = stats.takeSummaryIfDue(7000);
    QVERIFY(summary.has_value());
    QCOMPARE(summary->droppedFramesTotal, quint64(5));
    QCOMPARE(summary->droppedFramesDelta, quint64(5));
}

QTEST_MAIN(VideoRuntimeLogStatsTest)
#include "video_runtime_log_stats_test.moc"
