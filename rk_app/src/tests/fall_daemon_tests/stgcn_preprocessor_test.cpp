#include "action/stgcn_preprocessor.h"

#include <QtTest/QTest>

namespace {
PosePerson makePosePerson(float tx, float ty, float scale) {
    PosePerson person;
    person.score = 0.95f;
    person.keypoints.resize(17);

    auto setKeypoint = [&person, tx, ty, scale](int index, float x, float y, float conf = 1.0f) {
        PoseKeypoint keypoint;
        keypoint.x = tx + (x * scale);
        keypoint.y = ty + (y * scale);
        keypoint.score = conf;
        person.keypoints[index] = keypoint;
    };

    setKeypoint(0, 0.0f, -1.0f);   // nose
    setKeypoint(5, -0.4f, -0.2f);  // left shoulder
    setKeypoint(6, 0.4f, -0.2f);   // right shoulder
    setKeypoint(11, -0.25f, 0.0f); // left hip
    setKeypoint(12, 0.25f, 0.0f);  // right hip
    setKeypoint(13, -0.25f, 0.5f); // left knee
    setKeypoint(14, 0.25f, 0.5f);  // right knee
    setKeypoint(15, -0.5f, 1.0f);  // left ankle
    setKeypoint(16, 0.5f, 1.0f);   // right ankle
    return person;
}

int tensorIndex(int channel, int frame, int joint, const StgcnInputTensor &tensor) {
    return ((channel * tensor.frames) + frame) * tensor.joints + joint;
}
}

class StgcnPreprocessorTest : public QObject {
    Q_OBJECT

private slots:
    void normalizesSequenceIntoModelTensor();
    void rejectsSequenceWithoutMidHipReference();
};

void StgcnPreprocessorTest::normalizesSequenceIntoModelTensor() {
    QVector<PosePerson> sequence;
    for (int frame = 0; frame < 45; ++frame) {
        sequence.push_back(makePosePerson(100.0f + frame, 200.0f + (frame * 2.0f), 80.0f));
    }

    StgcnInputTensor tensor;
    QString error;
    QVERIFY(buildStgcnInputTensor(sequence, &tensor, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(tensor.channels, 3);
    QCOMPARE(tensor.frames, 45);
    QCOMPARE(tensor.joints, 17);
    QCOMPARE(tensor.values.size(), 3 * 45 * 17);

    QCOMPARE(tensor.values.at(tensorIndex(0, 0, 11, tensor)), -0.125f);
    QCOMPARE(tensor.values.at(tensorIndex(0, 0, 12, tensor)), 0.125f);
    QCOMPARE(tensor.values.at(tensorIndex(1, 0, 0, tensor)), -0.5f);
    QCOMPARE(tensor.values.at(tensorIndex(2, 0, 15, tensor)), 1.0f);
}

void StgcnPreprocessorTest::rejectsSequenceWithoutMidHipReference() {
    QVector<PosePerson> sequence(45, makePosePerson(0.0f, 0.0f, 60.0f));
    for (PosePerson &person : sequence) {
        person.keypoints[11].score = 0.0f;
        person.keypoints[12].score = 0.0f;
    }

    StgcnInputTensor tensor;
    QString error;
    QVERIFY(!buildStgcnInputTensor(sequence, &tensor, &error));
    QCOMPARE(error, QStringLiteral("stgcn_mid_hip_missing"));
}

QTEST_MAIN(StgcnPreprocessorTest)
#include "stgcn_preprocessor_test.moc"
