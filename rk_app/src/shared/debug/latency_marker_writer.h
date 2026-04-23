#pragma once

#include <QJsonObject>
#include <QString>

class LatencyMarkerWriter {
public:
    explicit LatencyMarkerWriter(const QString &path);

    bool isEnabled() const;
    void writeEvent(const QString &event, qint64 timestampMs, const QJsonObject &payload) const;

private:
    QString path_;
};
