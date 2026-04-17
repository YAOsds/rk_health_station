#include "action/sequence_buffer.h"

#include <QtTest/QTest>

class SequenceBufferTest : public QObject {
    Q_OBJECT

private slots:
    void keepsOnlyLatestFrames();
};

void SequenceBufferTest::keepsOnlyLatestFrames() {
    SequenceBuffer<int> buffer(3);
    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    buffer.push(4);

    const QVector<int> values = buffer.values();
    QCOMPARE(values.size(), 3);
    QCOMPARE(values.at(0), 2);
    QCOMPARE(values.at(2), 4);
}

QTEST_MAIN(SequenceBufferTest)
#include "sequence_buffer_test.moc"
