#include "tracking/track_manager.h"

#include <algorithm>

TrackManager::TrackManager(int maxTracks, int maxMissedFrames)
    : maxTracks_(maxTracks)
    , maxMissedFrames_(maxMissedFrames) {
}

QVector<TrackedPerson> TrackManager::update(
    const QVector<PosePerson> &detections, qint64 timestampMs) {
    QVector<bool> matched(tracks_.size(), false);

    for (const PosePerson &detection : detections) {
        if (detection.keypoints.size() < 17 || !detection.box.isValid()) {
            continue;
        }

        const int bestIndex = findBestTrackIndex(detection, &matched);
        if (bestIndex >= 0) {
            TrackedPerson &track = tracks_[bestIndex];
            track.latestPose = detection;
            track.lastUpdateTs = timestampMs;
            track.missCount = 0;
            track.sequence.push(detection);
            matched[bestIndex] = true;
            continue;
        }

        if (tracks_.size() >= maxTracks_) {
            continue;
        }

        TrackedPerson track(45);
        track.trackId = nextTrackId_++;
        track.latestPose = detection;
        track.lastUpdateTs = timestampMs;
        track.sequence.push(detection);
        tracks_.push_back(track);
        matched.push_back(true);
    }

    for (int i = 0; i < tracks_.size(); ++i) {
        if (!matched.value(i, false)) {
            tracks_[i].missCount += 1;
        }
    }

    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                      [this](const TrackedPerson &track) {
                          return track.missCount > maxMissedFrames_;
                      }),
        tracks_.end());

    std::sort(tracks_.begin(), tracks_.end(),
        [](const TrackedPerson &left, const TrackedPerson &right) {
            return left.latestPose.box.center().x() < right.latestPose.box.center().x();
        });

    return tracks_;
}

QVector<TrackedPerson> TrackManager::activeTracks() const {
    return tracks_;
}

void TrackManager::clear() {
    tracks_.clear();
}

double TrackManager::iou(const QRectF &left, const QRectF &right) {
    const QRectF intersection = left.intersected(right);
    if (intersection.isEmpty()) {
        return 0.0;
    }

    const double intersectionArea = intersection.width() * intersection.height();
    const double unionArea =
        left.width() * left.height() + right.width() * right.height() - intersectionArea;
    return unionArea <= 0.0 ? 0.0 : intersectionArea / unionArea;
}

double TrackManager::centerDistanceSquared(const QRectF &left, const QRectF &right) {
    const QPointF delta = left.center() - right.center();
    return delta.x() * delta.x() + delta.y() * delta.y();
}

int TrackManager::findBestTrackIndex(
    const PosePerson &detection, QVector<bool> *matched) const {
    int bestIndex = -1;
    double bestScore = -1.0;
    for (int i = 0; i < tracks_.size(); ++i) {
        if (matched && matched->value(i, false)) {
            continue;
        }

        const double overlap = iou(tracks_.at(i).latestPose.box, detection.box);
        const double distance = centerDistanceSquared(tracks_.at(i).latestPose.box, detection.box);
        if (overlap < 0.1 && distance > 2500.0) {
            continue;
        }

        const double score = overlap - distance / 100000.0;
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }
    return bestIndex;
}
