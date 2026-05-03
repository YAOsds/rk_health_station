#pragma once

#include "pipeline/analysis_frame_publisher.h"
#include "pipeline/dma_buffer_allocator.h"
#include "pipeline/gst_command_builder.h"
#include "pipeline/pipeline_session.h"
#include "pipeline/video_pipeline_backend.h"
#include "pipeline/preview_stream_reader.h"
#include "analysis/rga_frame_converter.h"
#include "runtime_config/app_runtime_config.h"

#include <QByteArray>
#include <QHash>
#include <QString>

class QProcess;
class SharedMemoryFrameRingWriter;
class InprocessGstreamerPipeline;

class GstreamerVideoPipelineBackend : public VideoPipelineBackend {
public:
    GstreamerVideoPipelineBackend();
    explicit GstreamerVideoPipelineBackend(const AppRuntimeConfig &runtimeConfig);
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
    friend class GstreamerVideoPipelineBackendTest;

    QString gstLaunchBinary() const;
    QString shellQuote(const QString &value) const;
    QString previewUrlForCamera(const QString &cameraId) const;
    QString previewBoundaryForCamera(const QString &cameraId) const;
    quint16 previewPortForCamera(const QString &cameraId) const;
    QString buildPreviewCommand(const VideoChannelStatus &status) const;
    QString buildRecordingCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    QString buildSnapshotCommand(const VideoChannelStatus &status, const QString &outputPath) const;
    QString buildPreviewStreamRecordingCommand(
        const QString &previewUrl, const QString &outputPath, QString *error) const;
    QString buildAnalysisTapCommandFragment(const VideoChannelStatus &status,
        const VideoProfile &sourceProfile) const;
    AnalysisConvertBackend analysisConvertBackendForProfile(const VideoProfile &sourceProfile) const;
    AnalysisFrameInputFormat inProcessAnalysisInputFormatForBackend(
        AnalysisConvertBackend backend) const;
    int strideBytesForAnalysisInputFormat(AnalysisFrameInputFormat format, int width) const;
    bool analysisTapEnabled(const VideoChannelStatus &status) const;
    bool startInProcessPreview(const VideoChannelStatus &status, QString *previewUrl, QString *error);
    void processAnalysisStdout(const QString &cameraId);
    void processAnalysisFrameBytes(const QString &cameraId, const QByteArray &inputFrame);
    bool processAnalysisFrameDma(const QString &cameraId, const AnalysisDmaBuffer &inputFrame);
    bool startCommand(const QString &cameraId, const QString &command, bool recording,
        QString *previewUrl, QString *error, const VideoProfile &analysisInputProfile = VideoProfile(),
        AnalysisConvertBackend analysisConvertBackend = AnalysisConvertBackend::GstreamerCpu);
    bool startRecordingProcess(
        const QString &cameraId, const QString &command, QString *error);
    bool stopRecordingProcess(const QString &cameraId, QString *error);
    bool runOneShotCommand(const QString &command, QString *error) const;
    bool stopActivePipeline(const QString &cameraId, QString *error);
    void stopAllPipelines();

    QHash<QString, PipelineSession> pipelines_;
    AppRuntimeConfig runtimeConfig_;
    VideoPipelineObserver *observer_ = nullptr;
    AnalysisFrameSource *analysisFrameSource_ = nullptr;
    AnalysisFrameConverter *analysisFrameConverter_ = nullptr;
    RgaFrameConverter defaultRgaFrameConverter_;
    GstCommandBuilder commandBuilder_;
    DmaBufferAllocator dmaBufferAllocator_;
    PreviewStreamReader previewStreamReader_;
    AnalysisFramePublisher analysisFramePublisher_;
};
