#pragma once

#include "action/sequence_buffer.h"
#include "domain/fall_event_policy.h"
#include "pose/pose_types.h"

struct TrackActionContext {
    explicit TrackActionContext(int sequenceLength)
        : sequence(sequenceLength) {
    }

    void onMatched(const PosePerson &pose) { sequence.push(pose); }
    void onLost() {}

    SequenceBuffer<PosePerson> sequence;
    FallEventPolicy eventPolicy;
    QString lastState = QStringLiteral("monitoring");
    double lastConfidence = 0.0;
};
