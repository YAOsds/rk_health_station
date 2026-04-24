#include "runtime/runtime_config.h"

namespace {
ActionBackendKind parseActionBackend(const QString &value) {
    if (value == QStringLiteral("lstm_rknn")) {
        return ActionBackendKind::LstmRknn;
    }
    if (value == QStringLiteral("rule_based")) {
        return ActionBackendKind::RuleBased;
    }
    return ActionBackendKind::StgcnRknn;
}

void applyPositiveIntEnv(const char *name, int *target) {
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    if (ok && value > 0) {
        *target = value;
    }
}

void applyPositiveDoubleEnv(const char *name, double *target) {
    bool ok = false;
    const double value = qEnvironmentVariable(name).toDouble(&ok);
    if (ok && value > 0.0) {
        *target = value;
    }
}
}

FallRuntimeConfig loadFallRuntimeConfig() {
    FallRuntimeConfig config;
    const QString socketName = qEnvironmentVariable("RK_FALL_SOCKET_NAME");
    if (!socketName.isEmpty()) {
        config.socketName = socketName;
    }

    const QString analysisSocketPath = qEnvironmentVariable("RK_VIDEO_ANALYSIS_SOCKET_PATH");
    if (!analysisSocketPath.isEmpty()) {
        config.analysisSocketPath = analysisSocketPath;
    }

    const QString analysisSharedMemoryName = qEnvironmentVariable("RK_VIDEO_ANALYSIS_SHM_NAME");
    if (!analysisSharedMemoryName.isEmpty()) {
        config.analysisSharedMemoryName = analysisSharedMemoryName;
    }

    const QString poseModelPath = qEnvironmentVariable("RK_FALL_POSE_MODEL_PATH");
    if (!poseModelPath.isEmpty()) {
        config.poseModelPath = poseModelPath;
    }

    const QString stgcnModelPath = qEnvironmentVariable("RK_FALL_STGCN_MODEL_PATH");
    if (!stgcnModelPath.isEmpty()) {
        config.stgcnModelPath = stgcnModelPath;
    }

    const QString lstmModelPath = qEnvironmentVariable("RK_FALL_LSTM_MODEL_PATH");
    if (!lstmModelPath.isEmpty()) {
        config.lstmModelPath = lstmModelPath;
    }

    const QString trackTracePath = qEnvironmentVariable("RK_FALL_TRACK_TRACE_PATH");
    if (!trackTracePath.isEmpty()) {
        config.trackTracePath = trackTracePath;
    }

    const QString actionBackend = qEnvironmentVariable("RK_FALL_ACTION_BACKEND");
    if (!actionBackend.isEmpty()) {
        config.actionBackend = parseActionBackend(actionBackend);
    }

    applyPositiveIntEnv("RK_FALL_MAX_TRACKS", &config.maxTracks);
    applyPositiveIntEnv("RK_FALL_LOST_TIMEOUT_MS", &config.lostTimeoutMs);
    applyPositiveIntEnv("RK_FALL_MIN_VALID_KEYPOINTS", &config.minValidKeypoints);
    applyPositiveIntEnv("RK_FALL_SEQUENCE_LENGTH", &config.sequenceLength);
    applyPositiveDoubleEnv("RK_FALL_TRACK_HIGH_THRESH", &config.trackHighThresh);
    applyPositiveDoubleEnv("RK_FALL_TRACK_LOW_THRESH", &config.trackLowThresh);
    applyPositiveDoubleEnv("RK_FALL_NEW_TRACK_THRESH", &config.newTrackThresh);
    applyPositiveDoubleEnv("RK_FALL_MATCH_THRESH", &config.matchThresh);
    applyPositiveDoubleEnv("RK_FALL_MIN_BOX_AREA", &config.minBoxArea);

    const QString cameraId = qEnvironmentVariable("RK_FALL_CAMERA_ID");
    if (!cameraId.isEmpty()) {
        config.cameraId = cameraId;
    }
    return config;
}
