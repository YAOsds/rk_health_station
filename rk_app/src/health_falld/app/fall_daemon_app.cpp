#include "app/fall_daemon_app.h"

#include "action/action_classifier_factory.h"
#include "ingest/analysis_stream_client.h"
#include "ipc/fall_gateway.h"
#include "pose/rknn_pose_estimator.h"

#include <QDateTime>
#include <QDebug>
#include <QStringList>

#include <memory>

FallDaemonApp::FallDaemonApp(QObject *parent)
    : FallDaemonApp(std::make_unique<RknnPoseEstimator>(), parent) {
}

FallDaemonApp::FallDaemonApp(std::unique_ptr<PoseEstimator> poseEstimator, QObject *parent)
    : QObject(parent)
    , config_(loadFallRuntimeConfig())
    , poseEstimator_(std::move(poseEstimator))
    , actionClassifier_(createActionClassifier(config_))
    , detectorService_(actionClassifier_.get())
    , trackManager_(5, 10)
    , ingestClient_(new AnalysisStreamClient(config_.analysisSocketPath, this))
    , gateway_(new FallGateway(FallRuntimeStatus(), this)) {
    connect(ingestClient_, &AnalysisStreamClient::statusChanged, this, [this](bool connected) {
        runtimeStatus_.inputConnected = connected;
        gateway_->setRuntimeStatus(runtimeStatus_);
    });
    connect(ingestClient_, &AnalysisStreamClient::frameReceived, this,
        [this](const AnalysisFramePacket &frame) {
            QString error;
            runtimeStatus_.lastFrameTs = QDateTime::currentMSecsSinceEpoch();
            const QVector<PosePerson> people = poseEstimator_->infer(frame, &error);
            runtimeStatus_.lastInferTs = QDateTime::currentMSecsSinceEpoch();
            if (!error.isEmpty()) {
                trackManager_.clear();
                runtimeStatus_.lastError = error;
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                gateway_->setRuntimeStatus(runtimeStatus_);
                return;
            }

            runtimeStatus_.lastError.clear();
            const QVector<TrackedPerson> tracks =
                trackManager_.update(people, runtimeStatus_.lastInferTs);
            if (tracks.isEmpty()) {
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                FallClassificationBatch batch;
                batch.cameraId = config_.cameraId;
                batch.timestampMs = runtimeStatus_.lastInferTs;
                gateway_->publishClassificationBatch(batch);
                gateway_->setRuntimeStatus(runtimeStatus_);
                return;
            }

            FallClassificationBatch batch;
            batch.cameraId = config_.cameraId;
            batch.timestampMs = runtimeStatus_.lastInferTs;

            double highestConfidence = 0.0;
            QString highestState = QStringLiteral("monitoring");

            for (const TrackedPerson &track : tracks) {
                if (track.missCount > 0 || track.latestPose.keypoints.size() < 17
                    || !track.sequence.isFull()) {
                    continue;
                }

                QString classifyError;
                const FallDetectorResult result =
                    detectorService_.update(track.sequence.values(), &classifyError);
                if (!classifyError.isEmpty()) {
                    runtimeStatus_.lastError = classifyError;
                    continue;
                }
                if (!result.hasClassification) {
                    continue;
                }

                FallClassificationEntry entry;
                entry.state = result.classificationState;
                entry.confidence = result.classificationConfidence;
                batch.results.push_back(entry);

                if (entry.confidence >= highestConfidence) {
                    highestConfidence = entry.confidence;
                    highestState = entry.state;
                }

                if (result.event.has_value()) {
                    FallEvent event = *result.event;
                    event.cameraId = config_.cameraId;
                    event.tsConfirm = runtimeStatus_.lastInferTs;
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
            } else if (!batch.results.isEmpty()) {
                gateway_->publishClassificationBatch(batch);
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
