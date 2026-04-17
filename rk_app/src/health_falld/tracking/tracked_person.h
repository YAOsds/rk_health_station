#pragma once

#include "action/sequence_buffer.h"
#include "pose/pose_types.h"

#include <QString>

struct TrackedPerson {
    int trackId = -1;
    PosePerson latestPose;
    qint64 lastUpdateTs = 0;
    int missCount = 0;
    SequenceBuffer<PosePerson> sequence;
    QString lastClassificationState = QStringLiteral("monitoring");
    double lastClassificationConfidence = 0.0;
    bool hasFreshClassification = false;

    explicit TrackedPerson(int sequenceLength = 45)
        : sequence(sequenceLength) {
    }
};
