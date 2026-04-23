#pragma once

#include "models/fall_models.h"

#include <QByteArray>
#include <QString>

struct PosePreprocessResult {
    QByteArray packedRgb;
    int xPad = 0;
    int yPad = 0;
    float scale = 1.0f;
};

PosePreprocessResult preprocessNv12ForPose(
    const AnalysisFramePacket &frame, int targetWidth, int targetHeight, QString *error);

bool canUseRgbPoseFastPath(
    const AnalysisFramePacket &frame, int targetWidth, int targetHeight, QString *error);

PosePreprocessResult preprocessRgbFrameForPose(
    const AnalysisFramePacket &frame, int targetWidth, int targetHeight, QString *error);
