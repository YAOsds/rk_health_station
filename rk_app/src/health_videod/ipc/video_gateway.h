#pragma once

#include "protocol/video_ipc.h"

#include <QByteArray>
#include <QHash>
#include <QObject>

class QLocalServer;
class QLocalSocket;
class VideoService;

class VideoGateway : public QObject {
    Q_OBJECT

public:
    static QString socketName();

    explicit VideoGateway(const QString &socketName, VideoService *service, QObject *parent = nullptr);
    explicit VideoGateway(VideoService *service, QObject *parent = nullptr);
    ~VideoGateway() override;

    bool start();
    void stop();

private slots:
    void onNewConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();

private:
    QByteArray encodeResult(const VideoCommandResult &result) const;
    bool decodeCommand(const QByteArray &frame, VideoCommand *command) const;
    VideoCommandResult route(const VideoCommand &command) const;
    VideoCommandResult buildStatusResult(const QString &cameraId, const QString &requestId) const;
    VideoCommandResult buildErrorResult(const QString &action, const QString &requestId,
        const QString &cameraId, const QString &errorCode) const;
    QString socketName_() const;

    VideoService *service_ = nullptr;
    QLocalServer *server_ = nullptr;
    QHash<QLocalSocket *, QByteArray> readBuffers_;
    QString socketNameValue_;
};
