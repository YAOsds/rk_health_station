#include "ingest/analysis_stream_client.h"

#include "debug/latency_marker_writer.h"
#include "protocol/analysis_frame_descriptor_protocol.h"
#include "protocol/unix_fd_passing.h"
#include "runtime/runtime_config.h"

#include <QDateTime>
#include <QLocalSocket>
#include <QTimer>

#include <poll.h>
#include <unistd.h>

namespace {
constexpr int kReconnectDelayMs = 300;
constexpr int kMaxBufferedBytes = 1024 * 1024;
}

AnalysisStreamClient::AnalysisStreamClient(const FallRuntimeConfig &config, QObject *parent)
    : QObject(parent)
    , socketName_(config.analysisSocketPath)
    , socket_(new QLocalSocket(this))
    , reconnectTimer_(new QTimer(this))
    , reader_(config.analysisSharedMemoryName)
    , analysisTransport_(config.analysisTransport)
    , latencyMarkerPath_(config.latencyMarkerPath) {
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &AnalysisStreamClient::attemptConnect);
    connect(socket_, &QLocalSocket::readyRead, this, &AnalysisStreamClient::onReadyRead);
    connect(socket_, &QLocalSocket::connected, this, [this]() {
        reconnectTimer_->stop();
        emit statusChanged(true);
    });
    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
        // Each socket session must start with a clean packet buffer.
        readBuffer_.clear();
        emit statusChanged(false);
    });
    connect(socket_, &QLocalSocket::stateChanged, this, [this](QLocalSocket::LocalSocketState state) {
        if (state == QLocalSocket::UnconnectedState) {
            scheduleReconnect();
        }
    });
    connect(socket_,
        static_cast<void (QLocalSocket::*)(QLocalSocket::LocalSocketError)>(&QLocalSocket::errorOccurred),
        this,
        [this](QLocalSocket::LocalSocketError) {
            if (socket_->state() == QLocalSocket::UnconnectedState) {
                scheduleReconnect();
            }
        });
}

AnalysisStreamClient::AnalysisStreamClient(
    const QString &socketName, const QString &sharedMemoryNameOverride, QObject *parent)
    : AnalysisStreamClient([&]() {
          FallRuntimeConfig config;
          config.analysisSocketPath = socketName;
          config.analysisSharedMemoryName = sharedMemoryNameOverride;
          return config;
      }(),
          parent) {
}

void AnalysisStreamClient::start() {
    qRegisterMetaType<AnalysisFramePacket>("AnalysisFramePacket");
    running_ = true;
    attemptConnect();
}

void AnalysisStreamClient::stop() {
    running_ = false;
    reconnectTimer_->stop();
    socket_->abort();
    resetFdSocket();
    readBuffer_.clear();
}

void AnalysisStreamClient::attemptConnect() {
    if (!running_) {
        return;
    }

    if (dmabufTransportEnabled() && fdSocketFd_ < 0) {
        QString error;
        fdSocketFd_ = connectUnixStreamSocket(fdSocketName(), &error);
        if (fdSocketFd_ < 0 && !error.isEmpty()) {
            qWarning() << "analysis_stream_client: failed to connect fd socket"
                       << fdSocketName() << error;
        }
    }

    if (socket_->state() == QLocalSocket::ConnectedState
        || socket_->state() == QLocalSocket::ConnectingState) {
        return;
    }

    socket_->abort();
    socket_->connectToServer(socketName_);
}

void AnalysisStreamClient::scheduleReconnect() {
    if (!running_ || reconnectTimer_->isActive() || socket_->state() == QLocalSocket::ConnectedState) {
        return;
    }

    reconnectTimer_->start(kReconnectDelayMs);
}

void AnalysisStreamClient::onReadyRead() {
    readBuffer_.append(socket_->readAll());
    if (readBuffer_.size() > kMaxBufferedBytes) {
        readBuffer_.clear();
        socket_->disconnectFromServer();
        return;
    }

    AnalysisFrameDescriptor descriptor;
    while (takeFirstAnalysisFrameDescriptor(&readBuffer_, &descriptor)) {
        AnalysisFramePacket packet;
        QString error;
        bool readOk = false;
        if (descriptor.payloadTransport == AnalysisPayloadTransport::DmaBuf) {
            if (!dmabufTransportEnabled()) {
                qWarning() << "dmabuf disabled";
                continue;
            }
            if (fdSocketFd_ < 0) {
                continue;
            }
            pollfd pollDescriptor{};
            pollDescriptor.fd = fdSocketFd_;
            pollDescriptor.events = POLLIN;
            if (::poll(&pollDescriptor, 1, 500) <= 0) {
                continue;
            }
            const int receivedFd = receiveFileDescriptor(fdSocketFd_, &error);
            if (receivedFd < 0) {
                continue;
            }
            readOk = dmaBufReader_.read(descriptor, receivedFd, &packet, &error);
            ::close(receivedFd);
        } else {
            readOk = reader_.read(descriptor, &packet, &error);
        }
        if (!readOk) {
            continue;
        }
        LatencyMarkerWriter marker(latencyMarkerPath_);
        marker.writeEvent(QStringLiteral("analysis_descriptor_ingested"),
            QDateTime::currentMSecsSinceEpoch(),
            QJsonObject{
                {QStringLiteral("camera_id"), packet.cameraId},
                {QStringLiteral("frame_id"), QString::number(packet.frameId)},
            });
        emit frameReceived(packet);
    }
}

bool AnalysisStreamClient::dmabufTransportEnabled() const {
    const QString value = analysisTransport_.trimmed().toLower();
    return value == QStringLiteral("dmabuf") || value == QStringLiteral("dma");
}

QString AnalysisStreamClient::fdSocketName() const {
    return socketName_ + QStringLiteral(".fd");
}

void AnalysisStreamClient::resetFdSocket() {
    if (fdSocketFd_ >= 0) {
        ::close(fdSocketFd_);
        fdSocketFd_ = -1;
    }
}
