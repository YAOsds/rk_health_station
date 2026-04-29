#include "action/rknn_lstm_action_classifier.h"

#include "action/stgcn_preprocessor.h"
#include "runtime/runtime_config.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QtGlobal>

#include <cmath>

namespace {
constexpr int kExpectedFrames = 45;
constexpr int kExpectedInputSize = 51;
constexpr int kExpectedClasses = 3;

void appendLstmTrace(const QString &tracePath, const QVector<float> &input, const QVector<float> &logits,
    const ActionClassification &classification) {
    if (tracePath.isEmpty()) {
        return;
    }

    QJsonArray inputJson;
    for (float value : input) {
        inputJson.append(value);
    }

    QJsonArray logitsJson;
    for (float value : logits) {
        logitsJson.append(value);
    }

    QJsonObject json;
    json.insert(QStringLiteral("ts"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    json.insert(QStringLiteral("label"), classification.label);
    json.insert(QStringLiteral("confidence"), classification.confidence);
    json.insert(QStringLiteral("input"), inputJson);
    json.insert(QStringLiteral("logits"), logitsJson);

    QFile file(tracePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    file.write("\n");
}

QString cpuWeightsPathForModel(const QString &modelPath, const QString &overridePath) {
    if (!overridePath.isEmpty()) {
        return overridePath;
    }

    const QFileInfo modelInfo(modelPath);
    return modelInfo.dir().filePath(modelInfo.completeBaseName() + QStringLiteral("_weights.json"));
}

bool readFloatArray(const QJsonObject &json, const QString &key, int expectedCount,
    QVector<float> *values, QString *error) {
    if (!values) {
        if (error) {
            *error = QStringLiteral("lstm_weights_target_missing");
        }
        return false;
    }

    const QJsonArray array = json.value(key).toArray();
    if (array.size() != expectedCount) {
        if (error) {
            *error = QStringLiteral("lstm_weights_shape_invalid_%1").arg(key);
        }
        return false;
    }

    values->resize(array.size());
    for (int index = 0; index < array.size(); ++index) {
        (*values)[index] = static_cast<float>(array.at(index).toDouble());
    }
    return true;
}

bool loadCpuRuntime(const QString &path, std::unique_ptr<CpuLstmRuntime> *runtime, QString *error) {
    if (!runtime) {
        if (error) {
            *error = QStringLiteral("lstm_runtime_target_missing");
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("lstm_weights_json_invalid");
        }
        return false;
    }

    const QJsonObject json = document.object();
    auto parsed = std::make_unique<CpuLstmRuntime>();
    parsed->inputSize = json.value(QStringLiteral("input_size")).toInt();
    parsed->hiddenSize = json.value(QStringLiteral("hidden_size")).toInt();
    parsed->numClasses = json.value(QStringLiteral("num_classes")).toInt();

    if (parsed->inputSize != kExpectedInputSize || parsed->numClasses != kExpectedClasses
        || parsed->hiddenSize <= 0) {
        if (error) {
            *error = QStringLiteral("lstm_weights_header_invalid");
        }
        return false;
    }

    const int gateSize = parsed->hiddenSize * 4;
    if (!readFloatArray(json, QStringLiteral("weight_ih_l0"), gateSize * parsed->inputSize,
            &parsed->weightIh, error)
        || !readFloatArray(json, QStringLiteral("weight_hh_l0"), gateSize * parsed->hiddenSize,
            &parsed->weightHh, error)
        || !readFloatArray(json, QStringLiteral("bias_ih_l0"), gateSize, &parsed->biasIh, error)
        || !readFloatArray(json, QStringLiteral("bias_hh_l0"), gateSize, &parsed->biasHh, error)
        || !readFloatArray(json, QStringLiteral("head_weight"), parsed->numClasses * parsed->hiddenSize,
            &parsed->headWeight, error)
        || !readFloatArray(json, QStringLiteral("head_bias"), parsed->numClasses, &parsed->headBias, error)) {
        return false;
    }

    *runtime = std::move(parsed);
    if (error) {
        error->clear();
    }
    return true;
}

double sigmoid(double value) {
    return 1.0 / (1.0 + std::exp(-value));
}
}

RknnLstmActionClassifier::RknnLstmActionClassifier()
    : RknnLstmActionClassifier(FallRuntimeConfig()) {
}

RknnLstmActionClassifier::RknnLstmActionClassifier(const FallRuntimeConfig &config)
    : weightsPathOverride_(config.lstmWeightsPath)
    , tracePath_(config.lstmTracePath)
    , runner_(config.actionDebug) {
}

RknnLstmActionClassifier::~RknnLstmActionClassifier() = default;

bool RknnLstmActionClassifier::loadModel(const QString &path, QString *error) {
    cpuRuntime_.reset();

    const QString weightsPath = cpuWeightsPathForModel(path, weightsPathOverride_);
    const bool weightsOverride = !weightsPathOverride_.isEmpty();
    if (QFileInfo::exists(weightsPath) || weightsOverride) {
        if (!loadCpuRuntime(weightsPath, &cpuRuntime_, error)) {
            return false;
        }
        qInfo().noquote()
            << QStringLiteral("loaded cpu lstm weights path=%1 hidden=%2")
                   .arg(weightsPath)
                   .arg(cpuRuntime_->hiddenSize);
        return true;
    }

    return runner_.loadModel(path, error);
}

QVector<float> RknnLstmActionClassifier::inferWithCpu(const QVector<float> &input, QString *error) const {
    if (!cpuRuntime_) {
        if (error) {
            *error = QStringLiteral("lstm_cpu_runtime_missing");
        }
        return {};
    }

    if (input.size() != kExpectedFrames * cpuRuntime_->inputSize) {
        if (error) {
            *error = QStringLiteral("lstm_cpu_input_shape_invalid");
        }
        return {};
    }

    QVector<float> hidden(cpuRuntime_->hiddenSize, 0.0f);
    QVector<float> cell(cpuRuntime_->hiddenSize, 0.0f);
    QVector<float> gates(cpuRuntime_->hiddenSize * 4, 0.0f);

    for (int frame = 0; frame < kExpectedFrames; ++frame) {
        const float *frameInput = input.constData() + (frame * cpuRuntime_->inputSize);
        for (int gate = 0; gate < gates.size(); ++gate) {
            double sum = cpuRuntime_->biasIh[gate] + cpuRuntime_->biasHh[gate];
            const float *inputWeights =
                cpuRuntime_->weightIh.constData() + (gate * cpuRuntime_->inputSize);
            const float *hiddenWeights =
                cpuRuntime_->weightHh.constData() + (gate * cpuRuntime_->hiddenSize);
            for (int inputIndex = 0; inputIndex < cpuRuntime_->inputSize; ++inputIndex) {
                sum += static_cast<double>(inputWeights[inputIndex]) * frameInput[inputIndex];
            }
            for (int hiddenIndex = 0; hiddenIndex < cpuRuntime_->hiddenSize; ++hiddenIndex) {
                sum += static_cast<double>(hiddenWeights[hiddenIndex]) * hidden[hiddenIndex];
            }
            gates[gate] = static_cast<float>(sum);
        }

        const int hiddenSize = cpuRuntime_->hiddenSize;
        for (int index = 0; index < hiddenSize; ++index) {
            const double inputGate = sigmoid(gates[index]);
            const double forgetGate = sigmoid(gates[hiddenSize + index]);
            const double candidate = std::tanh(static_cast<double>(gates[(hiddenSize * 2) + index]));
            const double outputGate = sigmoid(gates[(hiddenSize * 3) + index]);
            const double nextCell = (forgetGate * cell[index]) + (inputGate * candidate);
            cell[index] = static_cast<float>(nextCell);
            hidden[index] = static_cast<float>(outputGate * std::tanh(nextCell));
        }
    }

    QVector<float> logits(cpuRuntime_->numClasses, 0.0f);
    for (int cls = 0; cls < cpuRuntime_->numClasses; ++cls) {
        double sum = cpuRuntime_->headBias[cls];
        const float *classWeights = cpuRuntime_->headWeight.constData() + (cls * cpuRuntime_->hiddenSize);
        for (int hiddenIndex = 0; hiddenIndex < cpuRuntime_->hiddenSize; ++hiddenIndex) {
            sum += static_cast<double>(classWeights[hiddenIndex]) * hidden[hiddenIndex];
        }
        logits[cls] = static_cast<float>(sum);
    }

    if (error) {
        error->clear();
    }
    return logits;
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

    const QVector<float> logits = cpuRuntime_ ? inferWithCpu(flattened, error) : runner_.infer(flattened, error);
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
    appendLstmTrace(tracePath_, flattened, logits, classification);
    return classification;
}
