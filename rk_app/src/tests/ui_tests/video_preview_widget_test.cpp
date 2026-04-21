#include "widgets/video_preview_widget.h"

#include <QtTest/QTest>

class VideoPreviewWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void rendersMultipleClassificationRows();
    void rendersNoPersonRow();
    void showsIndependentSourceBadge();
    void storesAndClearsOverlayEntries();
};

void VideoPreviewWidgetTest::rendersMultipleClassificationRows() {
    VideoPreviewWidget widget;

    QVector<VideoPreviewWidget::ClassificationOverlayRow> rows;
    rows.push_back({QStringLiteral("stand 0.91"), VideoPreviewWidget::OverlaySeverity::Normal});
    rows.push_back({QStringLiteral("fall 0.96"), VideoPreviewWidget::OverlaySeverity::Alert});

    widget.setClassificationRows(rows);

    QCOMPARE(widget.classificationRows(),
        QStringList({QStringLiteral("stand 0.91"), QStringLiteral("fall 0.96")}));
}

void VideoPreviewWidgetTest::rendersNoPersonRow() {
    VideoPreviewWidget widget;
    QVector<VideoPreviewWidget::ClassificationOverlayRow> rows;
    rows.push_back({QStringLiteral("no person"), VideoPreviewWidget::OverlaySeverity::Muted});

    widget.setClassificationRows(rows);

    QCOMPARE(widget.classificationRows(), QStringList({QStringLiteral("no person")}));
}

void VideoPreviewWidgetTest::showsIndependentSourceBadge() {
    VideoPreviewWidget widget;

    widget.setSourceBadge(QStringLiteral("TEST MODE"), QStringLiteral("fall-demo.mp4"));

    QCOMPARE(widget.sourceBadgeText(), QStringLiteral("TEST MODE\nfall-demo.mp4"));
}

void VideoPreviewWidgetTest::storesAndClearsOverlayEntries() {
    VideoPreviewWidget widget;

    VideoPreviewWidget::OverlayEntry entry;
    entry.iconId = 1;
    entry.state = QStringLiteral("fall");
    entry.confidence = 0.94;
    entry.anchor = QPointF(120.0, 80.0);
    entry.bbox = QRectF(80.0, 60.0, 100.0, 220.0);

    widget.setOverlayEntries({entry});
    QCOMPARE(widget.overlayEntries().size(), 1);
    QCOMPARE(widget.overlayEntries().first().iconId, 1);

    widget.setOverlayEntries({});
    QVERIFY(widget.overlayEntries().isEmpty());
}

QTEST_MAIN(VideoPreviewWidgetTest)
#include "video_preview_widget_test.moc"
