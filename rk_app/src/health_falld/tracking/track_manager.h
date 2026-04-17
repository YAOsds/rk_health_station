#pragma once

#include "tracking/tracked_person.h"

#include <QVector>

class TrackManager {
public:
    TrackManager(int maxTracks = 5, int maxMissedFrames = 10);

    QVector<TrackedPerson> update(const QVector<PosePerson> &detections, qint64 timestampMs);
    QVector<TrackedPerson> activeTracks() const;
    void clear();

private:
    int findBestTrackIndex(const PosePerson &detection, QVector<bool> *matched) const;
    static double iou(const QRectF &left, const QRectF &right);
    static double centerDistanceSquared(const QRectF &left, const QRectF &right);

    int maxTracks_ = 5;
    int maxMissedFrames_ = 10;
    int nextTrackId_ = 1;
    QVector<TrackedPerson> tracks_;
};
