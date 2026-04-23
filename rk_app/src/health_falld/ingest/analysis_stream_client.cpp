#include "ingest/analysis_stream_client.h"

#include "protocol/analysis_frame_descriptor_protocol.h"

#include <QLocalSocket>
#include <QTimer>

namespace {
constexpr int kReconnectDelayMs = 300;
}

AnalysisStreamClient::AnalysisStreamClient(
    const QString &socketName, const QString &sharedMemoryNameOverride, QObject *parent)
    : QObject(parent)
    , socketName_(socketName)
    , socket_(new QLocalSocket(this))
    , reconnectTimer_(new QTimer(this))
    , reader_(sharedMemoryNameOverride) {
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

void AnalysisStreamClient::start() {
    qRegisterMetaType<AnalysisFramePacket>("AnalysisFramePacket");
    running_ = true;
    attemptConnect();
}

void AnalysisStreamClient::stop() {
    running_ = false;
    reconnectTimer_->stop();
    socket_->abort();
    readBuffer_.clear();
}

void AnalysisStreamClient::attemptConnect() {
    if (!running_ || socket_->state() == QLocalSocket::ConnectedState
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

    AnalysisFrameDescriptor descriptor;
    AnalysisFramePacket latestPacket;
    bool hasPacket = false;
    while (takeFirstAnalysisFrameDescriptor(&readBuffer_, &descriptor)) {
        AnalysisFramePacket packet;
        QString error;
        if (!reader_.read(descriptor, &packet, &error)) {
            continue;
        }
        latestPacket = packet;
        hasPacket = true;
    }

    if (hasPacket) {
        emit frameReceived(latestPacket);
    }
}
