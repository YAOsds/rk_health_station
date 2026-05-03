#pragma once

#include "analysis/analysis_frame_converter.h"
#include "debug/video_runtime_log_stats.h"

#include <QByteArray>
#include <QString>

class QProcess;
class SharedMemoryFrameRingWriter;
class InprocessGstreamerPipeline;
class VideoPipelineRunner;

enum class AnalysisConvertBackend {
    GstreamerCpu,
    Rga,
};

struct PipelineSession {
    QProcess *process = nullptr;
    QProcess *recordingProcess = nullptr;
    VideoPipelineRunner *previewRunner = nullptr;
#if defined(RKAPP_ENABLE_INPROCESS_GSTREAMER) && RKAPP_ENABLE_INPROCESS_GSTREAMER
    InprocessGstreamerPipeline *inprocessPipeline = nullptr;
#endif
    bool recording = false;
    bool testInput = false;
    QString previewUrl;
    QString cameraId;
    AnalysisConvertBackend analysisConvertBackend = AnalysisConvertBackend::GstreamerCpu;
    AnalysisFrameInputFormat analysisInputFormat = AnalysisFrameInputFormat::Nv12;
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

    QString inputModeName() const {
        return testInput ? QStringLiteral("test_file") : QStringLiteral("camera");
    }

    QString pipelineStateName() const {
        return recording ? QStringLiteral("recording") : QStringLiteral("previewing");
    }
};
