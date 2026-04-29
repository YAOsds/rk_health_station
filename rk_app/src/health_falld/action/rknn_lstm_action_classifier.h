#pragma once

#include "action/action_classifier.h"
#include "action/rknn_action_model_runner.h"
#include "runtime/runtime_config.h"

#include <memory>

struct CpuLstmRuntime {
    int inputSize = 0;
    int hiddenSize = 0;
    int numClasses = 0;
    QVector<float> weightIh;
    QVector<float> weightHh;
    QVector<float> biasIh;
    QVector<float> biasHh;
    QVector<float> headWeight;
    QVector<float> headBias;
};

class RknnLstmActionClassifier : public ActionClassifier {
public:
    RknnLstmActionClassifier();
    explicit RknnLstmActionClassifier(const FallRuntimeConfig &config);
    ~RknnLstmActionClassifier() override;

    bool loadModel(const QString &path, QString *error) override;
    ActionClassification classify(const QVector<PosePerson> &sequence, QString *error) override;

private:
    QVector<float> inferWithCpu(const QVector<float> &input, QString *error) const;

    QString weightsPathOverride_;
    QString tracePath_;
    RknnActionModelRunner runner_;
    std::unique_ptr<CpuLstmRuntime> cpuRuntime_;
};
