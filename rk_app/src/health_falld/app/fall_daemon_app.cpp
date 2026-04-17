#include "app/fall_daemon_app.h"

#include "action/action_classifier_factory.h"
#include "action/target_selector.h"
#include "ingest/analysis_stream_client.h"
#include "ipc/fall_gateway.h"
#include "pose/rknn_pose_estimator.h"

#include <QDateTime>
#include <QDebug>

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
    , sequenceBuffer_(config_.sequenceLength)
    , targetSelector_(std::make_unique<TargetSelector>())
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
                sequenceBuffer_.clear();
                runtimeStatus_.lastError = error;
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                gateway_->setRuntimeStatus(runtimeStatus_);
                return;
            }

            runtimeStatus_.lastError.clear();
            if (people.isEmpty()) {
                sequenceBuffer_.clear();
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                gateway_->setRuntimeStatus(runtimeStatus_);
                return;
            }

            const PosePerson primaryPerson = targetSelector_->selectPrimary(people);
            if (primaryPerson.keypoints.size() < 17) {
                sequenceBuffer_.clear();
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                gateway_->setRuntimeStatus(runtimeStatus_);
                return;
            }

            sequenceBuffer_.push(primaryPerson);
            if (!sequenceBuffer_.isFull()) {
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
                gateway_->setRuntimeStatus(runtimeStatus_);
                return;
            }

            const FallDetectorResult result = detectorService_.update(sequenceBuffer_.values(), &error);
            if (!error.isEmpty()) {
                runtimeStatus_.lastError = error;
                runtimeStatus_.latestState = QStringLiteral("monitoring");
                runtimeStatus_.latestConfidence = 0.0;
            } else {
                runtimeStatus_.lastError.clear();
                runtimeStatus_.latestState = result.state;
                runtimeStatus_.latestConfidence = result.confidence;
                if (result.hasClassification) {
                    FallClassificationResult classification;
                    classification.cameraId = config_.cameraId;
                    classification.timestampMs = runtimeStatus_.lastInferTs;
                    classification.state = result.classificationState;
                    classification.confidence = result.classificationConfidence;
                    gateway_->publishClassification(classification);
                    qInfo().noquote()
                        << QStringLiteral("classification camera=%1 state=%2 confidence=%3 ts=%4")
                               .arg(classification.cameraId)
                               .arg(classification.state)
                               .arg(QString::number(classification.confidence, 'f', 3))
                               .arg(classification.timestampMs);
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
