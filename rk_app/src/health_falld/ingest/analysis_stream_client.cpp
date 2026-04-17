#include "ingest/analysis_stream_client.h"

#include "protocol/analysis_stream_protocol.h"

#include <QLocalSocket>

AnalysisStreamClient::AnalysisStreamClient(const QString &socketName, QObject *parent)
    : QObject(parent)
    , socketName_(socketName)
    , socket_(new QLocalSocket(this)) {
    connect(socket_, &QLocalSocket::readyRead, this, &AnalysisStreamClient::onReadyRead);
    connect(socket_, &QLocalSocket::connected, this, [this]() {
        emit statusChanged(true);
    });
    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
        emit statusChanged(false);
    });
}

void AnalysisStreamClient::start() {
    qRegisterMetaType<AnalysisFramePacket>("AnalysisFramePacket");
    socket_->connectToServer(socketName_);
}

void AnalysisStreamClient::stop() {
    socket_->abort();
    readBuffer_.clear();
}

void AnalysisStreamClient::onReadyRead() {
    readBuffer_.append(socket_->readAll());

    AnalysisFramePacket packet;
    while (takeFirstAnalysisFramePacket(&readBuffer_, &packet)) {
        emit frameReceived(packet);
    }
}
