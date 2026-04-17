#include "widgets/video_preview_consumer.h"

#include <QDebug>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {
const int kMaxBufferedBytes = 1024 * 1024 * 2;
}

VideoPreviewConsumer::VideoPreviewConsumer(QObject *parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this)) {
    connect(socket_, &QTcpSocket::connected, this, [this]() {
        qInfo() << "health-ui video: preview socket connected"
                << previewHost_ << previewPort_;
    });
    connect(socket_, &QTcpSocket::readyRead, this, &VideoPreviewConsumer::processPendingData);
    connect(socket_, &QTcpSocket::disconnected, this, [this]() {
        setActive(false);
        if (!stopping_) {
            emit errorTextChanged(QStringLiteral("preview_stream_disconnected"));
        }
    });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!stopping_) {
            emit errorTextChanged(socket_->errorString());
        }
    });
#else
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
        this, [this](QAbstractSocket::SocketError) {
            if (!stopping_) {
                emit errorTextChanged(socket_->errorString());
            }
        });
#endif
}

VideoPreviewConsumer::~VideoPreviewConsumer() {
    stop();
}

void VideoPreviewConsumer::start(const VideoPreviewSource &source) {
    if (source.url.isEmpty()) {
        stop();
        emit errorTextChanged(QStringLiteral("preview_unavailable"));
        return;
    }

    if (active_ && currentSource_ == source) {
        return;
    }

    stop();

    QString errorText;
    if (!configureSource(source, &errorText)) {
        emit errorTextChanged(errorText);
        return;
    }

    clearBuffers();
    currentSource_ = source;
    stopping_ = false;

    qInfo() << "health-ui video: preview consumer started"
            << "source=" << currentSource_.url
            << "host=" << previewHost_
            << "port=" << previewPort_
            << "boundary=" << boundaryMarker_;
    socket_->connectToHost(previewHost_, previewPort_);
    setActive(true);
}

void VideoPreviewConsumer::stop() {
    stopping_ = true;
    socket_->abort();
    clearBuffers();
    currentSource_ = VideoPreviewSource();
    previewHost_.clear();
    previewPort_ = 0;
    boundaryMarker_.clear();
    setActive(false);
    stopping_ = false;
}

bool VideoPreviewConsumer::isActive() const {
    return active_;
}

bool VideoPreviewConsumer::configureSource(
    const VideoPreviewSource &source, QString *errorText) {
    if (errorText) {
        errorText->clear();
    }

    const QUrl parsedUrl(source.url);
    if (!parsedUrl.isValid()) {
        if (errorText) {
            *errorText = QStringLiteral("invalid_preview_url");
        }
        return false;
    }
    if (parsedUrl.scheme() != QStringLiteral("tcp")) {
        if (errorText) {
            *errorText = QStringLiteral("unsupported_preview_scheme");
        }
        return false;
    }
    if (parsedUrl.port() <= 0) {
        if (errorText) {
            *errorText = QStringLiteral("missing_preview_port");
        }
        return false;
    }

    const QUrlQuery query(parsedUrl);
    const QString transport = query.queryItemValue(QStringLiteral("transport"));
    if (transport != QStringLiteral("tcp_mjpeg")) {
        if (errorText) {
            *errorText = QStringLiteral("unsupported_preview_transport");
        }
        return false;
    }

    previewHost_ = parsedUrl.host().isEmpty() ? QStringLiteral("127.0.0.1") : parsedUrl.host();
    previewPort_ = static_cast<quint16>(parsedUrl.port());
    const QString boundary = query.queryItemValue(QStringLiteral("boundary"));
    boundaryMarker_ = QByteArrayLiteral("--") + (boundary.isEmpty()
            ? QByteArrayLiteral("rkpreview")
            : boundary.toUtf8());
    return true;
}

void VideoPreviewConsumer::clearBuffers() {
    streamBuffer_.clear();
}

void VideoPreviewConsumer::setActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    emit activeChanged(active_);
}

void VideoPreviewConsumer::processPendingData() {
    processSocketData(socket_->readAll());
}

void VideoPreviewConsumer::processSocketData(const QByteArray &chunk) {
    if (chunk.isEmpty()) {
        return;
    }
    streamBuffer_.append(chunk);
    trimBufferIfNeeded();

    while (true) {
        int boundaryIndex = streamBuffer_.indexOf(boundaryMarker_);
        if (boundaryIndex < 0) {
            return;
        }
        if (boundaryIndex > 0) {
            streamBuffer_.remove(0, boundaryIndex);
        }

        int cursor = boundaryMarker_.size();
        if (streamBuffer_.size() < cursor + 2) {
            return;
        }
        if (streamBuffer_.mid(cursor, 2) == QByteArrayLiteral("--")) {
            streamBuffer_.remove(0, cursor + 2);
            continue;
        }
        if (streamBuffer_.mid(cursor, 2) != QByteArrayLiteral("\r\n")) {
            streamBuffer_.remove(0, cursor);
            continue;
        }
        cursor += 2;

        const int headerEnd = streamBuffer_.indexOf(QByteArrayLiteral("\r\n\r\n"), cursor);
        if (headerEnd < 0) {
            return;
        }

        const QList<QByteArray> headerLines
            = streamBuffer_.mid(cursor, headerEnd - cursor).split('\n');
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
        QByteArray imageBytes;
        int consumed = payloadStart;
        if (contentLength >= 0) {
            if (streamBuffer_.size() < payloadStart + contentLength) {
                return;
            }
            imageBytes = streamBuffer_.mid(payloadStart, contentLength);
            consumed = payloadStart + contentLength;
            if (streamBuffer_.mid(consumed, 2) == QByteArrayLiteral("\r\n")) {
                consumed += 2;
            }
        } else {
            const int nextBoundary
                = streamBuffer_.indexOf(QByteArrayLiteral("\r\n") + boundaryMarker_, payloadStart);
            if (nextBoundary < 0) {
                return;
            }
            imageBytes = streamBuffer_.mid(payloadStart, nextBoundary - payloadStart);
            consumed = nextBoundary + 2;
        }

        streamBuffer_.remove(0, consumed);
        if (contentType.isEmpty() || contentType == QByteArrayLiteral("image/jpeg")) {
            emitJpegFrame(imageBytes);
        }
    }
}

void VideoPreviewConsumer::emitJpegFrame(const QByteArray &jpegBytes) {
    const QImage image = QImage::fromData(jpegBytes, "JPEG");
    if (!image.isNull()) {
        emit frameReady(image);
    }
}

void VideoPreviewConsumer::trimBufferIfNeeded() {
    if (streamBuffer_.size() <= kMaxBufferedBytes) {
        return;
    }
    const int markerIndex = streamBuffer_.lastIndexOf(boundaryMarker_);
    if (markerIndex > 0) {
        streamBuffer_.remove(0, markerIndex);
    } else {
        streamBuffer_.remove(0, streamBuffer_.size() - kMaxBufferedBytes / 2);
    }
}
