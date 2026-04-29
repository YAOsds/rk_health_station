#pragma once

#include "pose/pose_estimator.h"
#include "runtime/runtime_config.h"

class RknnPoseEstimator : public PoseEstimator {
public:
    RknnPoseEstimator();
    explicit RknnPoseEstimator(const FallRuntimeConfig &config);
    ~RknnPoseEstimator() override;

    bool loadModel(const QString &path, QString *error) override;
    QVector<PosePerson> infer(const AnalysisFramePacket &frame, QString *error) override;

private:
    QString poseTimingPath_;
    QString rknnIoMemMode_;
    bool rknnInputDmabuf_ = false;
    QString modelPath_;
    void *runtime_ = nullptr;
};
