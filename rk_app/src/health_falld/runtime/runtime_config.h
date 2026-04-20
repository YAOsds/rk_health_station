#pragma once

#include "action/action_backend_kind.h"

#include <QString>

struct FallRuntimeConfig {
    QString cameraId = QStringLiteral("front_cam");
    QString socketName = QStringLiteral("rk_fall.sock");
    QString analysisSocketPath = QStringLiteral("/tmp/rk_video_analysis.sock");
    QString poseModelPath = QStringLiteral("assets/models/yolov8n-pose.rknn");
    QString stgcnModelPath = QStringLiteral("assets/models/stgcn_fall.rknn");
    QString lstmModelPath = QStringLiteral("assets/models/lstm_fall.rknn");
    QString trackTracePath;
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
};

FallRuntimeConfig loadFallRuntimeConfig();
