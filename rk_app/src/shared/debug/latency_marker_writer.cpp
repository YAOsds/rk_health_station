#include "debug/latency_marker_writer.h"

#include <QFile>
#include <QJsonDocument>

LatencyMarkerWriter::LatencyMarkerWriter(const QString &path)
    : path_(path) {
}

bool LatencyMarkerWriter::isEnabled() const {
    return !path_.isEmpty();
}

void LatencyMarkerWriter::writeEvent(
    const QString &event, qint64 timestampMs, const QJsonObject &payload) const {
    if (!isEnabled()) {
        return;
    }

    QFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QJsonObject json = payload;
    json.insert(QStringLiteral("event"), event);
    json.insert(QStringLiteral("ts_ms"), timestampMs);
    const QByteArray line = QJsonDocument(json).toJson(QJsonDocument::Compact);
    file.write(line);
    file.write("\n");
}
