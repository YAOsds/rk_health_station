#include "widgets/video_preview_widget.h"

#include <QtTest/QTest>

class VideoPreviewWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void rendersFallOverlayText();
    void rendersNoPersonOverlayText();
};

void VideoPreviewWidgetTest::rendersFallOverlayText() {
    VideoPreviewWidget widget;
    widget.setClassificationOverlay(
        QStringLiteral("fall 0.93"),
        VideoPreviewWidget::OverlaySeverity::Alert);
    QCOMPARE(widget.classificationText(), QStringLiteral("fall 0.93"));
}

void VideoPreviewWidgetTest::rendersNoPersonOverlayText() {
    VideoPreviewWidget widget;
    widget.setClassificationOverlay(
        QStringLiteral("no person"),
        VideoPreviewWidget::OverlaySeverity::Muted);
    QCOMPARE(widget.classificationText(), QStringLiteral("no person"));
}

QTEST_MAIN(VideoPreviewWidgetTest)
#include "video_preview_widget_test.moc"
