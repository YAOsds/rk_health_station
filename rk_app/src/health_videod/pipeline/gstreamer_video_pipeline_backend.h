#pragma once

#include "pipeline/video_pipeline_backend.h"

#include <QByteArray>
#include <QHash>
#include <QString>

class QProcess;
class SharedMemoryFrameRingWriter;

class GstreamerVideoPipelineBackend : public VideoPipelineBackend {
public:
    GstreamerVideoPipelineBackend();
    ~GstreamerVideoPipelineBackend() override;

    void setObserver(VideoPipelineObserver *observer) override;
    void setAnalysisFrameSource(AnalysisFrameSource *source) override;
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
        QString cameraId;
        int analysisWidth = 0;
        int analysisHeight = 0;
        int analysisFrameBytes = 0;
        quint64 nextFrameId = 1;
        QByteArray stdoutBuffer;
        SharedMemoryFrameRingWriter *frameRing = nullptr;
    };

    QString gstLaunchBinary() const;
    QString shellQuote(const QString &value) const;
    QString previewUrlForCamera(const QString &cameraId) const;
    QString previewBoundaryForCamera(const QString &cameraId) const;
    quint16 previewPortForCamera(const QString &cameraId) const;
    QString buildPreviewCommand(const VideoChannelStatus &status) const;
    QString buildRecordingCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    QString buildSnapshotCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    QString buildAnalysisTapCommandFragment(const VideoChannelStatus &status) const;
    bool analysisTapEnabled(const VideoChannelStatus &status) const;
    void processAnalysisStdout(const QString &cameraId);
    bool startCommand(const QString &cameraId, const QString &command, bool recording,
        QString *previewUrl, QString *error, int analysisWidth = 0, int analysisHeight = 0);
    bool runOneShotCommand(const QString &command, QString *error) const;
    bool stopActivePipeline(const QString &cameraId, QString *error);
    void stopAllPipelines();

    QHash<QString, ActivePipeline> pipelines_;
    VideoPipelineObserver *observer_ = nullptr;
    AnalysisFrameSource *analysisFrameSource_ = nullptr;
};
