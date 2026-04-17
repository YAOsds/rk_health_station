#include "action/stgcn_preprocessor.h"

#include <QtTest/QTest>

class RknnLstmTensorShapeTest : public QObject {
    Q_OBJECT

private slots:
    void flattensSharedTensorTo45x51();
};

void RknnLstmTensorShapeTest::flattensSharedTensorTo45x51() {
    StgcnInputTensor tensor;
    tensor.channels = 3;
    tensor.frames = 45;
    tensor.joints = 17;
    tensor.values.fill(1.0f, 3 * 45 * 17);

    const QVector<float> flattened = flattenSkeletonSequenceForLstm(tensor);
    QCOMPARE(flattened.size(), 45 * 51);
}

QTEST_MAIN(RknnLstmTensorShapeTest)
#include "rknn_lstm_tensor_shape_test.moc"
