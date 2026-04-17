#pragma once

#include "action/action_classifier.h"

class StgcnActionClassifier : public ActionClassifier {
public:
    ~StgcnActionClassifier() override;

    bool loadModel(const QString &path, QString *error) override;
    ActionClassification classify(const QVector<PosePerson> &sequence, QString *error) override;

private:
    QString modelPath_;
    void *runtime_ = nullptr;
};
