#include "network/device_session.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QTest>

namespace {
constexpr int kWaitMs = 2000;

QTcpSocket *acceptServerSocket(QTcpServer *server) {
    if (!server) {
        return nullptr;
    }
    if (!server->waitForNewConnection(kWaitMs)) {
        return nullptr;
    }
    return server->nextPendingConnection();
}

QByteArray encodeEnvelope(qint64 seq, const QString &deviceId, const QString &blob = QString()) {
    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("telemetry_batch");
    envelope.seq = seq;
    envelope.ts = 1000 + seq;
    envelope.deviceId = deviceId;
    if (!blob.isEmpty()) {
        envelope.payload.insert(QStringLiteral("blob"), blob);
    } else {
        envelope.payload.insert(QStringLiteral("hr"), 72);
    }
    return DeviceFrameCodec::encode(envelope);
}
} // namespace

class DeviceSessionTest : public QObject {
    Q_OBJECT

private slots:
    void disconnectsOnFrameLengthOverflow();
    void disconnectsWhenReadBufferGrowsTooLarge();
    void processesCoalescedFramesBeforeBufferCapCheck();
    void tracksAuthStateAndSendsEnvelope();
};

void DeviceSessionTest::disconnectsOnFrameLengthOverflow() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(kWaitMs));

    QTcpSocket *serverSocket = acceptServerSocket(&server);
    QVERIFY(serverSocket != nullptr);
    auto *session = new DeviceSession(serverSocket, this);
    QVERIFY(session != nullptr);
    QSignalSpy disconnectedSpy(serverSocket, &QTcpSocket::disconnected);

    QByteArray oversizedHeader(4, '\0');
    oversizedHeader[0] = static_cast<char>(0xFF);
    oversizedHeader[1] = static_cast<char>(0xFF);
    oversizedHeader[2] = static_cast<char>(0xFF);
    oversizedHeader[3] = static_cast<char>(0xFF);

    const qint64 written = client.write(oversizedHeader);
    QCOMPARE(written, static_cast<qint64>(oversizedHeader.size()));
    QVERIFY(client.waitForBytesWritten(kWaitMs));

    QTRY_VERIFY_WITH_TIMEOUT(disconnectedSpy.count() > 0, kWaitMs);
}

void DeviceSessionTest::disconnectsWhenReadBufferGrowsTooLarge() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(kWaitMs));

    QTcpSocket *serverSocket = acceptServerSocket(&server);
    QVERIFY(serverSocket != nullptr);
    auto *session = new DeviceSession(serverSocket, this);
    QVERIFY(session != nullptr);
    QSignalSpy disconnectedSpy(serverSocket, &QTcpSocket::disconnected);

    constexpr quint32 kIncompletePayloadLen = 2U * 1024U * 1024U;
    QByteArray header(4, '\0');
    header[0] = static_cast<char>((kIncompletePayloadLen >> 24) & 0xFFU);
    header[1] = static_cast<char>((kIncompletePayloadLen >> 16) & 0xFFU);
    header[2] = static_cast<char>((kIncompletePayloadLen >> 8) & 0xFFU);
    header[3] = static_cast<char>(kIncompletePayloadLen & 0xFFU);

    qint64 written = client.write(header);
    QCOMPARE(written, static_cast<qint64>(header.size()));
    QVERIFY(client.waitForBytesWritten(kWaitMs));

    const QByteArray chunk(64 * 1024, 'x');
    for (int i = 0; i < 24 && disconnectedSpy.count() == 0; ++i) {
        written = client.write(chunk);
        if (written <= 0) {
            break;
        }
        QCOMPARE(written, static_cast<qint64>(chunk.size()));
        QVERIFY(client.waitForBytesWritten(kWaitMs));
    }

    QTRY_VERIFY_WITH_TIMEOUT(disconnectedSpy.count() > 0, kWaitMs);
}

void DeviceSessionTest::processesCoalescedFramesBeforeBufferCapCheck() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(kWaitMs));

    QTcpSocket *serverSocket = acceptServerSocket(&server);
    QVERIFY(serverSocket != nullptr);
    QSignalSpy disconnectedSpy(serverSocket, &QTcpSocket::disconnected);

    constexpr int kBlobBytes = 600 * 1024;
    const QByteArray firstFrame = encodeEnvelope(
        1, QStringLiteral("dev-1"), QString(kBlobBytes, QLatin1Char('a')));
    const QByteArray secondFrame = encodeEnvelope(
        2, QStringLiteral("dev-1"), QString(kBlobBytes, QLatin1Char('b')));
    QVERIFY(!firstFrame.isEmpty());
    QVERIFY(!secondFrame.isEmpty());

    const QByteArray burst = firstFrame + secondFrame;
    QVERIFY(burst.size() > 1024 * 1024);

    const qint64 written = client.write(burst);
    QCOMPARE(written, static_cast<qint64>(burst.size()));
    QVERIFY(client.waitForBytesWritten(kWaitMs));
    QTRY_VERIFY_WITH_TIMEOUT(serverSocket->bytesAvailable() >= burst.size(), kWaitMs);

    auto *session = new DeviceSession(serverSocket, this);
    QVERIFY(session != nullptr);
    int envelopeCount = 0;
    QList<qint64> receivedSeqs;
    connect(session, &DeviceSession::envelopeReceived, this, [&](const DeviceEnvelope &envelope) {
        ++envelopeCount;
        receivedSeqs.append(envelope.seq);
    });

    QTRY_COMPARE_WITH_TIMEOUT(envelopeCount, 2, kWaitMs);
    QCOMPARE(receivedSeqs.value(0), 1);
    QCOMPARE(receivedSeqs.value(1), 2);
    QCOMPARE(disconnectedSpy.count(), 0);
}

void DeviceSessionTest::tracksAuthStateAndSendsEnvelope() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(kWaitMs));

    QTcpSocket *serverSocket = acceptServerSocket(&server);
    QVERIFY(serverSocket != nullptr);
    auto *session = new DeviceSession(serverSocket, this);
    QVERIFY(session != nullptr);

    QCOMPARE(session->authState(), DeviceSession::SessionAuthState::New);
    session->setAuthState(DeviceSession::SessionAuthState::ChallengeSent);
    QCOMPARE(session->authState(), DeviceSession::SessionAuthState::ChallengeSent);

    session->setAuthenticatedDeviceId(QStringLiteral("auth_watch_001"));
    QCOMPARE(session->authenticatedDeviceId(), QStringLiteral("auth_watch_001"));
    session->setServerNonce(QStringLiteral("server_nonce_001"));
    QCOMPARE(session->serverNonce(), QStringLiteral("server_nonce_001"));

    DeviceEnvelope envelope;
    envelope.ver = 1;
    envelope.type = QStringLiteral("auth_result");
    envelope.seq = 9;
    envelope.ts = 1712345999;
    envelope.deviceId = QStringLiteral("auth_watch_001");
    envelope.payload.insert(QStringLiteral("result"), QStringLiteral("ok"));
    QVERIFY(session->sendEnvelope(envelope));

    QVERIFY(client.waitForReadyRead(kWaitMs));
    DeviceEnvelope decoded;
    QVERIFY(DeviceFrameCodec::decode(client.readAll(), &decoded));
    QCOMPARE(decoded.type, QStringLiteral("auth_result"));
    QCOMPARE(decoded.deviceId, QStringLiteral("auth_watch_001"));
    QCOMPARE(decoded.payload.value(QStringLiteral("result")).toString(), QStringLiteral("ok"));
}

QTEST_MAIN(DeviceSessionTest)

#include "device_session_test.moc"
