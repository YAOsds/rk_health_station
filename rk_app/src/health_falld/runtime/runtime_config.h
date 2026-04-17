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
    ActionBackendKind actionBackend = ActionBackendKind::LstmRknn;
    int sequenceLength = 45;
    bool enabled = true;
};

FallRuntimeConfig loadFallRuntimeConfig();
