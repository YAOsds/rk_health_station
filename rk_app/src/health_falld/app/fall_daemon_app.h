#pragma once

#include "domain/fall_detector_service.h"
#include "debug/latency_marker_writer.h"
#include "models/fall_models.h"
#include "pose/pose_estimator.h"
#include "pose/pose_types.h"
#include "runtime/runtime_config.h"
#include "tracking/byte_tracker.h"
#include "tracking/track_trace_logger.h"
#include "tracking/track_icon_registry.h"

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
    ByteTracker tracker_;
    TrackIconRegistry trackIconRegistry_;
    TrackTraceLogger trackTraceLogger_;
    std::unique_ptr<LatencyMarkerWriter> latencyMarkerWriter_;
    bool firstFrameMarkerWritten_ = false;
    bool firstClassificationMarkerWritten_ = false;
    AnalysisStreamClient *ingestClient_ = nullptr;
    FallGateway *gateway_ = nullptr;
};
