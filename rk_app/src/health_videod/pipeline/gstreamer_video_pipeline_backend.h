#pragma once

#include "pipeline/video_pipeline_backend.h"

#include <QHash>
#include <QString>

class QProcess;

class GstreamerVideoPipelineBackend : public VideoPipelineBackend {
public:
    GstreamerVideoPipelineBackend();
    ~GstreamerVideoPipelineBackend() override;

    void setObserver(VideoPipelineObserver *observer) override;
    bool startPreview(const VideoChannelStatus &status, QString *previewUrl, QString *error) override;
    bool stopPreview(const QString &cameraId, QString *error) override;
    bool captureSnapshot(const VideoChannelStatus &status, const QString &outputPath, QString *error) override;
    bool startRecording(const VideoChannelStatus &status, const QString &outputPath, QString *error) override;
    bool stopRecording(const QString &cameraId, QString *error) override;

private:
    struct ActivePipeline {
        QProcess *process = nullptr;
        bool recording = false;
        bool testInput = false;
        QString previewUrl;
    };

    QString gstLaunchBinary() const;
    QString shellQuote(const QString &value) const;
    QString previewUrlForCamera(const QString &cameraId) const;
    QString previewBoundaryForCamera(const QString &cameraId) const;
    quint16 previewPortForCamera(const QString &cameraId) const;
    QString buildPreviewCommand(const VideoChannelStatus &status) const;
    QString buildRecordingCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    QString buildSnapshotCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    bool startCommand(const QString &cameraId, const QString &command, bool recording,
        QString *previewUrl, QString *error);
    bool runOneShotCommand(const QString &command, QString *error) const;
    bool stopActivePipeline(const QString &cameraId, QString *error);
    void stopAllPipelines();

    QHash<QString, ActivePipeline> pipelines_;
    VideoPipelineObserver *observer_ = nullptr;
};
