#include "action/rknn_lstm_action_classifier.h"

#include "action/stgcn_preprocessor.h"

#include <cmath>

bool RknnLstmActionClassifier::loadModel(const QString &path, QString *error) {
    return runner_.loadModel(path, error);
}

ActionClassification RknnLstmActionClassifier::classify(
    const QVector<PosePerson> &sequence, QString *error) {
    StgcnInputTensor tensor;
    if (!buildStgcnInputTensor(sequence, &tensor, error)) {
        return {};
    }

    const QVector<float> flattened = flattenSkeletonSequenceForLstm(tensor);
    if (flattened.isEmpty()) {
        if (error) {
            *error = QStringLiteral("lstm_input_invalid");
        }
        return {};
    }

    const QVector<float> logits = runner_.infer(flattened, error);
    if (logits.size() < 3) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("lstm_output_invalid");
        }
        return {};
    }

    double maxLogit = logits[0];
    for (int index = 1; index < 3; ++index) {
        maxLogit = std::max(maxLogit, static_cast<double>(logits[index]));
    }

    double sumExp = 0.0;
    double probabilities[3] = {0.0, 0.0, 0.0};
    for (int index = 0; index < 3; ++index) {
        probabilities[index] = std::exp(static_cast<double>(logits[index]) - maxLogit);
        sumExp += probabilities[index];
    }
    if (sumExp <= 0.0) {
        if (error) {
            *error = QStringLiteral("lstm_softmax_invalid");
        }
        return {};
    }

    for (double &probability : probabilities) {
        probability /= sumExp;
    }

    int bestIndex = 0;
    for (int index = 1; index < 3; ++index) {
        if (probabilities[index] > probabilities[bestIndex]) {
            bestIndex = index;
        }
    }

    if (error) {
        error->clear();
    }

    ActionClassification classification;
    static const char *kLabels[] = {"stand", "fall", "lie"};
    classification.label = QString::fromUtf8(kLabels[bestIndex]);
    classification.confidence = probabilities[bestIndex];
    return classification;
}
