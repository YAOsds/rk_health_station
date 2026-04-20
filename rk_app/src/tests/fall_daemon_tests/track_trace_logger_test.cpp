#include "tracking/track_trace_logger.h"

#include <QFile>
#include <QtGlobal>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QTest>

class TrackTraceLoggerTest : public QObject {
    Q_OBJECT

private slots:
    void writesTrackIdsBoundingBoxesAndEventOwnership();
};

void TrackTraceLoggerTest::writesTrackIdsBoundingBoxesAndEventOwnership() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString tracePath = tempDir.filePath(QStringLiteral("track-trace.jsonl"));
    TrackTraceLogger logger(tracePath);

    AnalysisFramePacket frame;
    frame.frameId = 42;
    frame.timestampMs = 1001;
    frame.cameraId = QStringLiteral("front_cam");

    TrackedPerson tracked(45);
    tracked.trackId = 7;
    tracked.state = ByteTrackState::Tracked;
    tracked.latestPose.score = 0.97f;
    tracked.latestPose.box = QRectF(10.0, 20.0, 100.0, 200.0);
    tracked.lastClassificationState = QStringLiteral("fall");
    tracked.lastClassificationConfidence = 0.92;
    tracked.hasFreshClassification = true;
    tracked.action.sequence.push(tracked.latestPose);

    TrackedPerson lost(45);
    lost.trackId = 11;
    lost.state = ByteTrackState::Lost;
    lost.latestPose.score = 0.88f;
    lost.latestPose.box = QRectF(250.0, 40.0, 80.0, 150.0);
    lost.lastClassificationState = QStringLiteral("stand");
    lost.lastClassificationConfidence = 0.61;

    TrackTraceEvent event;
    event.trackId = tracked.trackId;
    event.eventType = QStringLiteral("fall_confirmed");
    event.confidence = 0.92;

    logger.appendFrame(frame, 2002, QStringLiteral("front_cam"), {tracked, lost}, {event});

    QFile file(tracePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray line = file.readLine().trimmed();
    QVERIFY(!line.isEmpty());

    const QJsonObject root = QJsonDocument::fromJson(line).object();
    QCOMPARE(static_cast<qint64>(root.value(QStringLiteral("frame_id")).toDouble()), 42LL);
    QCOMPARE(static_cast<qint64>(root.value(QStringLiteral("frame_ts")).toDouble()), 1001LL);
    QCOMPARE(static_cast<qint64>(root.value(QStringLiteral("infer_ts")).toDouble()), 2002LL);
    QCOMPARE(root.value(QStringLiteral("camera_id")).toString(), QStringLiteral("front_cam"));

    const QJsonArray tracks = root.value(QStringLiteral("tracks")).toArray();
    QCOMPARE(tracks.size(), 2);

    const QJsonObject firstTrack = tracks.at(0).toObject();
    QCOMPARE(firstTrack.value(QStringLiteral("track_id")).toInt(), 7);
    QCOMPARE(firstTrack.value(QStringLiteral("state")).toString(), QStringLiteral("tracked"));
    QVERIFY(qAbs(firstTrack.value(QStringLiteral("pose_score")).toDouble() - 0.97) < 1e-6);
    QCOMPARE(firstTrack.value(QStringLiteral("last_classification_state")).toString(), QStringLiteral("fall"));
    QCOMPARE(firstTrack.value(QStringLiteral("last_classification_confidence")).toDouble(), 0.92);
    QCOMPARE(firstTrack.value(QStringLiteral("sequence_size")).toInt(), 1);
    QVERIFY(firstTrack.value(QStringLiteral("has_fresh_classification")).toBool());

    const QJsonObject firstBox = firstTrack.value(QStringLiteral("bbox")).toObject();
    QCOMPARE(firstBox.value(QStringLiteral("x")).toDouble(), 10.0);
    QCOMPARE(firstBox.value(QStringLiteral("y")).toDouble(), 20.0);
    QCOMPARE(firstBox.value(QStringLiteral("w")).toDouble(), 100.0);
    QCOMPARE(firstBox.value(QStringLiteral("h")).toDouble(), 200.0);

    const QJsonObject secondTrack = tracks.at(1).toObject();
    QCOMPARE(secondTrack.value(QStringLiteral("track_id")).toInt(), 11);
    QCOMPARE(secondTrack.value(QStringLiteral("state")).toString(), QStringLiteral("lost"));
    QVERIFY(!secondTrack.value(QStringLiteral("has_fresh_classification")).toBool());

    const QJsonArray events = root.value(QStringLiteral("events")).toArray();
    QCOMPARE(events.size(), 1);
    const QJsonObject firstEvent = events.at(0).toObject();
    QCOMPARE(firstEvent.value(QStringLiteral("track_id")).toInt(), 7);
    QCOMPARE(firstEvent.value(QStringLiteral("event_type")).toString(), QStringLiteral("fall_confirmed"));
    QCOMPARE(firstEvent.value(QStringLiteral("confidence")).toDouble(), 0.92);
}

QTEST_MAIN(TrackTraceLoggerTest)

#include "track_trace_logger_test.moc"
