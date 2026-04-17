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

    const QString actionBackend = qEnvironmentVariable("RK_FALL_ACTION_BACKEND");
    if (!actionBackend.isEmpty()) {
        config.actionBackend = parseActionBackend(actionBackend);
    }

    bool ok = false;
    const int sequenceLength = qEnvironmentVariableIntValue("RK_FALL_SEQUENCE_LENGTH", &ok);
    if (ok && sequenceLength > 0) {
        config.sequenceLength = sequenceLength;
    }

    const QString cameraId = qEnvironmentVariable("RK_FALL_CAMERA_ID");
    if (!cameraId.isEmpty()) {
        config.cameraId = cameraId;
    }
    return config;
}
