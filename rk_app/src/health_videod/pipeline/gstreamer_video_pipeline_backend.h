#pragma once

#include "pipeline/video_pipeline_backend.h"
#include "debug/video_runtime_log_stats.h"
#include "analysis/rga_frame_converter.h"

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
    void setAnalysisFrameConverter(AnalysisFrameConverter *converter);
    bool startPreview(const VideoChannelStatus &status, QString *previewUrl, QString *error) override;
    bool stopPreview(const QString &cameraId, QString *error) override;
    bool captureSnapshot(const VideoChannelStatus &status, const QString &outputPath, QString *error) override;
    bool startRecording(const VideoChannelStatus &status, const QString &outputPath, QString *error) override;
    bool stopRecording(const QString &cameraId, QString *error) override;

private:
    enum class AnalysisConvertBackend {
        GstreamerCpu,
        Rga,
    };

    struct ActivePipeline {
        QProcess *process = nullptr;
        bool recording = false;
        bool testInput = false;
        QString previewUrl;
        QString cameraId;
        AnalysisConvertBackend analysisConvertBackend = AnalysisConvertBackend::GstreamerCpu;
        int analysisInputWidth = 0;
        int analysisInputHeight = 0;
        int analysisInputFrameBytes = 0;
        int analysisOutputWidth = 0;
        int analysisOutputHeight = 0;
        int analysisOutputFrameBytes = 0;
        quint64 nextFrameId = 1;
        QByteArray stdoutBuffer;
        SharedMemoryFrameRingWriter *frameRing = nullptr;
        VideoRuntimeLogStats logStats;
    };

    QString gstLaunchBinary() const;
    QString shellQuote(const QString &value) const;
    QString previewUrlForCamera(const QString &cameraId) const;
    QString previewBoundaryForCamera(const QString &cameraId) const;
    quint16 previewPortForCamera(const QString &cameraId) const;
    QString buildPreviewCommand(const VideoChannelStatus &status) const;
    QString buildRecordingCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    QString buildSnapshotCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    QString buildAnalysisTapCommandFragment(const VideoChannelStatus &status,
        const VideoProfile &sourceProfile) const;
    AnalysisConvertBackend analysisConvertBackendForProfile(const VideoProfile &sourceProfile) const;
    bool analysisTapEnabled(const VideoChannelStatus &status) const;
    void processAnalysisStdout(const QString &cameraId);
    bool startCommand(const QString &cameraId, const QString &command, bool recording,
        QString *previewUrl, QString *error, const VideoProfile &analysisInputProfile = VideoProfile(),
        AnalysisConvertBackend analysisConvertBackend = AnalysisConvertBackend::GstreamerCpu);
    bool runOneShotCommand(const QString &command, QString *error) const;
    bool stopActivePipeline(const QString &cameraId, QString *error);
    void stopAllPipelines();

    QHash<QString, ActivePipeline> pipelines_;
    VideoPipelineObserver *observer_ = nullptr;
    AnalysisFrameSource *analysisFrameSource_ = nullptr;
    AnalysisFrameConverter *analysisFrameConverter_ = nullptr;
    RgaFrameConverter defaultRgaFrameConverter_;
};
