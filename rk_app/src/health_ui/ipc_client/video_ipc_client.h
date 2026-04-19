#pragma once

#include "protocol/video_ipc.h"

#include <QByteArray>
#include <QObject>

class QLocalSocket;

class AbstractVideoClient : public QObject {
    Q_OBJECT

public:
    explicit AbstractVideoClient(QObject *parent = nullptr)
        : QObject(parent) {
    }
    ~AbstractVideoClient() override = default;

    virtual bool connectToBackend() = 0;
    virtual void requestStatus(const QString &cameraId) = 0;
    virtual void takeSnapshot(const QString &cameraId) = 0;
    virtual void startRecording(const QString &cameraId) = 0;
    virtual void stopRecording(const QString &cameraId) = 0;
    virtual void setStorageDir(const QString &cameraId, const QString &storageDir) = 0;
    virtual void startTestInput(const QString &cameraId, const QString &filePath) = 0;
    virtual void stopTestInput(const QString &cameraId) = 0;

signals:
    void statusReceived(const VideoChannelStatus &status);
    void commandFinished(const VideoCommandResult &result);
};

class VideoIpcClient : public AbstractVideoClient {
    Q_OBJECT

public:
    explicit VideoIpcClient(QObject *parent = nullptr);

    bool connectToBackend() override;
    void requestStatus(const QString &cameraId) override;
    void takeSnapshot(const QString &cameraId) override;
    void startRecording(const QString &cameraId) override;
    void stopRecording(const QString &cameraId) override;
    void setStorageDir(const QString &cameraId, const QString &storageDir) override;
    void startTestInput(const QString &cameraId, const QString &filePath) override;
    void stopTestInput(const QString &cameraId) override;

private slots:
    void onReadyRead();

private:
    void sendCommand(const QString &action, const QString &cameraId,
        const QJsonObject &payload = QJsonObject());

    QLocalSocket *socket_ = nullptr;
    QByteArray readBuffer_;
    int nextRequestId_ = 1;
};
