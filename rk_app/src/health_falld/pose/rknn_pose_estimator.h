#pragma once

#include "pose/pose_estimator.h"

class RknnPoseEstimator : public PoseEstimator {
public:
    ~RknnPoseEstimator() override;

    bool loadModel(const QString &path, QString *error) override;
    QVector<PosePerson> infer(const AnalysisFramePacket &frame, QString *error) override;

private:
    QString modelPath_;
    void *runtime_ = nullptr;
};
