#pragma once

#include "ingest/dmabuf_frame_reader.h"
#include "ingest/shared_memory_frame_reader.h"
#include "models/fall_models.h"

#include <QObject>

class QLocalSocket;
class QTimer;

class AnalysisStreamClient : public QObject {
    Q_OBJECT

public:
    explicit AnalysisStreamClient(const QString &socketName,
        const QString &sharedMemoryNameOverride = QString(), QObject *parent = nullptr);

    void start();
    void stop();

signals:
    void frameReceived(const AnalysisFramePacket &packet);
    void statusChanged(bool connected);

private:
    void attemptConnect();
    void scheduleReconnect();
    void onReadyRead();
    bool dmabufTransportEnabled() const;
    QString fdSocketName() const;
    void resetFdSocket();

    QString socketName_;
    QLocalSocket *socket_ = nullptr;
    int fdSocketFd_ = -1;
    QTimer *reconnectTimer_ = nullptr;
    QByteArray readBuffer_;
    SharedMemoryFrameReader reader_;
    DmaBufFrameReader dmaBufReader_;
    bool running_ = false;
};
