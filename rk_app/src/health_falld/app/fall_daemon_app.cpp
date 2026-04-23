#include "app/fall_daemon_app.h"

#include "action/action_classifier_factory.h"
#include "ingest/analysis_stream_client.h"
#include "ipc/fall_gateway.h"
#include "pose/rknn_pose_estimator.h"

#include <QDateTime>
#include <QDebug>
#include <QPointF>
#include <QStringList>

#include <memory>

namespace {
const char kFallLatencyMarkerEnvVar[] = "RK_FALL_LATENCY_MARKER_PATH";

int validKeypointCount(const PosePerson &person) {
    int count = 0;
    for (const PoseKeypoint &keypoint : person.keypoints) {
        if (keypoint.score > 0.0f) {
            ++count;
        }
    }
    return count;
}

QPointF overlayAnchorForPose(const PosePerson &person) {
    const QVector<int> preferred = {0, 1, 2, 3, 4};
    QVector<QPointF> points;
    for (int index : preferred) {
        if (index < person.keypoints.size() && person.keypoints[index].score > 0.2f) {
            points.push_back(QPointF(person.keypoints[index].x, person.keypoints[index].y));
        }
    }

    if (!points.isEmpty()) {
        QPointF sum;
        for (const QPointF &point : points) {
            sum += point;
        }
        return sum / points.size();
    }

    return QPointF(person.box.center().x(), person.box.top());
}
}

FallDaemonApp::FallDaemonApp(QObject *parent)
    : FallDaemonApp(std::make_unique<RknnPoseEstimator>(), parent) {
}

FallDaemonApp::FallDaemonApp(std::unique_ptr<PoseEstimator> poseEstimator, QObject *parent)
    : QObject(parent)
    , config_(loadFallRuntimeConfig())
    , poseEstimator_(std::move(poseEstimator))
    , actionClassifier_(createActionClassifier(config_))
    , detectorService_(actionClassifier_.get())
    , tracker_(config_)
    , trackIconRegistry_(config_.maxTracks)
    , trackTraceLogger_(config_.trackTracePath)
    , latencyMarkerWriter_(std::make_unique<LatencyMarkerWriter>(
          qEnvironmentVariable(kFallLatencyMarkerEnvVar)))
    , ingestClient_(new AnalysisStreamClient(
          config_.analysisSocketPath, config_.analysisSharedMemoryName, this))
    , gateway_(new FallGateway(FallRuntimeStatus(), this)) {
    connect(ingestClient_, &AnalysisStreamClient::statusChanged, this, [this](bool connected) {
        runtimeStatus_.inputConnected = connected;
        gateway_->setRuntimeStatus(runtimeStatus_);
    });
    connect(ingestClient_, &AnalysisStreamClient::frameReceived, this,
        [this](const AnalysisFramePacket &frame) {
            QString error;
            runtimeStatus_.lastFrameTs = QDateTime::currentMSecsSinceEpoch();
            if (latencyMarkerWriter_ && !firstFrameMarkerWritten_) {
                const QString pixelFormat = frame.pixelFormat == AnalysisPixelFormat::Nv12
                    ? QStringLiteral("nv12")
                    : (frame.pixelFormat == AnalysisPixelFormat::Rgb
                        ? QStringLiteral("rgb")
                        : QStringLiteral("jpeg"));
                latencyMarkerWriter_->writeEvent(
                    QStringLiteral("first_analysis_frame"), runtimeStatus_.lastFrameTs,
                    QJsonObject{
                        {QStringLiteral("camera_id"), frame.cameraId},
                        {QStringLiteral("frame_id"), QString::number(frame.frameId)},
                        {QStringLiteral("pixel_format"), pixelFormat},
                    });
                firstFrameMarkerWritten_ = true;
            }
            const QVector<PosePerson> people = poseEstimator_->infer(frame, &error);
            runtimeStatus_.lastInferTs = QDateTime::currentMSecsSinceEpoch();
            if (!error.isEmpty()) {
                tracker_.clear();
                runtimeStatus_.lastError = error;
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                gateway_->setRuntimeStatus(runtimeStatus_);
                return;
            }

            runtimeStatus_.lastError.clear();
            QVector<TrackedPerson> &tracks = tracker_.update(people, runtimeStatus_.lastInferTs);
            QVector<int> activeTrackIds;
            for (const TrackedPerson &track : tracks) {
                activeTrackIds.push_back(track.trackId);
            }
            trackIconRegistry_.reconcileActiveTracks(activeTrackIds);
            if (tracks.isEmpty()) {
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                FallClassificationBatch batch;
                batch.cameraId = config_.cameraId;
                batch.timestampMs = runtimeStatus_.lastInferTs;
                gateway_->publishClassificationBatch(batch);
                trackTraceLogger_.appendFrame(
                    frame, runtimeStatus_.lastInferTs, config_.cameraId, tracks, {});
                gateway_->setRuntimeStatus(runtimeStatus_);
                return;
            }

            FallClassificationBatch batch;
            batch.cameraId = config_.cameraId;
            batch.timestampMs = runtimeStatus_.lastInferTs;
            QVector<TrackTraceEvent> traceEvents;

            double highestConfidence = 0.0;
            QString highestState = QStringLiteral("monitoring");

            for (TrackedPerson &track : tracks) {
                track.hasFreshClassification = false;
                if (track.state != ByteTrackState::Tracked
                    || validKeypointCount(track.latestPose) < config_.minValidKeypoints
                    || !track.action.sequence.isFull()) {
                    continue;
                }

                QString classifyError;
                const FallDetectorResult result = detectorService_.update(
                    track.action.sequence.values(), &track.action.eventPolicy, &classifyError);
                if (!classifyError.isEmpty()) {
                    runtimeStatus_.lastError = classifyError;
                    continue;
                }
                if (!result.hasClassification) {
                    continue;
                }

                track.action.lastState = result.classificationState;
                track.action.lastConfidence = result.classificationConfidence;
                track.lastClassificationState = result.classificationState;
                track.lastClassificationConfidence = result.classificationConfidence;
                track.hasFreshClassification = true;

                const QPointF anchor = overlayAnchorForPose(track.latestPose);

                FallClassificationEntry entry;
                entry.trackId = track.trackId;
                entry.iconId = trackIconRegistry_.iconIdForTrack(track.trackId);
                entry.state = result.classificationState;
                entry.confidence = result.classificationConfidence;
                entry.anchorX = anchor.x();
                entry.anchorY = anchor.y();
                entry.bboxX = track.latestPose.box.x();
                entry.bboxY = track.latestPose.box.y();
                entry.bboxW = track.latestPose.box.width();
                entry.bboxH = track.latestPose.box.height();
                batch.results.push_back(entry);

                if (entry.confidence >= highestConfidence) {
                    highestConfidence = entry.confidence;
                    highestState = entry.state;
                }

                if (result.event.has_value()) {
                    FallEvent event = *result.event;
                    event.cameraId = config_.cameraId;
                    event.tsConfirm = runtimeStatus_.lastInferTs;
                    traceEvents.push_back(
                        {track.trackId, event.eventType, event.confidence});
                    gateway_->publishEvent(event);
                    qInfo().noquote()
                        << QStringLiteral("fall_event camera=%1 type=%2 confidence=%3 ts=%4")
                               .arg(event.cameraId)
                               .arg(event.eventType)
                               .arg(QString::number(event.confidence, 'f', 3))
                               .arg(event.tsConfirm);
                }
            }

            runtimeStatus_.latestState = batch.results.isEmpty() ? QStringLiteral("monitoring") : highestState;
            runtimeStatus_.latestConfidence = highestConfidence;

            if (batch.results.size() == 1) {
                FallClassificationResult classification;
                classification.cameraId = config_.cameraId;
                classification.timestampMs = runtimeStatus_.lastInferTs;
                classification.state = batch.results.first().state;
                classification.confidence = batch.results.first().confidence;
                gateway_->publishClassification(classification);
                qInfo().noquote()
                    << QStringLiteral("classification camera=%1 state=%2 confidence=%3 ts=%4")
                           .arg(classification.cameraId)
                           .arg(classification.state)
                           .arg(QString::number(classification.confidence, 'f', 3))
                           .arg(classification.timestampMs);
            }

            if (!batch.results.isEmpty()) {
                gateway_->publishClassificationBatch(batch);
                if (latencyMarkerWriter_ && !firstClassificationMarkerWritten_) {
                    latencyMarkerWriter_->writeEvent(
                        QStringLiteral("first_classification"), batch.timestampMs,
                        QJsonObject{
                            {QStringLiteral("camera_id"), batch.cameraId},
                            {QStringLiteral("state"), batch.results.first().state},
                            {QStringLiteral("confidence"), batch.results.first().confidence},
                        });
                    firstClassificationMarkerWritten_ = true;
                }
                QStringList states;
                for (const FallClassificationEntry &entry : batch.results) {
                    states.push_back(QStringLiteral("%1:%2")
                                         .arg(entry.state)
                                         .arg(QString::number(entry.confidence, 'f', 2)));
                }
                qInfo().noquote()
                    << QStringLiteral("classification_batch camera=%1 count=%2 states=[%3] ts=%4")
                           .arg(batch.cameraId)
                           .arg(batch.results.size())
                           .arg(states.join(','))
                           .arg(batch.timestampMs);
            }

            trackTraceLogger_.appendFrame(
                frame, runtimeStatus_.lastInferTs, config_.cameraId, tracks, traceEvents);

            gateway_->setRuntimeStatus(runtimeStatus_);
        });
}

bool FallDaemonApp::start() {
    runtimeStatus_.cameraId = config_.cameraId;
    runtimeStatus_.inputConnected = false;
    QString error;
    runtimeStatus_.poseModelReady = poseEstimator_->loadModel(config_.poseModelPath, &error);
    if (!runtimeStatus_.poseModelReady) {
        runtimeStatus_.lastError = error;
    }
    error.clear();
    runtimeStatus_.actionModelReady = actionClassifier_->loadModel(actionModelPathForConfig(config_), &error);
    if (!runtimeStatus_.actionModelReady) {
        runtimeStatus_.lastError = error;
    }
    runtimeStatus_.latestState = QStringLiteral("monitoring");
    gateway_->setSocketName(config_.socketName);
    gateway_->setRuntimeStatus(runtimeStatus_);
    const bool started = gateway_->start();
    if (started) {
        ingestClient_->start();
    }
    return started;
}
