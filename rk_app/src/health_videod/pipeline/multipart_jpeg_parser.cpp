#include "pipeline/multipart_jpeg_parser.h"

#include <QList>

bool MultipartJpegParser::takeFrame(
    QByteArray *streamBuffer, const QByteArray &boundaryMarker, QByteArray *jpegBytes) const {
    if (!streamBuffer || !jpegBytes || boundaryMarker.isEmpty()) {
        return false;
    }

    while (true) {
        const int boundaryIndex = streamBuffer->indexOf(boundaryMarker);
        if (boundaryIndex < 0) {
            return false;
        }
        if (boundaryIndex > 0) {
            streamBuffer->remove(0, boundaryIndex);
        }

        int cursor = boundaryMarker.size();
        if (streamBuffer->size() < cursor + 2) {
            return false;
        }
        if (streamBuffer->mid(cursor, 2) == QByteArrayLiteral("--")) {
            streamBuffer->remove(0, cursor + 2);
            continue;
        }
        if (streamBuffer->mid(cursor, 2) != QByteArrayLiteral("\r\n")) {
            streamBuffer->remove(0, cursor);
            continue;
        }
        cursor += 2;

        const int headerEnd = streamBuffer->indexOf(QByteArrayLiteral("\r\n\r\n"), cursor);
        if (headerEnd < 0) {
            return false;
        }

        const QList<QByteArray> headerLines
            = streamBuffer->mid(cursor, headerEnd - cursor).split('\n');
        QByteArray contentType;
        int contentLength = -1;
        for (QByteArray line : headerLines) {
            line = line.trimmed();
            const int separator = line.indexOf(':');
            if (separator <= 0) {
                continue;
            }
            const QByteArray key = line.left(separator).trimmed().toLower();
            const QByteArray value = line.mid(separator + 1).trimmed();
            if (key == QByteArrayLiteral("content-type")) {
                contentType = value.toLower();
            } else if (key == QByteArrayLiteral("content-length")) {
                contentLength = value.toInt();
            }
        }

        const int payloadStart = headerEnd + 4;
        int consumed = payloadStart;
        if (contentLength >= 0) {
            if (streamBuffer->size() < payloadStart + contentLength) {
                return false;
            }
            *jpegBytes = streamBuffer->mid(payloadStart, contentLength);
            consumed = payloadStart + contentLength;
            if (streamBuffer->mid(consumed, 2) == QByteArrayLiteral("\r\n")) {
                consumed += 2;
            }
        } else {
            const int nextBoundary = streamBuffer->indexOf(
                QByteArrayLiteral("\r\n") + boundaryMarker, payloadStart);
            if (nextBoundary < 0) {
                return false;
            }
            *jpegBytes = streamBuffer->mid(payloadStart, nextBoundary - payloadStart);
            consumed = nextBoundary + 2;
        }

        streamBuffer->remove(0, consumed);
        const bool isJpeg = contentType.isEmpty() || contentType == QByteArrayLiteral("image/jpeg");
        if (!isJpeg) {
            jpegBytes->clear();
        }
        return isJpeg;
    }
}
