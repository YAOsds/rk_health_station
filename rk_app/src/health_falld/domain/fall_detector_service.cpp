#include "domain/fall_detector_service.h"

FallDetectorService::FallDetectorService(ActionClassifier *classifier)
    : classifier_(classifier) {
}

FallDetectorResult FallDetectorService::update(
    const QVector<PosePerson> &sequence, QString *error) {
    return update(sequence, nullptr, error);
}

FallDetectorResult FallDetectorService::update(
    const QVector<PosePerson> &sequence, FallEventPolicy *policy, QString *error) {
    FallDetectorResult result;
    if (!classifier_) {
        if (error) {
            *error = QStringLiteral("action_classifier_missing");
        }
        return result;
    }

    const ActionClassification classification = classifier_->classify(sequence, error);
    if (error && !error->isEmpty()) {
        return result;
    }

    result.state = classification.label;
    result.confidence = classification.confidence;
    result.hasClassification = true;
    result.classificationState = classification.label;
    result.classificationConfidence = classification.confidence;
    if (policy) {
        result.event = policy->update(classification.label, classification.confidence);
    }
    return result;
}
