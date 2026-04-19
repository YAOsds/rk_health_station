#pragma once

#include "pose/pose_types.h"

struct ByteTrackDetection {
    QRectF box;
    double score = 0.0;
    PosePerson pose;
};
