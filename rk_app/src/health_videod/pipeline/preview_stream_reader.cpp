#include "pipeline/preview_stream_reader.h"

#include <QElapsedTimer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {
const int kPreviewFrameReadTimeoutMs = 5000;
}

bool PreviewStreamReader::parsePreviewUrl(
    const QString &previewUrl, PreviewStreamConfig *config, QString *error) const {
    if (error) {
        error->clear();
    }

    const QUrl parsedUrl(previewUrl);
    if (!parsedUrl.isValid() || parsedUrl.scheme() != QStringLiteral("tcp") || parsedUrl.port() <= 0) {
        if (error) {
            *error = QStringLiteral("invalid_preview_url");
        }
        return false;
    }

    const QUrlQuery query(parsedUrl);
    if (query.queryItemValue(QStringLiteral("transport")) != QStringLiteral("tcp_mjpeg")) {
        if (error) {
            *error = QStringLiteral("unsupported_preview_transport");
        }
        return false;
    }

    if (config) {
        config->host = parsedUrl.host().isEmpty() ? QStringLiteral("127.0.0.1") : parsedUrl.host();
        config->port = static_cast<quint16>(parsedUrl.port());
        config->boundary = query.queryItemValue(QStringLiteral("boundary"));
        if (config->boundary.isEmpty()) {
            config->boundary = QStringLiteral("rkpreview");
        }
    }
    return true;
}

bool PreviewStreamReader::readJpegFrame(
    const QString &previewUrl, QByteArray *jpegBytes, QString *error) const {
    if (jpegBytes) {
        jpegBytes->clear();
    }

    PreviewStreamConfig config;
    if (!parsePreviewUrl(previewUrl, &config, error)) {
        return false;
    }

    QTcpSocket socket;
    socket.connectToHost(config.host, config.port);
    if (!socket.waitForConnected(kPreviewFrameReadTimeoutMs)) {
        if (error) {
            *error = socket.errorString();
        }
        return false;
    }

    QByteArray buffer;
    const QByteArray boundaryMarker = QByteArrayLiteral("--") + config.boundary.toUtf8();
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < kPreviewFrameReadTimeoutMs) {
        buffer.append(socket.readAll());
        if (parser_.takeFrame(&buffer, boundaryMarker, jpegBytes)) {
            if (error) {
                error->clear();
            }
            return true;
        }

        const int remainingMs = kPreviewFrameReadTimeoutMs - static_cast<int>(timer.elapsed());
        if (remainingMs <= 0) {
            break;
        }
        if (!socket.waitForReadyRead(remainingMs)) {
            break;
        }
    }

    if (error) {
        *error = QStringLiteral("preview_frame_unavailable");
    }
    return false;
}
