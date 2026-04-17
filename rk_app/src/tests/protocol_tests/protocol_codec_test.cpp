#include "protocol/device_frame.h"

#include <QJsonDocument>
#include <QtTest/QTest>

class ProtocolCodecTest : public QObject {
    Q_OBJECT

private slots:
    void roundTrip();
    void encodeRejectsEmptyTypeOrDeviceId();
    void encodeRejectsUnsafeSeqTsRange();
    void rejectsShortOrBadLength();
    void rejectsInvalidJson();
    void rejectsMissingRequiredFields();
    void rejectsMissingCommonEnvelopeFields();
    void rejectsWrongEnvelopeFieldTypes();
    void rejectsSeqTsStringValues();
    void rejectsInvalidSeqTsValues();
};

static QByteArray wrapFrame(const QByteArray &body) {
    const quint32 len = static_cast<quint32>(body.size());
    QByteArray frame;
    frame.resize(4);
    frame[0] = static_cast<char>((len >> 24) & 0xFF);
    frame[1] = static_cast<char>((len >> 16) & 0xFF);
    frame[2] = static_cast<char>((len >> 8) & 0xFF);
    frame[3] = static_cast<char>(len & 0xFF);
    frame.append(body);
    return frame;
}

void ProtocolCodecTest::roundTrip() {
    DeviceEnvelope env;
    env.type = QStringLiteral("telemetry_batch");
    env.seq = 1;
    env.ts = 1712345678;
    env.deviceId = QStringLiteral("watch_001");
    env.payload.insert(QStringLiteral("hr"), 72);
    env.payload.insert(QStringLiteral("spo2"), 98.5);

    const QByteArray encoded = DeviceFrameCodec::encode(env);

    DeviceEnvelope decoded;
    QVERIFY(DeviceFrameCodec::decode(encoded, &decoded));
    QCOMPARE(decoded.ver, env.ver);
    QCOMPARE(decoded.type, env.type);
    QCOMPARE(decoded.seq, env.seq);
    QCOMPARE(decoded.ts, env.ts);
    QCOMPARE(decoded.deviceId, env.deviceId);
    QCOMPARE(decoded.payload.value(QStringLiteral("hr")).toInt(), 72);
    QCOMPARE(decoded.payload.value(QStringLiteral("spo2")).toDouble(), 98.5);

    const QByteArray padded = encoded + QByteArray("extra");
    DeviceEnvelope paddedDecoded;
    QVERIFY(DeviceFrameCodec::decode(padded, &paddedDecoded));
    QCOMPARE(paddedDecoded.type, env.type);
    QCOMPARE(paddedDecoded.deviceId, env.deviceId);
}

void ProtocolCodecTest::encodeRejectsEmptyTypeOrDeviceId() {
    DeviceEnvelope env;
    env.seq = 1;
    env.ts = 1712345678;
    env.payload = QJsonObject{};

    env.type = QString();
    env.deviceId = QStringLiteral("watch_001");
    QVERIFY(DeviceFrameCodec::encode(env).isEmpty());

    env.type = QStringLiteral("telemetry_batch");
    env.deviceId = QString();
    QVERIFY(DeviceFrameCodec::encode(env).isEmpty());
}

void ProtocolCodecTest::encodeRejectsUnsafeSeqTsRange() {
    DeviceEnvelope env;
    env.type = QStringLiteral("telemetry_batch");
    env.deviceId = QStringLiteral("watch_001");
    env.payload = QJsonObject{};

    env.seq = 9007199254740992LL;
    env.ts = 1712345678;
    QVERIFY(DeviceFrameCodec::encode(env).isEmpty());

    env.seq = 1;
    env.ts = -9007199254740992LL;
    QVERIFY(DeviceFrameCodec::encode(env).isEmpty());
}

void ProtocolCodecTest::rejectsShortOrBadLength() {
    DeviceEnvelope out;
    QByteArray shortFrame(2, '\0');
    shortFrame[1] = 0x01;
    QVERIFY(!DeviceFrameCodec::decode(shortFrame, &out));

    const QByteArray badBody = QByteArray("{}", 2);
    QByteArray badFrame;
    badFrame.resize(4);
    badFrame[0] = 0x00;
    badFrame[1] = 0x00;
    badFrame[2] = 0x00;
    badFrame[3] = 0x05;
    badFrame.append(badBody);
    QVERIFY(!DeviceFrameCodec::decode(badFrame, &out));
}

void ProtocolCodecTest::rejectsInvalidJson() {
    DeviceEnvelope out;
    const QByteArray frame = wrapFrame(QByteArray("{", 1));
    QVERIFY(!DeviceFrameCodec::decode(frame, &out));
}

void ProtocolCodecTest::rejectsMissingRequiredFields() {
    DeviceEnvelope out;

    QJsonObject missingType;
    missingType.insert(QStringLiteral("device_id"), QStringLiteral("watch_001"));
    const QByteArray frameMissingType = wrapFrame(
        QJsonDocument(missingType).toJson(QJsonDocument::Compact));
    QVERIFY(!DeviceFrameCodec::decode(frameMissingType, &out));

    QJsonObject missingDeviceId;
    missingDeviceId.insert(QStringLiteral("type"), QStringLiteral("telemetry_batch"));
    const QByteArray frameMissingDevice = wrapFrame(
        QJsonDocument(missingDeviceId).toJson(QJsonDocument::Compact));
    QVERIFY(!DeviceFrameCodec::decode(frameMissingDevice, &out));
}

void ProtocolCodecTest::rejectsMissingCommonEnvelopeFields() {
    DeviceEnvelope out;

    const QJsonObject valid {
        {QStringLiteral("ver"), 1},
        {QStringLiteral("type"), QStringLiteral("telemetry_batch")},
        {QStringLiteral("seq"), 5},
        {QStringLiteral("ts"), 1712345678},
        {QStringLiteral("device_id"), QStringLiteral("watch_001")},
        {QStringLiteral("payload"), QJsonObject {}},
    };

    QJsonObject missingVer = valid;
    missingVer.remove(QStringLiteral("ver"));
    QVERIFY(!DeviceFrameCodec::decode(
        wrapFrame(QJsonDocument(missingVer).toJson(QJsonDocument::Compact)), &out));

    QJsonObject missingSeq = valid;
    missingSeq.remove(QStringLiteral("seq"));
    QVERIFY(!DeviceFrameCodec::decode(
        wrapFrame(QJsonDocument(missingSeq).toJson(QJsonDocument::Compact)), &out));

    QJsonObject missingTs = valid;
    missingTs.remove(QStringLiteral("ts"));
    QVERIFY(!DeviceFrameCodec::decode(
        wrapFrame(QJsonDocument(missingTs).toJson(QJsonDocument::Compact)), &out));

    QJsonObject missingPayload = valid;
    missingPayload.remove(QStringLiteral("payload"));
    QVERIFY(!DeviceFrameCodec::decode(
        wrapFrame(QJsonDocument(missingPayload).toJson(QJsonDocument::Compact)), &out));
}

void ProtocolCodecTest::rejectsWrongEnvelopeFieldTypes() {
    DeviceEnvelope out;

    QJsonObject wrongVerType {
        {QStringLiteral("ver"), QStringLiteral("1")},
        {QStringLiteral("type"), QStringLiteral("telemetry_batch")},
        {QStringLiteral("seq"), 5},
        {QStringLiteral("ts"), 1712345678},
        {QStringLiteral("device_id"), QStringLiteral("watch_001")},
        {QStringLiteral("payload"), QJsonObject {}},
    };
    QVERIFY(!DeviceFrameCodec::decode(
        wrapFrame(QJsonDocument(wrongVerType).toJson(QJsonDocument::Compact)), &out));

    QJsonObject wrongPayloadType {
        {QStringLiteral("ver"), 1},
        {QStringLiteral("type"), QStringLiteral("telemetry_batch")},
        {QStringLiteral("seq"), 5},
        {QStringLiteral("ts"), 1712345678},
        {QStringLiteral("device_id"), QStringLiteral("watch_001")},
        {QStringLiteral("payload"), QStringLiteral("bad")},
    };
    QVERIFY(!DeviceFrameCodec::decode(
        wrapFrame(QJsonDocument(wrongPayloadType).toJson(QJsonDocument::Compact)), &out));
}

void ProtocolCodecTest::rejectsSeqTsStringValues() {
    DeviceEnvelope out;

    QJsonObject stringSeq {
        {QStringLiteral("ver"), 1},
        {QStringLiteral("type"), QStringLiteral("telemetry_batch")},
        {QStringLiteral("seq"), QStringLiteral("9007199254740991")},
        {QStringLiteral("ts"), 1712345678},
        {QStringLiteral("device_id"), QStringLiteral("watch_001")},
        {QStringLiteral("payload"), QJsonObject {}},
    };
    QVERIFY(!DeviceFrameCodec::decode(
        wrapFrame(QJsonDocument(stringSeq).toJson(QJsonDocument::Compact)), &out));

    QJsonObject stringTs {
        {QStringLiteral("ver"), 1},
        {QStringLiteral("type"), QStringLiteral("telemetry_batch")},
        {QStringLiteral("seq"), 5},
        {QStringLiteral("ts"), QStringLiteral("1712345678")},
        {QStringLiteral("device_id"), QStringLiteral("watch_001")},
        {QStringLiteral("payload"), QJsonObject {}},
    };
    QVERIFY(!DeviceFrameCodec::decode(
        wrapFrame(QJsonDocument(stringTs).toJson(QJsonDocument::Compact)), &out));
}

void ProtocolCodecTest::rejectsInvalidSeqTsValues() {
    DeviceEnvelope out;

    const QByteArray unsafeNumericSeqFrame = wrapFrame(
        QByteArray(
            "{\"ver\":1,\"type\":\"telemetry_batch\",\"seq\":9007199254740993,\"ts\":1712345678,"
            "\"device_id\":\"watch_001\",\"payload\":{}}"));
    QVERIFY(!DeviceFrameCodec::decode(unsafeNumericSeqFrame, &out));

    const QByteArray fractionalTsFrame = wrapFrame(
        QByteArray(
            "{\"ver\":1,\"type\":\"telemetry_batch\",\"seq\":5,\"ts\":1.5,"
            "\"device_id\":\"watch_001\",\"payload\":{}}"));
    QVERIFY(!DeviceFrameCodec::decode(fractionalTsFrame, &out));
}

QTEST_MAIN(ProtocolCodecTest)

#include "protocol_codec_test.moc"
