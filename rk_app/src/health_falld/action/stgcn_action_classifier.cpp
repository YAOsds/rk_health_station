#include "action/stgcn_action_classifier.h"

#include "action/stgcn_preprocessor.h"

#ifdef RKAPP_ENABLE_REAL_STGCN
#include <opencv2/dnn.hpp>

#include <exception>
#endif

#include <cmath>

#ifdef RKAPP_ENABLE_REAL_STGCN
struct StgcnRuntime {
    cv::dnn::Net net;
};

namespace {
void releaseRuntime(StgcnRuntime *runtime) {
    delete runtime;
}
}
#endif

StgcnActionClassifier::~StgcnActionClassifier() {
#ifdef RKAPP_ENABLE_REAL_STGCN
    releaseRuntime(static_cast<StgcnRuntime *>(runtime_));
    runtime_ = nullptr;
#endif
}

bool StgcnActionClassifier::loadModel(const QString &path, QString *error) {
    if (path.isEmpty()) {
        if (error) {
            *error = QStringLiteral("action_model_path_empty");
        }
        return false;
    }

    modelPath_ = path;
#ifdef RKAPP_ENABLE_REAL_STGCN
    releaseRuntime(static_cast<StgcnRuntime *>(runtime_));
    runtime_ = nullptr;
    try {
        auto *runtime = new StgcnRuntime();
        runtime->net = cv::dnn::readNetFromONNX(path.toStdString());
        runtime_ = runtime;
    } catch (const cv::Exception &exception) {
        if (error) {
            *error = QStringLiteral("stgcn_load_failed_%1").arg(QString::fromUtf8(exception.what()));
        }
        return false;
    } catch (const std::exception &exception) {
        if (error) {
            *error = QStringLiteral("stgcn_load_failed_%1").arg(QString::fromUtf8(exception.what()));
        }
        return false;
    }
#endif
    if (error) {
        error->clear();
    }
    return true;
}

ActionClassification StgcnActionClassifier::classify(
    const QVector<PosePerson> &sequence, QString *error) {
    if (error) {
        error->clear();
    }

#ifdef RKAPP_ENABLE_REAL_STGCN
    auto *runtime = static_cast<StgcnRuntime *>(runtime_);
    if (!runtime) {
        if (error) {
            *error = QStringLiteral("action_model_not_loaded");
        }
        return {};
    }

    StgcnInputTensor tensor;
    if (!buildStgcnInputTensor(sequence, &tensor, error)) {
        return {};
    }

    const int dims[] = {1, tensor.channels, tensor.frames, tensor.joints};
    cv::Mat input(4, dims, CV_32F, tensor.values.data());

    try {
        runtime->net.setInput(input.clone());
        cv::Mat output = runtime->net.forward().reshape(1, 1);
        if (output.total() < 3) {
            if (error) {
                *error = QStringLiteral("stgcn_output_invalid");
            }
            return {};
        }

        const float *logits = output.ptr<float>();
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
                *error = QStringLiteral("stgcn_softmax_invalid");
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

        static const char *kLabels[] = {"stand", "fall", "lie"};
        ActionClassification classification;
        classification.label = QString::fromUtf8(kLabels[bestIndex]);
        classification.confidence = probabilities[bestIndex];
        return classification;
    } catch (const cv::Exception &exception) {
        if (error) {
            *error = QStringLiteral("stgcn_infer_failed_%1").arg(QString::fromUtf8(exception.what()));
        }
        return {};
    } catch (const std::exception &exception) {
        if (error) {
            *error = QStringLiteral("stgcn_infer_failed_%1").arg(QString::fromUtf8(exception.what()));
        }
        return {};
    }
#else
    Q_UNUSED(sequence);
    ActionClassification classification;
    classification.label = sequence.isEmpty() ? QStringLiteral("monitoring") : QStringLiteral("stand");
    classification.confidence = sequence.isEmpty() ? 0.0 : 0.5;
    return classification;
#endif
}
