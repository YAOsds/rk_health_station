#include "domain/fall_detector_service.h"

#include <QtTest/QTest>

namespace {
QVector<PosePerson> makeSequence() {
    PosePerson person;
    person.score = 0.9f;
    person.keypoints.resize(17);
    return QVector<PosePerson>(45, person);
}

class FakeActionClassifier : public ActionClassifier {
public:
    bool loadModel(const QString &path, QString *error) override {
        Q_UNUSED(path);
        if (error) {
            error->clear();
        }
        return true;
    }

    ActionClassification classify(const QVector<PosePerson> &sequence, QString *error) override {
        lastSequenceLength = sequence.size();
        if (error) {
            error->clear();
        }
        if (responses.isEmpty()) {
            return {};
        }
        return responses.takeFirst();
    }

    int lastSequenceLength = 0;
    QList<ActionClassification> responses;
};
}

class FallDetectorServiceTest : public QObject {
    Q_OBJECT

private slots:
    void confirmsEventAfterRepeatedFallLikeStates();
};

void FallDetectorServiceTest::confirmsEventAfterRepeatedFallLikeStates() {
    FakeActionClassifier classifier;
    classifier.responses = {
        {QStringLiteral("fall"), 0.91},
        {QStringLiteral("lie"), 0.93},
        {QStringLiteral("lie"), 0.95},
    };

    FallDetectorService service(&classifier);
    QString error;

    const QVector<PosePerson> sequence = makeSequence();
    const FallDetectorResult first = service.update(sequence, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(classifier.lastSequenceLength, 45);
    QCOMPARE(first.state, QStringLiteral("fall"));
    QCOMPARE(first.confidence, 0.91);
    QVERIFY(!first.event.has_value());

    const FallDetectorResult second = service.update(sequence, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(second.state, QStringLiteral("lie"));
    QCOMPARE(second.confidence, 0.93);
    QVERIFY(!second.event.has_value());

    const FallDetectorResult third = service.update(sequence, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(third.state, QStringLiteral("lie"));
    QCOMPARE(third.confidence, 0.95);
    QVERIFY(third.event.has_value());
    QCOMPARE(third.event->eventType, QStringLiteral("fall_confirmed"));
}

QTEST_MAIN(FallDetectorServiceTest)
#include "fall_detector_service_test.moc"
