#include "tracking/association.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
double centerDistance(const QRectF &left, const QRectF &right) {
    const QPointF delta = left.center() - right.center();
    return std::hypot(delta.x(), delta.y());
}
}

double iou(const QRectF &left, const QRectF &right) {
    const QRectF intersection = left.intersected(right);
    if (intersection.isEmpty()) {
        return 0.0;
    }

    const double intersectionArea = intersection.width() * intersection.height();
    const double unionArea =
        left.width() * left.height() + right.width() * right.height() - intersectionArea;
    return unionArea <= 0.0 ? 0.0 : intersectionArea / unionArea;
}

bool passesMotionGate(
    const QRectF &predicted, const QRectF &candidate, const AssociationConfig &config) {
    const double scale = std::max(predicted.width(), predicted.height());
    return scale > 0.0 && centerDistance(predicted, candidate) <= scale * config.maxCenterDistanceFactor;
}

QVector<AssociationPair> greedyAssociate(const QVector<QRectF> &trackBoxes,
    const QVector<QRectF> &detectionBoxes, const AssociationConfig &config) {
    QVector<AssociationPair> candidates;
    for (int trackIndex = 0; trackIndex < trackBoxes.size(); ++trackIndex) {
        for (int detectionIndex = 0; detectionIndex < detectionBoxes.size(); ++detectionIndex) {
            const double overlap = iou(trackBoxes.at(trackIndex), detectionBoxes.at(detectionIndex));
            if (overlap < config.matchThresh
                && !passesMotionGate(trackBoxes.at(trackIndex), detectionBoxes.at(detectionIndex), config)) {
                continue;
            }

            AssociationPair pair;
            pair.trackIndex = trackIndex;
            pair.detectionIndex = detectionIndex;
            pair.score = overlap;
            candidates.push_back(pair);
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const AssociationPair &left, const AssociationPair &right) {
            if (left.score == right.score) {
                if (left.trackIndex == right.trackIndex) {
                    return left.detectionIndex < right.detectionIndex;
                }
                return left.trackIndex < right.trackIndex;
            }
            return left.score > right.score;
        });

    QVector<AssociationPair> accepted;
    QSet<int> usedTracks;
    QSet<int> usedDetections;
    for (const AssociationPair &candidate : candidates) {
        if (usedTracks.contains(candidate.trackIndex)
            || usedDetections.contains(candidate.detectionIndex)) {
            continue;
        }
        accepted.push_back(candidate);
        usedTracks.insert(candidate.trackIndex);
        usedDetections.insert(candidate.detectionIndex);
    }
    return accepted;
}
