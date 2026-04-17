#pragma once

#include "runtime/runtime_config.h"
#include "tracking/byte_track_detection.h"
#include "tracking/tracked_person.h"

class ByteTracker {
public:
    explicit ByteTracker(const FallRuntimeConfig &config);

    QVector<TrackedPerson> update(const QVector<PosePerson> &detections, qint64 timestampMs);
    QVector<TrackedPerson> activeTracks() const;
    void clear();

private:
    QVector<ByteTrackDetection> buildDetections(const QVector<PosePerson> &detections) const;
    void predictTracks();

    FallRuntimeConfig config_;
    QVector<TrackedPerson> tracks_;
    int nextTrackId_ = 1;
};
