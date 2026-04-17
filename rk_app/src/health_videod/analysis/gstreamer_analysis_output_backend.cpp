#include "analysis/gstreamer_analysis_output_backend.h"

#include "protocol/analysis_stream_protocol.h"

#include <QDateTime>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {
const char kAnalysisSocketEnvVar[] = "RK_VIDEO_ANALYSIS_SOCKET_PATH";
const int kMaxBufferedBytes = 1024 * 1024 * 2;
}

GstreamerAnalysisOutputBackend::GstreamerAnalysisOutputBackend(QObject *parent)
    : QObject(parent)
    , localServer_(new QLocalServer(this))
    , previewSocket_(new QTcpSocket(this)) {
    connect(localServer_, &QLocalServer::newConnection,
        this, &GstreamerAnalysisOutputBackend::onNewLocalConnection);
    connect(previewSocket_, &QTcpSocket::readyRead,
        this, &GstreamerAnalysisOutputBackend::onPreviewReadyRead);
    connect(previewSocket_, &QTcpSocket::connected, this, [this]() {
        setStreamConnected(true);
    });
    connect(previewSocket_, &QTcpSocket::disconnected, this, [this]() {
        setStreamConnected(false);
    });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(previewSocket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!activeCameraId_.isEmpty() && statuses_.contains(activeCameraId_)) {
            AnalysisChannelStatus status = statuses_.value(activeCameraId_);
            status.lastError = previewSocket_->errorString();
            statuses_.insert(activeCameraId_, status);
        }
        setStreamConnected(false);
    });
#else
    connect(previewSocket_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
        this, [this](QAbstractSocket::SocketError) {
            if (!activeCameraId_.isEmpty() && statuses_.contains(activeCameraId_)) {
                AnalysisChannelStatus status = statuses_.value(activeCameraId_);
                status.lastError = previewSocket_->errorString();
                statuses_.insert(activeCameraId_, status);
            }
            setStreamConnected(false);
        });
#endif
}

GstreamerAnalysisOutputBackend::~GstreamerAnalysisOutputBackend() {
    QString error;
    stop(activeCameraId_, &error);
}

QString GstreamerAnalysisOutputBackend::socketPath() const {
    const QString path = qEnvironmentVariable(kAnalysisSocketEnvVar);
    return path.isEmpty() ? QStringLiteral("/tmp/rk_video_analysis.sock") : path;
}

bool GstreamerAnalysisOutputBackend::start(const VideoChannelStatus &status, QString *error) {
    if (error) {
        error->clear();
    }

    AnalysisChannelStatus analysis;
    analysis.cameraId = status.cameraId;
    analysis.enabled = true;
    analysis.streamConnected = false;
    analysis.outputFormat = QStringLiteral("jpeg");
    analysis.width = status.previewProfile.width;
    analysis.height = status.previewProfile.height;
    analysis.fps = status.previewProfile.fps;
    activeCameraId_ = status.cameraId;
    statuses_.insert(status.cameraId, analysis);

    ensureLocalServer(error);
    if (error && !error->isEmpty()) {
        return false;
    }

    if (!configurePreviewSource(status.previewUrl, error)) {
        AnalysisChannelStatus failed = statuses_.value(status.cameraId);
        failed.lastError = error ? *error : QStringLiteral("invalid_preview_url");
        statuses_.insert(status.cameraId, failed);
        return false;
    }

    previewBuffer_.clear();
    previewSocket_->abort();
    previewSocket_->connectToHost(previewHost_, previewPort_);
    return true;
}

bool GstreamerAnalysisOutputBackend::stop(const QString &cameraId, QString *error) {
    if (error) {
        error->clear();
    }
    if (!statuses_.contains(cameraId)) {
        return true;
    }

    AnalysisChannelStatus status = statuses_.value(cameraId);
    status.enabled = false;
    status.streamConnected = false;
    statuses_.insert(cameraId, status);

    previewSocket_->abort();
    previewBuffer_.clear();
    boundaryMarker_.clear();
    previewHost_.clear();
    previewPort_ = 0;

    for (QLocalSocket *client : clients_) {
        if (!client) {
            continue;
        }
        client->disconnect(this);
        client->disconnectFromServer();
        client->deleteLater();
    }
    clients_.clear();

    if (localServer_->isListening()) {
        localServer_->close();
    }
    QLocalServer::removeServer(socketPath());
    return true;
}

AnalysisChannelStatus GstreamerAnalysisOutputBackend::statusForCamera(const QString &cameraId) const {
    AnalysisChannelStatus status = statuses_.value(cameraId);
    return status.cameraId.isEmpty() ? defaultStatusForCamera(cameraId) : status;
}

bool GstreamerAnalysisOutputBackend::configurePreviewSource(const QString &previewUrl, QString *error) {
    if (error) {
        error->clear();
    }

    const QUrl url(previewUrl);
    if (!url.isValid() || url.scheme() != QStringLiteral("tcp") || url.port() <= 0) {
        if (error) {
            *error = QStringLiteral("invalid_preview_url");
        }
        return false;
    }

    const QUrlQuery query(url);
    if (query.queryItemValue(QStringLiteral("transport")) != QStringLiteral("tcp_mjpeg")) {
        if (error) {
            *error = QStringLiteral("unsupported_preview_transport");
        }
        return false;
    }

    previewHost_ = url.host().isEmpty() ? QStringLiteral("127.0.0.1") : url.host();
    previewPort_ = static_cast<quint16>(url.port());
    const QString boundary = query.queryItemValue(QStringLiteral("boundary"));
    boundaryMarker_ = QByteArrayLiteral("--") + (boundary.isEmpty()
            ? QByteArrayLiteral("rkpreview")
            : boundary.toUtf8());
    return true;
}

void GstreamerAnalysisOutputBackend::ensureLocalServer(QString *error) {
    if (error) {
        error->clear();
    }
    if (localServer_->isListening()) {
        return;
    }

    QLocalServer::removeServer(socketPath());
    if (!localServer_->listen(socketPath()) && error) {
        *error = localServer_->errorString();
    }
}

void GstreamerAnalysisOutputBackend::onNewLocalConnection() {
    while (localServer_->hasPendingConnections()) {
        QLocalSocket *socket = localServer_->nextPendingConnection();
        if (!socket) {
            continue;
        }
        clients_.append(socket);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            clients_.removeAll(socket);
            socket->deleteLater();
        });
    }
}

void GstreamerAnalysisOutputBackend::onPreviewReadyRead() {
    processPreviewChunk(previewSocket_->readAll());
}

void GstreamerAnalysisOutputBackend::processPreviewChunk(const QByteArray &chunk) {
    if (chunk.isEmpty()) {
        return;
    }

    previewBuffer_.append(chunk);
    if (previewBuffer_.size() > kMaxBufferedBytes) {
        const int markerIndex = previewBuffer_.lastIndexOf(boundaryMarker_);
        if (markerIndex > 0) {
            previewBuffer_.remove(0, markerIndex);
        } else {
            previewBuffer_.remove(0, previewBuffer_.size() - kMaxBufferedBytes / 2);
        }
    }

    while (true) {
        int boundaryIndex = previewBuffer_.indexOf(boundaryMarker_);
        if (boundaryIndex < 0) {
            return;
        }
        if (boundaryIndex > 0) {
            previewBuffer_.remove(0, boundaryIndex);
        }

        int cursor = boundaryMarker_.size();
        if (previewBuffer_.size() < cursor + 2) {
            return;
        }
        if (previewBuffer_.mid(cursor, 2) == QByteArrayLiteral("--")) {
            previewBuffer_.remove(0, cursor + 2);
            continue;
        }
        if (previewBuffer_.mid(cursor, 2) != QByteArrayLiteral("\r\n")) {
            previewBuffer_.remove(0, cursor);
            continue;
        }
        cursor += 2;

        const int headerEnd = previewBuffer_.indexOf(QByteArrayLiteral("\r\n\r\n"), cursor);
        if (headerEnd < 0) {
            return;
        }

        const QList<QByteArray> headerLines = previewBuffer_.mid(cursor, headerEnd - cursor).split('\n');
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
        if (contentLength < 0 || previewBuffer_.size() < payloadStart + contentLength) {
            return;
        }

        QByteArray imageBytes = previewBuffer_.mid(payloadStart, contentLength);
        int consumed = payloadStart + contentLength;
        if (previewBuffer_.mid(consumed, 2) == QByteArrayLiteral("\r\n")) {
            consumed += 2;
        }
        previewBuffer_.remove(0, consumed);

        if (contentType.isEmpty() || contentType == QByteArrayLiteral("image/jpeg")) {
            broadcastFrame(imageBytes);
        }
    }
}

void GstreamerAnalysisOutputBackend::broadcastFrame(const QByteArray &jpegBytes) {
    if (activeCameraId_.isEmpty()) {
        return;
    }

    AnalysisFramePacket packet;
    packet.frameId = nextFrameId_++;
    packet.timestampMs = QDateTime::currentMSecsSinceEpoch();
    packet.cameraId = activeCameraId_;
    packet.pixelFormat = AnalysisPixelFormat::Jpeg;
    packet.payload = jpegBytes;

    AnalysisChannelStatus status = statuses_.value(activeCameraId_, defaultStatusForCamera(activeCameraId_));
    packet.width = status.width;
    packet.height = status.height;
    status.streamConnected = true;
    statuses_.insert(activeCameraId_, status);

    const QByteArray encoded = encodeAnalysisFramePacket(packet);
    for (int i = clients_.size() - 1; i >= 0; --i) {
        QLocalSocket *client = clients_.at(i);
        if (!client || client->state() != QLocalSocket::ConnectedState) {
            clients_.removeAt(i);
            continue;
        }
        client->write(encoded);
        client->flush();
    }
}

void GstreamerAnalysisOutputBackend::setStreamConnected(bool connected) {
    if (activeCameraId_.isEmpty()) {
        return;
    }
    AnalysisChannelStatus status = statuses_.value(activeCameraId_, defaultStatusForCamera(activeCameraId_));
    status.streamConnected = connected;
    statuses_.insert(activeCameraId_, status);
}

AnalysisChannelStatus GstreamerAnalysisOutputBackend::defaultStatusForCamera(const QString &cameraId) const {
    AnalysisChannelStatus status;
    status.cameraId = cameraId;
    status.enabled = false;
    status.streamConnected = false;
    return status;
}
