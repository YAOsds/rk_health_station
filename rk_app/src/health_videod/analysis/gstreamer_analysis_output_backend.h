#pragma once

#include "analysis/analysis_output_backend.h"

#include <QHash>
#include <QList>
#include <QObject>

class QLocalServer;
class QLocalSocket;
class QTcpSocket;

class GstreamerAnalysisOutputBackend : public QObject, public AnalysisOutputBackend {
    Q_OBJECT

public:
    explicit GstreamerAnalysisOutputBackend(QObject *parent = nullptr);
    ~GstreamerAnalysisOutputBackend() override;

    QString socketPath() const;
    bool start(const VideoChannelStatus &status, QString *error) override;
    bool stop(const QString &cameraId, QString *error) override;
    AnalysisChannelStatus statusForCamera(const QString &cameraId) const override;

private:
    bool configurePreviewSource(const QString &previewUrl, QString *error);
    void ensureLocalServer(QString *error);
    void onNewLocalConnection();
    void onPreviewReadyRead();
    void processPreviewChunk(const QByteArray &chunk);
    void broadcastFrame(const QByteArray &jpegBytes);
    void setStreamConnected(bool connected);
    AnalysisChannelStatus defaultStatusForCamera(const QString &cameraId) const;

    QHash<QString, AnalysisChannelStatus> statuses_;
    QList<QLocalSocket *> clients_;
    QLocalServer *localServer_ = nullptr;
    QTcpSocket *previewSocket_ = nullptr;
    QByteArray previewBuffer_;
    QByteArray boundaryMarker_;
    QString previewHost_;
    QString activeCameraId_;
    quint16 previewPort_ = 0;
    quint64 nextFrameId_ = 1;
};
