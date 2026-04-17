#include "pose/pose_estimator.h"

#include <QtTest/QTest>

class FakePoseEstimator : public PoseEstimator {
public:
    bool loadModel(const QString &path, QString *error) override {
        lastPath = path;
        if (error) {
            error->clear();
        }
        return true;
    }

    QVector<PosePerson> infer(const AnalysisFramePacket &frame, QString *error) override {
        Q_UNUSED(frame);
        if (error) {
            error->clear();
        }
        PosePerson person;
        person.score = 0.95f;
        person.keypoints.resize(17);
        return {person};
    }

    QString lastPath;
};

class FallRuntimePoseStubTest : public QObject {
    Q_OBJECT

private slots:
    void estimatorInterfaceReturnsSinglePose();
};

void FallRuntimePoseStubTest::estimatorInterfaceReturnsSinglePose() {
    FakePoseEstimator estimator;
    QString error;
    QVERIFY(estimator.loadModel(QStringLiteral("assets/models/yolov8n-pose.rknn"), &error));

    AnalysisFramePacket frame;
    frame.width = 640;
    frame.height = 640;
    const QVector<PosePerson> result = estimator.infer(frame, &error);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first().keypoints.size(), 17);
}

QTEST_MAIN(FallRuntimePoseStubTest)
#include "fall_runtime_pose_stub_test.moc"
