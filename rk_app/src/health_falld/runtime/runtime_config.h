#pragma once

#include "action/action_backend_kind.h"
#include "runtime_config/app_runtime_config.h"

#include <QString>

struct FallRuntimeConfig {
    QString cameraId = QStringLiteral("front_cam");
    QString socketName = QStringLiteral("rk_fall.sock");
    QString analysisSocketPath = QStringLiteral("/tmp/rk_video_analysis.sock");
    QString analysisTransport = QStringLiteral("shared_memory");
    QString analysisSharedMemoryName;
    QString poseModelPath = QStringLiteral("assets/models/yolov8n-pose.rknn");
    QString stgcnModelPath = QStringLiteral("assets/models/stgcn_fall.rknn");
    QString lstmModelPath = QStringLiteral("assets/models/lstm_fall.rknn");
    QString lstmWeightsPath;
    QString trackTracePath;
    QString latencyMarkerPath;
    QString poseTimingPath;
    QString lstmTracePath;
    ActionBackendKind actionBackend = ActionBackendKind::LstmRknn;
    int maxTracks = 5;
    double trackHighThresh = 0.35;
    double trackLowThresh = 0.10;
    double newTrackThresh = 0.45;
    double matchThresh = 0.80;
    int lostTimeoutMs = 800;
    int minValidKeypoints = 8;
    double minBoxArea = 4096.0;
    int sequenceLength = 45;
    bool enabled = true;
    bool rknnInputDmabuf = false;
    QString rknnIoMemMode = QStringLiteral("default");
    bool actionDebug = false;
};

FallRuntimeConfig loadFallRuntimeConfig(const AppRuntimeConfig &appConfig);
FallRuntimeConfig loadFallRuntimeConfig();
