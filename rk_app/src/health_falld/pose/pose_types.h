#pragma once

#include <QRectF>
#include <QVector>

struct PoseKeypoint {
    float x = 0.0f;
    float y = 0.0f;
    float score = 0.0f;
};

struct PosePerson {
    QRectF box;
    float score = 0.0f;
    QVector<PoseKeypoint> keypoints;
};
