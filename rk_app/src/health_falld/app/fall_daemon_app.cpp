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
    const auto syncErrorLog = [this]() {
        if (runtimeStatus_.lastError == lastLoggedError_) {
            return;
        }
        if (runtimeStatus_.lastError.isEmpty()) {
            if (!lastLoggedError_.isEmpty()) {
                qInfo().noquote()
                    << QStringLiteral("fall_runtime camera=%1 event=error_cleared")
                           .arg(config_.cameraId);
            }
        } else {
            qWarning().noquote()
                << QStringLiteral("fall_runtime camera=%1 event=error error=%2")
                       .arg(config_.cameraId)
                       .arg(runtimeStatus_.lastError);
        }
        lastLoggedError_ = runtimeStatus_.lastError;
    };

    connect(ingestClient_, &AnalysisStreamClient::statusChanged, this, [this](bool connected) {
        runtimeStatus_.inputConnected = connected;
        qInfo().noquote()
            << QStringLiteral("fall_runtime camera=%1 event=input_connected connected=%2")
                   .arg(config_.cameraId)
                   .arg(connected ? 1 : 0);
        gateway_->setRuntimeStatus(runtimeStatus_);
    });
    connect(ingestClient_, &AnalysisStreamClient::frameReceived, this,
        [this, syncErrorLog](const AnalysisFramePacket &frame) {
            QString error;
            const qint64 frameStartMs = QDateTime::currentMSecsSinceEpoch();
            runtimeStatus_.lastFrameTs = frameStartMs;
            logStats_.onFrameIngested(frameStartMs);
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
            const auto finalizeFrame = [this, frameStartMs, syncErrorLog](
                                           bool hasPeople, bool nonEmptyBatch) {
                logStats_.onInferenceComplete(
                    runtimeStatus_.lastInferTs, hasPeople, nonEmptyBatch,
                    runtimeStatus_.lastInferTs - frameStartMs);
                if (const auto summary = logStats_.takeSummaryIfDue(
                        config_.cameraId, runtimeStatus_.latestState, runtimeStatus_.latestConfidence,
                        runtimeStatus_.lastError, runtimeStatus_.lastInferTs)) {
                    runtimeStatus_.currentFps = summary->inferFps;
                    qInfo().noquote()
                        << QStringLiteral(
                               "fall_perf camera=%1 ingest_fps=%2 infer_fps=%3 avg_infer_ms=%4 people_frames=%5 empty_frames=%6 nonempty_batches=%7 state=%8 conf=%9 error=%10")
                               .arg(summary->cameraId)
                               .arg(QString::number(summary->ingestFps, 'f', 1))
                               .arg(QString::number(summary->inferFps, 'f', 1))
                               .arg(QString::number(summary->avgInferMs, 'f', 1))
                               .arg(summary->peopleFrames)
                               .arg(summary->emptyFrames)
                               .arg(summary->nonEmptyBatchCount)
                               .arg(summary->latestState)
                               .arg(QString::number(summary->latestConfidence, 'f', 2))
                               .arg(summary->latestError);
                }
                syncErrorLog();
            };
            if (!error.isEmpty()) {
                tracker_.clear();
                runtimeStatus_.lastError = error;
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                finalizeFrame(false, false);
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
                finalizeFrame(false, false);
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
                const bool alertState = classification.state == QStringLiteral("fall")
                    || classification.state == QStringLiteral("lie");
                const bool stateChanged = classification.state != lastLoggedState_;
                if (alertState || stateChanged) {
                    qInfo().noquote()
                        << QStringLiteral("classification camera=%1 state=%2 confidence=%3 ts=%4")
                               .arg(classification.cameraId)
                               .arg(classification.state)
                               .arg(QString::number(classification.confidence, 'f', 3))
                               .arg(classification.timestampMs);
                    lastLoggedState_ = classification.state;
                }
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
                    qInfo().noquote()
                        << QStringLiteral(
                               "fall_runtime camera=%1 event=first_classification state=%2 confidence=%3 ts=%4")
                               .arg(batch.cameraId)
                               .arg(batch.results.first().state)
                               .arg(QString::number(batch.results.first().confidence, 'f', 3))
                               .arg(batch.timestampMs);
                }
                const bool alertState = highestState == QStringLiteral("fall")
                    || highestState == QStringLiteral("lie");
                if (alertState) {
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
            }

            trackTraceLogger_.appendFrame(
                frame, runtimeStatus_.lastInferTs, config_.cameraId, tracks, traceEvents);

            finalizeFrame(true, !batch.results.isEmpty());
            gateway_->setRuntimeStatus(runtimeStatus_);
        });
}

bool FallDaemonApp::start() {
    runtimeStatus_.cameraId = config_.cameraId;
    runtimeStatus_.inputConnected = false;
    QString error;
    runtimeStatus_.poseModelReady = poseEstimator_->loadModel(config_.poseModelPath, &error);
    qInfo().noquote()
        << QStringLiteral("fall_runtime camera=%1 event=pose_model_loaded path=%2 ready=%3")
               .arg(config_.cameraId)
               .arg(config_.poseModelPath)
               .arg(runtimeStatus_.poseModelReady ? 1 : 0);
    if (!runtimeStatus_.poseModelReady) {
        runtimeStatus_.lastError = error;
    }
    error.clear();
    runtimeStatus_.actionModelReady = actionClassifier_->loadModel(actionModelPathForConfig(config_), &error);
    qInfo().noquote()
        << QStringLiteral("fall_runtime camera=%1 event=action_model_loaded path=%2 ready=%3")
               .arg(config_.cameraId)
               .arg(actionModelPathForConfig(config_))
               .arg(runtimeStatus_.actionModelReady ? 1 : 0);
    if (!runtimeStatus_.actionModelReady) {
        runtimeStatus_.lastError = error;
    }
    runtimeStatus_.latestState = QStringLiteral("monitoring");
    if (!runtimeStatus_.lastError.isEmpty()) {
        qWarning().noquote()
            << QStringLiteral("fall_runtime camera=%1 event=error error=%2")
                   .arg(config_.cameraId)
                   .arg(runtimeStatus_.lastError);
        lastLoggedError_ = runtimeStatus_.lastError;
    }
    gateway_->setSocketName(config_.socketName);
    gateway_->setRuntimeStatus(runtimeStatus_);
    const bool started = gateway_->start();
    if (started) {
        ingestClient_->start();
    } else {
        qWarning().noquote()
            << QStringLiteral("fall_runtime camera=%1 event=start_failed").arg(config_.cameraId);
    }
    return started;
}
