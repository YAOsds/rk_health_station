#include "protocol/ipc_message.h"

#include <QJsonArray>
#include <QtTest/QTest>

class IpcMessageTest : public QObject {
    Q_OBJECT

private slots:
    void roundTrip();
    void toJsonRejectsInvalidRequiredFields();
    void rejectsMissingRequiredFields();
    void rejectsWrongFieldTypes();
};

void IpcMessageTest::roundTrip() {
    IpcMessage msg;
    msg.ver = 2;
    msg.kind = QStringLiteral("request");
    msg.action = QStringLiteral("sync");
    msg.reqId = QStringLiteral("req-001");
    msg.ok = false;
    msg.payload.insert(QStringLiteral("scope"), QStringLiteral("all"));

    const QJsonObject obj = ipcMessageToJson(msg);

    IpcMessage decoded;
    QVERIFY(ipcMessageFromJson(obj, &decoded));
    QCOMPARE(decoded.ver, msg.ver);
    QCOMPARE(decoded.kind, msg.kind);
    QCOMPARE(decoded.action, msg.action);
    QCOMPARE(decoded.reqId, msg.reqId);
    QCOMPARE(decoded.ok, msg.ok);
    QCOMPARE(decoded.payload, msg.payload);
}

void IpcMessageTest::toJsonRejectsInvalidRequiredFields() {
    IpcMessage missingKind;
    missingKind.action = QStringLiteral("sync");
    missingKind.payload = QJsonObject{};
    QVERIFY(ipcMessageToJson(missingKind).isEmpty());

    IpcMessage missingAction;
    missingAction.kind = QStringLiteral("request");
    missingAction.payload = QJsonObject{};
    QVERIFY(ipcMessageToJson(missingAction).isEmpty());

    IpcMessage valid;
    valid.kind = QStringLiteral("request");
    valid.action = QStringLiteral("sync");
    valid.payload = QJsonObject{};
    QVERIFY(!ipcMessageToJson(valid).isEmpty());
}

void IpcMessageTest::rejectsMissingRequiredFields() {
    IpcMessage out;

    QJsonObject missingKind;
    missingKind.insert(QStringLiteral("action"), QStringLiteral("sync"));
    QVERIFY(!ipcMessageFromJson(missingKind, &out));

    QJsonObject missingAction;
    missingAction.insert(QStringLiteral("kind"), QStringLiteral("request"));
    QVERIFY(!ipcMessageFromJson(missingAction, &out));

    QJsonObject emptyKind;
    emptyKind.insert(QStringLiteral("kind"), QStringLiteral(""));
    emptyKind.insert(QStringLiteral("action"), QStringLiteral("sync"));
    QVERIFY(!ipcMessageFromJson(emptyKind, &out));

    QJsonObject missingVer;
    missingVer.insert(QStringLiteral("kind"), QStringLiteral("request"));
    missingVer.insert(QStringLiteral("action"), QStringLiteral("sync"));
    missingVer.insert(QStringLiteral("payload"), QJsonObject{});
    QVERIFY(!ipcMessageFromJson(missingVer, &out));

    QJsonObject missingPayload;
    missingPayload.insert(QStringLiteral("kind"), QStringLiteral("request"));
    missingPayload.insert(QStringLiteral("action"), QStringLiteral("sync"));
    missingPayload.insert(QStringLiteral("ver"), 1);
    QVERIFY(!ipcMessageFromJson(missingPayload, &out));
}

void IpcMessageTest::rejectsWrongFieldTypes() {
    IpcMessage out;

    QJsonObject badVer;
    badVer.insert(QStringLiteral("kind"), QStringLiteral("request"));
    badVer.insert(QStringLiteral("action"), QStringLiteral("sync"));
    badVer.insert(QStringLiteral("ver"), QStringLiteral("1"));
    QVERIFY(!ipcMessageFromJson(badVer, &out));

    QJsonObject badReqId;
    badReqId.insert(QStringLiteral("kind"), QStringLiteral("request"));
    badReqId.insert(QStringLiteral("action"), QStringLiteral("sync"));
    badReqId.insert(QStringLiteral("req_id"), 5);
    QVERIFY(!ipcMessageFromJson(badReqId, &out));

    QJsonObject badOk;
    badOk.insert(QStringLiteral("kind"), QStringLiteral("request"));
    badOk.insert(QStringLiteral("action"), QStringLiteral("sync"));
    badOk.insert(QStringLiteral("ok"), QStringLiteral("true"));
    QVERIFY(!ipcMessageFromJson(badOk, &out));

    QJsonObject badPayload;
    badPayload.insert(QStringLiteral("kind"), QStringLiteral("request"));
    badPayload.insert(QStringLiteral("action"), QStringLiteral("sync"));
    badPayload.insert(QStringLiteral("payload"), QJsonArray{});
    QVERIFY(!ipcMessageFromJson(badPayload, &out));
}

QTEST_MAIN(IpcMessageTest)

#include "ipc_message_test.moc"
