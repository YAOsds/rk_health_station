#include "action/rknn_lstm_action_classifier.h"
#include "runtime/runtime_config.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

namespace {
QString buildWeightsJson() {
    QString json = QStringLiteral("{\n");
    json += QStringLiteral("  \"input_size\": 51,\n");
    json += QStringLiteral("  \"hidden_size\": 1,\n");
    json += QStringLiteral("  \"num_classes\": 3,\n");

    const auto appendArray = [&json](const QString &name, int count) {
        json += QStringLiteral("  \"") + name + QStringLiteral("\": [");
        for (int i = 0; i < count; ++i) {
            if (i > 0) {
                json += QStringLiteral(", ");
            }
            json += QStringLiteral("0.0");
        }
        json += QStringLiteral("]");
    };

    appendArray(QStringLiteral("weight_ih_l0"), 4 * 51);
    json += QStringLiteral(",\n");
    appendArray(QStringLiteral("weight_hh_l0"), 4);
    json += QStringLiteral(",\n");
    appendArray(QStringLiteral("bias_ih_l0"), 4);
    json += QStringLiteral(",\n");
    appendArray(QStringLiteral("bias_hh_l0"), 4);
    json += QStringLiteral(",\n");
    appendArray(QStringLiteral("head_weight"), 3);
    json += QStringLiteral(",\n");
    appendArray(QStringLiteral("head_bias"), 3);
    json += QStringLiteral("\n}\n");
    return json;
}
}

class RknnLstmActionClassifierTest : public QObject {
    Q_OBJECT

private slots:
    void fallsBackToModelRunnerWhenDefaultWeightsPathIsMissing();
    void loadsCpuWeightsFromExplicitRuntimeConfigPath();
};

void RknnLstmActionClassifierTest::fallsBackToModelRunnerWhenDefaultWeightsPathIsMissing() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    RknnLstmActionClassifier classifier;
    QString error;
    QVERIFY2(classifier.loadModel(tempDir.filePath(QStringLiteral("model_without_weights_file.rknn")), &error),
        qPrintable(error));
    QCOMPARE(error, QString());
}

void RknnLstmActionClassifierTest::loadsCpuWeightsFromExplicitRuntimeConfigPath() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString weightsPath = tempDir.filePath(QStringLiteral("custom_weights.json"));
    QFile file(weightsPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(buildWeightsJson().toUtf8());
    file.close();

    FallRuntimeConfig config;
    config.lstmWeightsPath = weightsPath;

    RknnLstmActionClassifier classifier(config);
    QString error;
    QVERIFY2(classifier.loadModel(tempDir.filePath(QStringLiteral("model_without_matching_weights_name.rknn")), &error), qPrintable(error));
    QCOMPARE(error, QString());
}

QTEST_MAIN(RknnLstmActionClassifierTest)
#include "rknn_lstm_action_classifier_test.moc"
