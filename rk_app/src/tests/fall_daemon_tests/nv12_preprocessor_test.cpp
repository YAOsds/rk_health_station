#include "pose/nv12_preprocessor.h"

#include <QtTest/QTest>

class Nv12PreprocessorTest : public QObject {
    Q_OBJECT

private slots:
    void letterboxesNv12FrameIntoRgbTensor();
};

void Nv12PreprocessorTest::letterboxesNv12FrameIntoRgbTensor() {
    AnalysisFramePacket frame;
    frame.cameraId = QStringLiteral("front_cam");
    frame.width = 4;
    frame.height = 2;
    frame.pixelFormat = AnalysisPixelFormat::Nv12;
    frame.payload = QByteArray::fromHex("C8C8C8C8C8C8C8C880808080");

    QString error;
    const PosePreprocessResult result = preprocessNv12ForPose(frame, 4, 4, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.packedRgb.size(), 4 * 4 * 3);
    QCOMPARE(result.xPad, 0);
    QCOMPARE(result.yPad, 1);
    QCOMPARE(result.scale, 1.0f);

    for (int i = 0; i < 4 * 3; ++i) {
        QCOMPARE(static_cast<unsigned char>(result.packedRgb.at(i)), static_cast<unsigned char>(114));
    }

    const int firstImageRowOffset = 4 * 3;
    QVERIFY(static_cast<unsigned char>(result.packedRgb.at(firstImageRowOffset)) > 114);
}

QTEST_MAIN(Nv12PreprocessorTest)
#include "nv12_preprocessor_test.moc"
