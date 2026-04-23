#pragma once

#include "analysis/analysis_output_backend.h"

#include <QHash>
#include <QList>
#include <QObject>

class QLocalServer;
class QLocalSocket;

class GstreamerAnalysisOutputBackend : public QObject, public AnalysisOutputBackend {
    Q_OBJECT

public:
    explicit GstreamerAnalysisOutputBackend(QObject *parent = nullptr);
    ~GstreamerAnalysisOutputBackend() override;

    QString socketPath() const;
    bool start(const VideoChannelStatus &status, QString *error) override;
    bool stop(const QString &cameraId, QString *error) override;
    AnalysisChannelStatus statusForCamera(const QString &cameraId) const override;
    bool acceptsFrames(const QString &cameraId) const override;
    void publishFrame(const AnalysisFramePacket &packet) override;

private:
    void ensureLocalServer(QString *error);
    void onNewLocalConnection();
    void updateClientState();
    AnalysisChannelStatus defaultStatusForCamera(const QString &cameraId) const;
    QString pixelFormatName(AnalysisPixelFormat pixelFormat) const;

    QHash<QString, AnalysisChannelStatus> statuses_;
    QList<QLocalSocket *> clients_;
    QLocalServer *localServer_ = nullptr;
    QString activeCameraId_;
};
