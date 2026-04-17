#pragma once

#include "domain/fall_detector_service.h"
#include "models/fall_models.h"
#include "pose/pose_estimator.h"
#include "pose/pose_types.h"
#include "runtime/runtime_config.h"
#include "tracking/track_manager.h"

#include <memory>
#include <QObject>

class ActionClassifier;
class FallGateway;
class AnalysisStreamClient;

class FallDaemonApp : public QObject {
    Q_OBJECT

public:
    explicit FallDaemonApp(QObject *parent = nullptr);
    explicit FallDaemonApp(std::unique_ptr<PoseEstimator> poseEstimator, QObject *parent = nullptr);

    bool start();

private:
    FallRuntimeConfig config_;
    FallRuntimeStatus runtimeStatus_;
    std::unique_ptr<PoseEstimator> poseEstimator_;
    std::unique_ptr<ActionClassifier> actionClassifier_;
    FallDetectorService detectorService_;
    TrackManager trackManager_;
    AnalysisStreamClient *ingestClient_ = nullptr;
    FallGateway *gateway_ = nullptr;
};
