#pragma once

#include "models/fall_models.h"

#include <QObject>

class QLocalSocket;
class QTimer;

class AnalysisStreamClient : public QObject {
    Q_OBJECT

public:
    explicit AnalysisStreamClient(const QString &socketName, QObject *parent = nullptr);

    void start();
    void stop();

signals:
    void frameReceived(const AnalysisFramePacket &packet);
    void statusChanged(bool connected);

private:
    void attemptConnect();
    void scheduleReconnect();
    void onReadyRead();

    QString socketName_;
    QLocalSocket *socket_ = nullptr;
    QTimer *reconnectTimer_ = nullptr;
    QByteArray readBuffer_;
    bool running_ = false;
};
