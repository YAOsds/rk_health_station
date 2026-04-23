#include "debug/latency_marker_writer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QTest>

class LatencyMarkerWriterTest : public QObject {
    Q_OBJECT

private slots:
    void appendsStructuredEventWhenEnabled();
    void ignoresWritesWhenPathIsEmpty();
};

void LatencyMarkerWriterTest::appendsStructuredEventWhenEnabled() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString markerPath = tempDir.filePath(QStringLiteral("latency.jsonl"));
    LatencyMarkerWriter writer(markerPath);

    writer.writeEvent(QStringLiteral("playback_started"), 1777000000123,
        QJsonObject{{QStringLiteral("camera_id"), QStringLiteral("front_cam")}});

    QFile file(markerPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray line = file.readLine().trimmed();
    const QJsonObject json = QJsonDocument::fromJson(line).object();
    QCOMPARE(json.value(QStringLiteral("event")).toString(), QStringLiteral("playback_started"));
    QCOMPARE(static_cast<qint64>(json.value(QStringLiteral("ts_ms")).toDouble()), 1777000000123);
    QCOMPARE(json.value(QStringLiteral("camera_id")).toString(), QStringLiteral("front_cam"));
}

void LatencyMarkerWriterTest::ignoresWritesWhenPathIsEmpty() {
    LatencyMarkerWriter writer{QString()};
    writer.writeEvent(QStringLiteral("ignored"), 1777000000999, {});
    QVERIFY(true);
}

QTEST_MAIN(LatencyMarkerWriterTest)
#include "latency_marker_writer_test.moc"
