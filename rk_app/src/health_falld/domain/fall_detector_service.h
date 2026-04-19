#pragma once

#include "action/action_classifier.h"
#include "domain/fall_event_policy.h"

struct FallDetectorResult {
    QString state = QStringLiteral("monitoring");
    double confidence = 0.0;
    bool hasClassification = false;
    QString classificationState;
    double classificationConfidence = 0.0;
    std::optional<FallEvent> event;
};

class FallDetectorService {
public:
    explicit FallDetectorService(ActionClassifier *classifier = nullptr);

    FallDetectorResult update(const QVector<PosePerson> &sequence, QString *error);
    FallDetectorResult update(
        const QVector<PosePerson> &sequence, FallEventPolicy *policy, QString *error);

private:
    ActionClassifier *classifier_ = nullptr;
};
