#pragma once

#include "models/fall_models.h"

#include <QObject>

class QLocalSocket;

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
    void onReadyRead();

    QString socketName_;
    QLocalSocket *socket_ = nullptr;
    QByteArray readBuffer_;
};
