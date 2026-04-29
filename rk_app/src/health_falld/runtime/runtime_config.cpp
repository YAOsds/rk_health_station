#include "runtime/runtime_config.h"

#include "runtime_config/app_runtime_config_loader.h"

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

FallRuntimeConfig loadFallRuntimeConfig(const AppRuntimeConfig &appConfig) {
    FallRuntimeConfig config;
    config.cameraId = appConfig.video.cameraId;
    config.socketName = appConfig.ipc.fallSocketPath;
    config.analysisSocketPath = appConfig.ipc.analysisSocketPath;
    config.analysisTransport = appConfig.analysis.transport;
    config.analysisSharedMemoryName = appConfig.ipc.analysisSharedMemoryName;
    config.poseModelPath = appConfig.fallDetection.poseModelPath;
    config.stgcnModelPath = appConfig.fallDetection.stgcnModelPath;
    config.lstmModelPath = appConfig.fallDetection.lstmModelPath;
    config.lstmWeightsPath = appConfig.fallDetection.lstmWeightsPath;
    config.trackTracePath = appConfig.debug.fallTrackTracePath;
    config.latencyMarkerPath = appConfig.debug.fallLatencyMarkerPath;
    config.poseTimingPath = appConfig.debug.fallPoseTimingPath;
    config.lstmTracePath = appConfig.debug.fallLstmTracePath;
    config.actionBackend = parseActionBackend(appConfig.fallDetection.actionBackend);
    config.maxTracks = appConfig.fallDetection.maxTracks;
    config.trackHighThresh = appConfig.fallDetection.trackHighThresh;
    config.trackLowThresh = appConfig.fallDetection.trackLowThresh;
    config.newTrackThresh = appConfig.fallDetection.newTrackThresh;
    config.matchThresh = appConfig.fallDetection.matchThresh;
    config.lostTimeoutMs = appConfig.fallDetection.lostTimeoutMs;
    config.minValidKeypoints = appConfig.fallDetection.minValidKeypoints;
    config.minBoxArea = appConfig.fallDetection.minBoxArea;
    config.sequenceLength = appConfig.fallDetection.sequenceLength;
    config.enabled = appConfig.fallDetection.enabled;
    config.rknnInputDmabuf = appConfig.fallDetection.rknnInputDmabuf;
    config.rknnIoMemMode = appConfig.fallDetection.rknnIoMemMode;
    config.actionDebug = appConfig.debug.fallActionDebug;
    return config;
}

FallRuntimeConfig loadFallRuntimeConfig() {
    return loadFallRuntimeConfig(loadAppRuntimeConfig(QString()).config);
}
