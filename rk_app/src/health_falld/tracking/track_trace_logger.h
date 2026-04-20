#pragma once

#include "models/fall_models.h"
#include "tracking/tracked_person.h"

#include <QString>

struct TrackTraceEvent {
    int trackId = -1;
    QString eventType;
    double confidence = 0.0;
};

class TrackTraceLogger {
public:
    explicit TrackTraceLogger(const QString &path = QString());

    bool isEnabled() const;
    void appendFrame(const AnalysisFramePacket &frame, qint64 inferTs, const QString &cameraId,
        const QVector<TrackedPerson> &tracks, const QVector<TrackTraceEvent> &events) const;

private:
    QString path_;
};
