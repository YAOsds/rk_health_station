#include "pose/nv12_preprocessor.h"

#include <QtTest/QTest>

class RgbPoseFastPathTest : public QObject {
    Q_OBJECT

private slots:
    void detectsModelSizedRgbFrame();
    void letterboxesRgbFrameWhenResizeIsRequired();
    void rejectsRgbFrameWithWrongPayloadSize();
    void preservesPoseMetadataForModelSizedRgbFrame();
};

void RgbPoseFastPathTest::detectsModelSizedRgbFrame() {
    AnalysisFramePacket frame;
    frame.cameraId = QStringLiteral("front_cam");
    frame.width = 4;
    frame.height = 4;
    frame.pixelFormat = AnalysisPixelFormat::Rgb;
    frame.payload = QByteArray(4 * 4 * 3, '\x22');

    QString error;
    QVERIFY(canUseRgbPoseFastPath(frame, 4, 4, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void RgbPoseFastPathTest::letterboxesRgbFrameWhenResizeIsRequired() {
    AnalysisFramePacket frame;
    frame.cameraId = QStringLiteral("front_cam");
    frame.width = 4;
    frame.height = 2;
    frame.pixelFormat = AnalysisPixelFormat::Rgb;
    frame.payload = QByteArray(4 * 2 * 3, '\x7f');

    QString error;
    QVERIFY(!canUseRgbPoseFastPath(frame, 4, 4, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const PosePreprocessResult result = preprocessRgbFrameForPose(frame, 4, 4, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.packedRgb.size(), 4 * 4 * 3);
    QCOMPARE(result.xPad, 0);
    QCOMPARE(result.yPad, 1);
    QCOMPARE(result.scale, 1.0f);
}

void RgbPoseFastPathTest::preservesPoseMetadataForModelSizedRgbFrame() {
    AnalysisFramePacket frame;
    frame.cameraId = QStringLiteral("front_cam");
    frame.width = 4;
    frame.height = 4;
    frame.pixelFormat = AnalysisPixelFormat::Rgb;
    frame.posePreprocessed = true;
    frame.poseXPad = 0;
    frame.poseYPad = 1;
    frame.poseScale = 0.75f;
    frame.payload = QByteArray(4 * 4 * 3, '\x22');

    QString error;
    QVERIFY(canUseRgbPoseFastPath(frame, 4, 4, &error));
    const PosePreprocessResult result = preprocessRgbFrameForPose(frame, 4, 4, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.xPad, 0);
    QCOMPARE(result.yPad, 1);
    QCOMPARE(result.scale, 0.75f);
}

void RgbPoseFastPathTest::rejectsRgbFrameWithWrongPayloadSize() {
    AnalysisFramePacket frame;
    frame.cameraId = QStringLiteral("front_cam");
    frame.width = 4;
    frame.height = 4;
    frame.pixelFormat = AnalysisPixelFormat::Rgb;
    frame.payload = QByteArray(4 * 4 * 3 - 1, '\x11');

    QString error;
    QVERIFY(!canUseRgbPoseFastPath(frame, 4, 4, &error));
    QCOMPARE(error, QStringLiteral("pose_input_invalid_rgb_payload"));
}

QTEST_MAIN(RgbPoseFastPathTest)
#include "rgb_pose_fast_path_test.moc"
