#pragma once

#include "analysis/analysis_output_backend.h"
#include "runtime_config/app_runtime_config.h"

#include <QHash>
#include <QList>
#include <QObject>

class QLocalServer;
class QLocalSocket;

class GstreamerAnalysisOutputBackend : public QObject, public AnalysisOutputBackend {
    Q_OBJECT

public:
    explicit GstreamerAnalysisOutputBackend(QObject *parent = nullptr);
    explicit GstreamerAnalysisOutputBackend(const AppRuntimeConfig &runtimeConfig, QObject *parent = nullptr);
    ~GstreamerAnalysisOutputBackend() override;

    QString socketPath() const;
    bool start(const VideoChannelStatus &status, QString *error) override;
    bool stop(const QString &cameraId, QString *error) override;
    AnalysisChannelStatus statusForCamera(const QString &cameraId) const override;
    bool acceptsFrames(const QString &cameraId) const override;
    bool supportsDmaBufFrames() const override;
    void publishDescriptor(const AnalysisFrameDescriptor &descriptor) override;
    void publishDmaBufDescriptor(const AnalysisFrameDescriptor &descriptor, int fd) override;

private:
    void ensureLocalServer(QString *error);
    void onNewLocalConnection();
    QString fdSocketPath() const;
    bool dmabufTransportEnabled() const;
    void ensureFdServer(QString *error);
    void acceptPendingFdClients();
    void closeFdServer();
    void updateClientState();
    AnalysisChannelStatus defaultStatusForCamera(const QString &cameraId) const;
    QString pixelFormatName(AnalysisPixelFormat pixelFormat) const;

    QHash<QString, AnalysisChannelStatus> statuses_;
    QList<QLocalSocket *> clients_;
    QList<int> fdClients_;
    int fdServerFd_ = -1;
    QLocalServer *localServer_ = nullptr;
    QString activeCameraId_;
    AppRuntimeConfig runtimeConfig_;
};
