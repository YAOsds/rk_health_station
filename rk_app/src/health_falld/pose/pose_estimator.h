#pragma once

#include "models/fall_models.h"
#include "pose/pose_types.h"

#include <QString>
#include <QVector>

class PoseEstimator {
public:
    virtual ~PoseEstimator() = default;

    virtual bool loadModel(const QString &path, QString *error) = 0;
    virtual QVector<PosePerson> infer(const AnalysisFramePacket &frame, QString *error) = 0;
};
