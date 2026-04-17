#pragma once

#include "action/sequence_buffer.h"
#include "pose/pose_types.h"
#include "tracking/byte_track_state.h"
#include "tracking/kalman_filter.h"

#include <QString>

struct TrackedPerson {
    int trackId = -1;
    ByteTrackState state = ByteTrackState::Tracked;
    QRectF predictedBox;
    PosePerson latestPose;
    qint64 lastUpdateTs = 0;
    qint64 lostSinceTs = 0;
    int hitCount = 0;
    int missCount = 0;
    KalmanFilter motion;
    SequenceBuffer<PosePerson> sequence;
    QString lastClassificationState = QStringLiteral("monitoring");
    double lastClassificationConfidence = 0.0;
    bool hasFreshClassification = false;

    explicit TrackedPerson(int sequenceLength = 45)
        : sequence(sequenceLength) {
    }
};
