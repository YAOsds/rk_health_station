#include "pipeline/multipart_jpeg_parser.h"
#include "pipeline/preview_stream_reader.h"

#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <future>
#include <thread>

namespace {
QByteArray buildMultipartFrame(const QByteArray &boundary, const QByteArray &jpegBytes) {
    QByteArray payload;
    payload.append("--");
    payload.append(boundary);
    payload.append("\r\n");
    payload.append("Content-Type: image/jpeg\r\n");
    payload.append("Content-Length: ");
    payload.append(QByteArray::number(jpegBytes.size()));
    payload.append("\r\n\r\n");
    payload.append(jpegBytes);
    payload.append("\r\n");
    return payload;
}

QByteArray buildTextMultipartFrame(const QByteArray &boundary, const QByteArray &payloadBytes) {
    QByteArray payload;
    payload.append("--");
    payload.append(boundary);
    payload.append("\r\n");
    payload.append("Content-Type: text/plain\r\n");
    payload.append("Content-Length: ");
    payload.append(QByteArray::number(payloadBytes.size()));
    payload.append("\r\n\r\n");
    payload.append(payloadBytes);
    payload.append("\r\n");
    return payload;
}

QByteArray sampleJpegBytes() {
    return QByteArray::fromHex(
        "ffd8ffe000104a46494600010100000100010000ffdb0084000906071010101010100f0f100f0f0f0f0f0f"
        "0f0f0f0f0f0f0f1511161615111515181d2820181a251b151521312125292b2e2e2e171f3338332c37282d2e"
        "2b010a0a0a0e0d0e1a10101a2d1f1f252d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d"
        "2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2dffc00011080001000103012200021101031101ffc4001700"
        "00030100000000000000000000000000010203ffc40014100100000000000000000000000000000000ffda00"
        "0c03010002100310000001ea00ffc400191001000301010000000000000000000000010002111221ffda0008"
        "010100013f00a74697ffc400161101010100000000000000000000000000001101ffda0008010201013f015f"
        "ffc400161101010100000000000000000000000000001121ffda0008010301013f0157ffd9");
}
}

class GstreamerPipelineComponentsTest : public QObject {
    Q_OBJECT

private slots:
    void extractsSingleJpegFromMultipartPayload();
    void ignoresNonJpegMultipartChunk();
    void rejectsInvalidPreviewUrl();
    void readsJpegFrameFromTcpMultipartPreview();
};

void GstreamerPipelineComponentsTest::extractsSingleJpegFromMultipartPayload() {
    MultipartJpegParser parser;
    QByteArray streamBuffer = buildMultipartFrame(QByteArrayLiteral("rkpreview"), sampleJpegBytes());
    QByteArray jpegBytes;

    QVERIFY(parser.takeFrame(
        &streamBuffer, QByteArrayLiteral("--rkpreview"), &jpegBytes));
    QCOMPARE(jpegBytes, sampleJpegBytes());
    QVERIFY(streamBuffer.isEmpty());
}

void GstreamerPipelineComponentsTest::ignoresNonJpegMultipartChunk() {
    MultipartJpegParser parser;
    QByteArray streamBuffer = buildTextMultipartFrame(
        QByteArrayLiteral("rkpreview"), QByteArrayLiteral("not-a-jpeg"));
    QByteArray jpegBytes;

    QVERIFY(!parser.takeFrame(
        &streamBuffer, QByteArrayLiteral("--rkpreview"), &jpegBytes));
    QVERIFY(jpegBytes.isEmpty());
}

void GstreamerPipelineComponentsTest::rejectsInvalidPreviewUrl() {
    PreviewStreamReader reader;
    PreviewStreamReader::PreviewStreamConfig config;
    QString error;

    QVERIFY(!reader.parsePreviewUrl(
        QStringLiteral("http://127.0.0.1/not-preview"), &config, &error));
    QCOMPARE(error, QStringLiteral("invalid_preview_url"));
}

void GstreamerPipelineComponentsTest::readsJpegFrameFromTcpMultipartPreview() {
    const QByteArray jpegBytes = sampleJpegBytes();
    const QByteArray multipartPayload = buildMultipartFrame(QByteArrayLiteral("rkpreview"), jpegBytes);

    std::promise<quint16> portPromise;
    std::future<quint16> portFuture = portPromise.get_future();
    std::thread serverThread([payload = multipartPayload, port = std::move(portPromise)]() mutable {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            port.set_value(0);
            return;
        }
        port.set_value(server.serverPort());
        if (!server.waitForNewConnection(5000)) {
            return;
        }
        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket) {
            return;
        }
        socket->write(payload);
        socket->waitForBytesWritten(5000);
        socket->disconnectFromHost();
        delete socket;
    });

    const quint16 port = portFuture.get();
    QVERIFY(port > 0);

    PreviewStreamReader reader;
    QByteArray capturedJpeg;
    QString error;
    QVERIFY(reader.readJpegFrame(
        QStringLiteral("tcp://127.0.0.1:%1?transport=tcp_mjpeg&boundary=rkpreview").arg(port),
        &capturedJpeg,
        &error));
    QCOMPARE(capturedJpeg, jpegBytes);
    QVERIFY(error.isEmpty());

    serverThread.join();
}

QTEST_MAIN(GstreamerPipelineComponentsTest)
#include "gstreamer_pipeline_components_test.moc"
