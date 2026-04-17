#include "widgets/video_preview_widget.h"

#include <QtTest/QTest>

class VideoPreviewWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void rendersMultipleClassificationRows();
    void rendersNoPersonRow();
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

QTEST_MAIN(VideoPreviewWidgetTest)
#include "video_preview_widget_test.moc"
