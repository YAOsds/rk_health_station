#pragma once

#include "action/action_classifier.h"

class RuleBasedActionClassifier : public ActionClassifier {
public:
    bool loadModel(const QString &path, QString *error) override;
    ActionClassification classify(const QVector<PosePerson> &sequence, QString *error) override;
};
