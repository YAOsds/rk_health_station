#include "tracking/byte_tracker.h"

#include "tracking/association.h"

#include <QSet>

#include <algorithm>

namespace {
int validKeypointCount(const PosePerson &person) {
    int count = 0;
    for (const PoseKeypoint &keypoint : person.keypoints) {
        if (keypoint.score > 0.0f) {
            ++count;
        }
    }
    return count;
}

bool isTrackActive(const TrackedPerson &track) {
    return track.state != ByteTrackState::Removed;
}

QVector<int> collectCandidateTrackIndexes(const QVector<TrackedPerson> &tracks) {
    QVector<int> indexes;
    for (int index = 0; index < tracks.size(); ++index) {
        if (tracks.at(index).state == ByteTrackState::Tracked
            || tracks.at(index).state == ByteTrackState::Lost) {
            indexes.push_back(index);
        }
    }
    return indexes;
}

QVector<int> collectDetectionIndexesByScore(const QVector<ByteTrackDetection> &detections,
    double minScore, double maxScore) {
    QVector<int> indexes;
    for (int index = 0; index < detections.size(); ++index) {
        const double score = detections.at(index).score;
        if (score >= minScore && score < maxScore) {
            indexes.push_back(index);
        }
    }
    return indexes;
}
}

ByteTracker::ByteTracker(const FallRuntimeConfig &config)
    : config_(config) {
}

QVector<TrackedPerson> &ByteTracker::update(const QVector<PosePerson> &detections, qint64 timestampMs) {
    const QVector<ByteTrackDetection> allDetections = buildDetections(detections);
    const QVector<int> highDetectionIndexes =
        collectDetectionIndexesByScore(allDetections, config_.trackHighThresh, 2.0);
    const QVector<int> lowDetectionIndexes =
        collectDetectionIndexesByScore(allDetections, config_.trackLowThresh, config_.trackHighThresh);

    predictTracks();

    AssociationConfig associationConfig;
    associationConfig.matchThresh = config_.matchThresh;

    QSet<int> matchedTrackIndexes;
    QSet<int> matchedDetectionIndexes;

    auto associate = [&](const QVector<int> &candidateTrackIndexes,
                         const QVector<int> &candidateDetectionIndexes) {
        QVector<QRectF> trackBoxes;
        QVector<QRectF> detectionBoxes;
        for (int trackIndex : candidateTrackIndexes) {
            trackBoxes.push_back(tracks_.at(trackIndex).predictedBox.isValid()
                    ? tracks_.at(trackIndex).predictedBox
                    : tracks_.at(trackIndex).latestPose.box);
        }
        for (int detectionIndex : candidateDetectionIndexes) {
            detectionBoxes.push_back(allDetections.at(detectionIndex).box);
        }

        const QVector<AssociationPair> pairs =
            greedyAssociate(trackBoxes, detectionBoxes, associationConfig);
        for (const AssociationPair &pair : pairs) {
            const int trackIndex = candidateTrackIndexes.at(pair.trackIndex);
            const int detectionIndex = candidateDetectionIndexes.at(pair.detectionIndex);
            TrackedPerson &track = tracks_[trackIndex];
            const ByteTrackDetection &detection = allDetections.at(detectionIndex);
            track.latestPose = detection.pose;
            track.predictedBox = detection.box;
            track.lastUpdateTs = timestampMs;
            track.lostSinceTs = 0;
            track.state = ByteTrackState::Tracked;
            track.hitCount += 1;
            track.missCount = 0;
            track.motion.update(detection.box);
            track.sequence.push(detection.pose);
            track.action.onMatched(detection.pose);
            matchedTrackIndexes.insert(trackIndex);
            matchedDetectionIndexes.insert(detectionIndex);
        }
    };

    const QVector<int> candidateTrackIndexes = collectCandidateTrackIndexes(tracks_);
    associate(candidateTrackIndexes, highDetectionIndexes);

    QVector<int> unmatchedTrackIndexes;
    for (int trackIndex : candidateTrackIndexes) {
        if (!matchedTrackIndexes.contains(trackIndex)) {
            unmatchedTrackIndexes.push_back(trackIndex);
        }
    }

    QVector<int> unmatchedLowDetectionIndexes;
    for (int detectionIndex : lowDetectionIndexes) {
        if (!matchedDetectionIndexes.contains(detectionIndex)) {
            unmatchedLowDetectionIndexes.push_back(detectionIndex);
        }
    }
    associate(unmatchedTrackIndexes, unmatchedLowDetectionIndexes);

    for (int detectionIndex : highDetectionIndexes) {
        if (matchedDetectionIndexes.contains(detectionIndex)) {
            continue;
        }
        if (tracks_.size() >= config_.maxTracks
            || allDetections.at(detectionIndex).score < config_.newTrackThresh) {
            continue;
        }

        TrackedPerson track(config_.sequenceLength);
        track.trackId = nextTrackId_++;
        track.state = ByteTrackState::Tracked;
        track.latestPose = allDetections.at(detectionIndex).pose;
        track.predictedBox = allDetections.at(detectionIndex).box;
        track.lastUpdateTs = timestampMs;
        track.hitCount = 1;
        track.motion.initiate(track.latestPose.box);
        track.sequence.push(track.latestPose);
        track.action.onMatched(track.latestPose);
        tracks_.push_back(track);
    }

    for (int trackIndex = 0; trackIndex < tracks_.size(); ++trackIndex) {
        TrackedPerson &track = tracks_[trackIndex];
        if (track.state == ByteTrackState::Removed || matchedTrackIndexes.contains(trackIndex)) {
            continue;
        }

        track.missCount += 1;
        if (track.state == ByteTrackState::Tracked) {
            track.state = ByteTrackState::Lost;
            if (track.lostSinceTs <= 0) {
                track.lostSinceTs = timestampMs;
            }
            track.action.onLost();
        }

        if (track.lostSinceTs > 0 && timestampMs - track.lostSinceTs > config_.lostTimeoutMs) {
            track.state = ByteTrackState::Removed;
        }
    }

    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                      [](const TrackedPerson &track) { return track.state == ByteTrackState::Removed; }),
        tracks_.end());

    std::sort(tracks_.begin(), tracks_.end(),
        [](const TrackedPerson &left, const TrackedPerson &right) {
            return left.latestPose.box.center().x() < right.latestPose.box.center().x();
        });

    return tracks_;
}

const QVector<TrackedPerson> &ByteTracker::activeTracks() const {
    return tracks_;
}

void ByteTracker::clear() {
    tracks_.clear();
}

QVector<ByteTrackDetection> ByteTracker::buildDetections(const QVector<PosePerson> &detections) const {
    QVector<ByteTrackDetection> filtered;
    for (const PosePerson &person : detections) {
        if (!person.box.isValid()) {
            continue;
        }
        if (person.box.width() * person.box.height() < config_.minBoxArea) {
            continue;
        }
        if (validKeypointCount(person) < config_.minValidKeypoints) {
            continue;
        }

        ByteTrackDetection detection;
        detection.box = person.box;
        detection.score = person.score;
        detection.pose = person;
        filtered.push_back(detection);
    }
    return filtered;
}

void ByteTracker::predictTracks() {
    for (TrackedPerson &track : tracks_) {
        if (track.state == ByteTrackState::Removed) {
            continue;
        }
        const QRectF predicted = track.motion.predict();
        track.predictedBox = predicted.isValid() ? predicted : track.latestPose.box;
    }
}
