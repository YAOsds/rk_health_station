#include "widgets/video_preview_consumer.h"

#include <QBuffer>
#include <QHostAddress>
#include <QImage>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QTest>

Q_DECLARE_METATYPE(QImage)

namespace {
QByteArray buildMultipartFrame(const QByteArray &boundary, const QImage &image) {
    QByteArray jpegBytes;
    QBuffer buffer(&jpegBytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 100);

    QByteArray payload;
    payload += "--" + boundary + "\r\n";
    payload += "Content-Type: image/jpeg\r\n";
    payload += "Content-Length: " + QByteArray::number(jpegBytes.size()) + "\r\n\r\n";
    payload += jpegBytes;
    payload += "\r\n";
    return payload;
}
}

class VideoPreviewConsumerTest : public QObject {
    Q_OBJECT

private slots:
    void consumesMultipartJpegFramesFromTcpSource();
    void rejectsUnsupportedPreviewTransport();
};

void VideoPreviewConsumerTest::consumesMultipartJpegFramesFromTcpSource() {
    qRegisterMetaType<QImage>("QImage");

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const QByteArray boundary("rkpreview");
    QImage sourceImage(16, 8, QImage::Format_RGB32);
    sourceImage.fill(Qt::red);
    for (int y = 0; y < sourceImage.height(); ++y) {
        for (int x = sourceImage.width() / 2; x < sourceImage.width(); ++x) {
            sourceImage.setPixelColor(x, y, QColor(0, 255, 0));
        }
    }
    const QByteArray payload = buildMultipartFrame(boundary, sourceImage);

    connect(&server, &QTcpServer::newConnection, &server, [&server, payload]() {
        QTcpSocket *socket = server.nextPendingConnection();
        socket->write(payload);
        socket->flush();
    });

    VideoPreviewConsumer consumer;
    QSignalSpy frameSpy(&consumer, &VideoPreviewConsumer::frameReady);

    VideoPreviewSource source;
    source.url = QStringLiteral("tcp://127.0.0.1:%1?transport=tcp_mjpeg&boundary=rkpreview")
                     .arg(server.serverPort());
    consumer.start(source);

    QVERIFY(frameSpy.wait(2000));
    const QImage image = qvariant_cast<QImage>(frameSpy.takeFirst().at(0));
    QCOMPARE(image.size(), QSize(16, 8));
    const QColor leftColor = image.pixelColor(2, image.height() / 2);
    const QColor rightColor = image.pixelColor(image.width() - 3, image.height() / 2);
    QVERIFY(leftColor.red() > leftColor.green());
    QVERIFY(rightColor.green() > rightColor.red());
}

void VideoPreviewConsumerTest::rejectsUnsupportedPreviewTransport() {
    VideoPreviewConsumer consumer;
    QSignalSpy errorSpy(&consumer, &VideoPreviewConsumer::errorTextChanged);

    VideoPreviewSource source;
    source.url = QStringLiteral("tcp://127.0.0.1:5602?transport=udp_mpegts_h264");

    consumer.start(source);

    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.takeFirst().at(0).toString(), QStringLiteral("unsupported_preview_transport"));
}

QTEST_MAIN(VideoPreviewConsumerTest)
#include "video_preview_consumer_test.moc"
