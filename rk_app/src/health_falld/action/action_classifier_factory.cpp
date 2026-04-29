#include "action/action_classifier_factory.h"

#include "action/rknn_lstm_action_classifier.h"
#include "action/rule_based_action_classifier.h"

namespace {
class UnavailableActionClassifier : public ActionClassifier {
public:
    explicit UnavailableActionClassifier(QString reason)
        : reason_(std::move(reason)) {
    }

    bool loadModel(const QString &path, QString *error) override {
        Q_UNUSED(path);
        if (error) {
            *error = reason_;
        }
        return false;
    }

    ActionClassification classify(const QVector<PosePerson> &sequence, QString *error) override {
        Q_UNUSED(sequence);
        if (error) {
            *error = reason_;
        }
        return {};
    }

private:
    QString reason_;
};
}

std::unique_ptr<ActionClassifier> createActionClassifier(const FallRuntimeConfig &config) {
    if (config.actionBackend == ActionBackendKind::LstmRknn) {
        return std::make_unique<RknnLstmActionClassifier>(config);
    }
    if (config.actionBackend == ActionBackendKind::RuleBased) {
        return std::make_unique<RuleBasedActionClassifier>();
    }
    return std::make_unique<UnavailableActionClassifier>(QStringLiteral("stgcn_rknn_blocked_switch_to_lstm"));
}

QString actionModelPathForConfig(const FallRuntimeConfig &config) {
    switch (config.actionBackend) {
    case ActionBackendKind::LstmRknn:
        return config.lstmModelPath;
    case ActionBackendKind::RuleBased:
        return QString();
    case ActionBackendKind::StgcnRknn:
    default:
        return config.stgcnModelPath;
    }
}
