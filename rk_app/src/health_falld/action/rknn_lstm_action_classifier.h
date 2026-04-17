#pragma once

#include "action/action_classifier.h"
#include "action/rknn_action_model_runner.h"

class RknnLstmActionClassifier : public ActionClassifier {
public:
    ~RknnLstmActionClassifier() override = default;

    bool loadModel(const QString &path, QString *error) override;
    ActionClassification classify(const QVector<PosePerson> &sequence, QString *error) override;

private:
    RknnActionModelRunner runner_;
};
