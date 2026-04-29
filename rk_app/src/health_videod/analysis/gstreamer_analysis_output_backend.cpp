#include "analysis/gstreamer_analysis_output_backend.h"

#include "protocol/analysis_frame_descriptor_protocol.h"
#include "protocol/unix_fd_passing.h"
#include "runtime_config/app_runtime_config_loader.h"

#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>

#include <poll.h>
#include <unistd.h>

namespace {
const int kAnalysisFrameWidth = 640;
const int kAnalysisFrameHeight = 640;
}

GstreamerAnalysisOutputBackend::GstreamerAnalysisOutputBackend(QObject *parent)
    : GstreamerAnalysisOutputBackend(loadAppRuntimeConfig(QString()).config, parent) {
}

GstreamerAnalysisOutputBackend::GstreamerAnalysisOutputBackend(
    const AppRuntimeConfig &runtimeConfig, QObject *parent)
    : QObject(parent)
    , localServer_(new QLocalServer(this))
    , runtimeConfig_(runtimeConfig) {
    connect(localServer_, &QLocalServer::newConnection,
        this, &GstreamerAnalysisOutputBackend::onNewLocalConnection);
}

GstreamerAnalysisOutputBackend::~GstreamerAnalysisOutputBackend() {
    QString error;
    stop(activeCameraId_, &error);
}

QString GstreamerAnalysisOutputBackend::socketPath() const {
    return runtimeConfig_.ipc.analysisSocketPath.trimmed().isEmpty()
        ? QStringLiteral("/tmp/rk_video_analysis.sock")
        : runtimeConfig_.ipc.analysisSocketPath.trimmed();
}

bool GstreamerAnalysisOutputBackend::start(const VideoChannelStatus &status, QString *error) {
    if (error) {
        error->clear();
    }

    ensureLocalServer(error);
    if (error && !error->isEmpty()) {
        return false;
    }
    if (dmabufTransportEnabled()) {
        ensureFdServer(error);
        if (error && !error->isEmpty()) {
            return false;
        }
    }

    activeCameraId_ = status.cameraId;

    AnalysisChannelStatus analysis;
    analysis.cameraId = status.cameraId;
    analysis.enabled = true;
    analysis.streamConnected = false;
    analysis.outputFormat = QStringLiteral("rgb");
    analysis.width = kAnalysisFrameWidth;
    analysis.height = kAnalysisFrameHeight;
    analysis.fps = status.previewProfile.fps;
    statuses_.insert(status.cameraId, analysis);
    updateClientState();
    return true;
}

bool GstreamerAnalysisOutputBackend::stop(const QString &cameraId, QString *error) {
    if (error) {
        error->clear();
    }

    if (!cameraId.isEmpty() && statuses_.contains(cameraId)) {
        AnalysisChannelStatus status = statuses_.value(cameraId);
        status.enabled = false;
        status.streamConnected = false;
        statuses_.insert(cameraId, status);
    }

    if (cameraId == activeCameraId_) {
        activeCameraId_.clear();
    }

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
    closeFdServer();
    return true;
}

AnalysisChannelStatus GstreamerAnalysisOutputBackend::statusForCamera(const QString &cameraId) const {
    const AnalysisChannelStatus status = statuses_.value(cameraId);
    return status.cameraId.isEmpty() ? defaultStatusForCamera(cameraId) : status;
}

bool GstreamerAnalysisOutputBackend::acceptsFrames(const QString &cameraId) const {
    if (cameraId.isEmpty() || cameraId != activeCameraId_ || !localServer_->isListening()) {
        return false;
    }
    if (dmabufTransportEnabled() && fdServerFd_ < 0) {
        return false;
    }
    const AnalysisChannelStatus status = statuses_.value(cameraId);
    return status.cameraId == cameraId && status.enabled;
}

bool GstreamerAnalysisOutputBackend::supportsDmaBufFrames() const {
    return dmabufTransportEnabled() && fdServerFd_ >= 0;
}

void GstreamerAnalysisOutputBackend::publishDescriptor(const AnalysisFrameDescriptor &descriptor) {
    if (!acceptsFrames(descriptor.cameraId)) {
        return;
    }

    if (localServer_->hasPendingConnections()) {
        onNewLocalConnection();
    }

    AnalysisChannelStatus status
        = statuses_.value(descriptor.cameraId, defaultStatusForCamera(descriptor.cameraId));
    status.enabled = true;
    status.streamConnected = true;
    status.outputFormat = pixelFormatName(descriptor.pixelFormat);
    if (descriptor.width > 0) {
        status.width = descriptor.width;
    }
    if (descriptor.height > 0) {
        status.height = descriptor.height;
    }
    statuses_.insert(descriptor.cameraId, status);

    const QByteArray encoded = encodeAnalysisFrameDescriptor(descriptor);
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


void GstreamerAnalysisOutputBackend::publishDmaBufDescriptor(
    const AnalysisFrameDescriptor &descriptor, int fd) {
    if (!acceptsFrames(descriptor.cameraId) || fd < 0) {
        return;
    }

    if (localServer_->hasPendingConnections()) {
        onNewLocalConnection();
    }
    acceptPendingFdClients();
    if (fdClients_.isEmpty()) {
        return;
    }

    AnalysisFrameDescriptor dmabufDescriptor = descriptor;
    dmabufDescriptor.payloadTransport = AnalysisPayloadTransport::DmaBuf;
    const QByteArray encoded = encodeAnalysisFrameDescriptor(dmabufDescriptor);

    for (int i = clients_.size() - 1; i >= 0; --i) {
        QLocalSocket *client = clients_.at(i);
        if (!client || client->state() != QLocalSocket::ConnectedState) {
            clients_.removeAt(i);
            continue;
        }
        const int fdClient = i < fdClients_.size() ? fdClients_.at(i) : -1;
        QString fdError;
        if (fdClient < 0 || !sendFileDescriptor(fdClient, fd, &fdError)) {
            continue;
        }
        client->write(encoded);
        client->flush();
    }
}

void GstreamerAnalysisOutputBackend::ensureLocalServer(QString *error) {
    if (error) {
        error->clear();
    }
    if (localServer_->isListening()) {
        return;
    }

    const QFileInfo socketInfo(socketPath());
    if (!socketInfo.absolutePath().isEmpty()) {
        QDir().mkpath(socketInfo.absolutePath());
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
            updateClientState();
        });
    }
    updateClientState();
}

void GstreamerAnalysisOutputBackend::updateClientState() {
    if (activeCameraId_.isEmpty() || !statuses_.contains(activeCameraId_)) {
        return;
    }

    AnalysisChannelStatus status = statuses_.value(activeCameraId_);
    status.streamConnected = status.enabled && !clients_.isEmpty();
    statuses_.insert(activeCameraId_, status);
}


QString GstreamerAnalysisOutputBackend::fdSocketPath() const {
    return socketPath() + QStringLiteral(".fd");
}

bool GstreamerAnalysisOutputBackend::dmabufTransportEnabled() const {
    const QString value = runtimeConfig_.analysis.transport.trimmed().toLower();
    return value == QStringLiteral("dmabuf") || value == QStringLiteral("dma");
}

void GstreamerAnalysisOutputBackend::ensureFdServer(QString *error) {
    if (error) {
        error->clear();
    }
    if (fdServerFd_ >= 0) {
        return;
    }
    const QFileInfo fdSocketInfo(fdSocketPath());
    if (!fdSocketInfo.absolutePath().isEmpty()) {
        QDir().mkpath(fdSocketInfo.absolutePath());
    }
    fdServerFd_ = createUnixStreamServerSocket(fdSocketPath(), error);
}

void GstreamerAnalysisOutputBackend::acceptPendingFdClients() {
    if (fdServerFd_ < 0) {
        return;
    }
    pollfd descriptor{};
    descriptor.fd = fdServerFd_;
    descriptor.events = POLLIN;
    while (::poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN) != 0) {
        QString error;
        const int clientFd = acceptUnixStreamClient(fdServerFd_, &error);
        if (clientFd < 0) {
            return;
        }
        fdClients_.append(clientFd);
        descriptor.revents = 0;
    }
}

void GstreamerAnalysisOutputBackend::closeFdServer() {
    for (int fd : fdClients_) {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    fdClients_.clear();
    if (fdServerFd_ >= 0) {
        ::close(fdServerFd_);
        fdServerFd_ = -1;
    }
    removeUnixStreamSocket(fdSocketPath());
}

AnalysisChannelStatus GstreamerAnalysisOutputBackend::defaultStatusForCamera(const QString &cameraId) const {
    AnalysisChannelStatus status;
    status.cameraId = cameraId;
    status.enabled = false;
    status.streamConnected = false;
    return status;
}

QString GstreamerAnalysisOutputBackend::pixelFormatName(AnalysisPixelFormat pixelFormat) const {
    switch (pixelFormat) {
    case AnalysisPixelFormat::Jpeg:
        return QStringLiteral("jpeg");
    case AnalysisPixelFormat::Nv12:
        return QStringLiteral("nv12");
    case AnalysisPixelFormat::Rgb:
        return QStringLiteral("rgb");
    }
    return QStringLiteral("unknown");
}
