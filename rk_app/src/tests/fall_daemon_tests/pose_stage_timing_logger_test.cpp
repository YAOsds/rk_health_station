#include "pose/pose_stage_timing_logger.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QTest>

class PoseStageTimingLoggerTest : public QObject {
    Q_OBJECT

private slots:
    void writesStructuredStageDurations();
    void writesRgbPixelFormatName();
};

void PoseStageTimingLoggerTest::writesStructuredStageDurations() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString tracePath = tempDir.filePath(QStringLiteral("pose-stage-timing.jsonl"));
    PoseStageTimingLogger logger(tracePath);

    AnalysisFramePacket frame;
    frame.frameId = 9;
    frame.cameraId = QStringLiteral("front_cam");
    frame.pixelFormat = AnalysisPixelFormat::Nv12;
    frame.width = 640;
    frame.height = 480;
    frame.timestampMs = 1777000000123;

    PoseStageTimingSample sample;
    sample.preprocessMs = 12;
    sample.inputsSetMs = 3;
    sample.rknnRunMs = 27;
    sample.outputsGetMs = 4;
    sample.postProcessMs = 6;
    sample.totalMs = 52;
    sample.peopleCount = 1;

    logger.appendSample(frame, sample);

    QFile file(tracePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray line = file.readLine().trimmed();
    QVERIFY(!line.isEmpty());

    const QJsonObject json = QJsonDocument::fromJson(line).object();
    QCOMPARE(static_cast<qint64>(json.value(QStringLiteral("frame_id")).toDouble()), 9LL);
    QCOMPARE(json.value(QStringLiteral("camera_id")).toString(), QStringLiteral("front_cam"));
    QCOMPARE(json.value(QStringLiteral("pixel_format")).toString(), QStringLiteral("nv12"));
    QCOMPARE(json.value(QStringLiteral("preprocess_ms")).toInt(), 12);
    QCOMPARE(json.value(QStringLiteral("inputs_set_ms")).toInt(), 3);
    QCOMPARE(json.value(QStringLiteral("rknn_run_ms")).toInt(), 27);
    QCOMPARE(json.value(QStringLiteral("outputs_get_ms")).toInt(), 4);
    QCOMPARE(json.value(QStringLiteral("post_process_ms")).toInt(), 6);
    QCOMPARE(json.value(QStringLiteral("total_ms")).toInt(), 52);
    QCOMPARE(json.value(QStringLiteral("people_count")).toInt(), 1);
}

void PoseStageTimingLoggerTest::writesRgbPixelFormatName() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString tracePath = tempDir.filePath(QStringLiteral("pose-stage-timing-rgb.jsonl"));
    PoseStageTimingLogger logger(tracePath);

    AnalysisFramePacket frame;
    frame.frameId = 10;
    frame.cameraId = QStringLiteral("front_cam");
    frame.pixelFormat = AnalysisPixelFormat::Rgb;
    frame.width = 640;
    frame.height = 640;
    frame.timestampMs = 1777000000999;

    PoseStageTimingSample sample;
    sample.totalMs = 20;

    logger.appendSample(frame, sample);

    QFile file(tracePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray line = file.readLine().trimmed();
    QVERIFY(!line.isEmpty());

    const QJsonObject json = QJsonDocument::fromJson(line).object();
    QCOMPARE(json.value(QStringLiteral("pixel_format")).toString(), QStringLiteral("rgb"));
}

QTEST_MAIN(PoseStageTimingLoggerTest)
#include "pose_stage_timing_logger_test.moc"
