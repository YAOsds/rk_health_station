#include "pipeline/gstreamer_video_pipeline_backend.h"

#include "analysis/analysis_output_backend.h"
#include "analysis/shared_memory_frame_ring.h"
#include "pipeline/pipeline_runner_factory.h"
#include "pipeline/video_pipeline_runner.h"
#include "runtime_config/app_runtime_config_loader.h"
#if defined(RKAPP_ENABLE_INPROCESS_GSTREAMER) && RKAPP_ENABLE_INPROCESS_GSTREAMER
#include "pipeline/inprocess_gstreamer_pipeline.h"
#endif

#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <signal.h>
#include <unistd.h>

namespace {
const int kStartTimeoutMs = 5000;
const int kStopTimeoutMs = 5000;
const int kStartupProbeMs = 750;
const int kStableAnalysisTapFps = 15;
const int kAnalysisOutputWidth = 640;
const int kAnalysisOutputHeight = 640;
const quint16 kAnalysisRingSlotCount = 32;
const int kPreviewJpegQuality = 95;
const char kDefaultGstLaunchBinary[] = "gst-launch-1.0";

int nv12FrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 3 / 2 : 0;
}

int uyvyFrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 2 : 0;
}

int frameBytesForFormat(AnalysisFrameInputFormat format, int width, int height) {
    return format == AnalysisFrameInputFormat::Uyvy ? uyvyFrameBytes(width, height) : nv12FrameBytes(width, height);
}

QString gstPixelFormatForAnalysisInput(AnalysisFrameInputFormat format) {
    return format == AnalysisFrameInputFormat::Uyvy ? QStringLiteral("UYVY") : QStringLiteral("NV12");
}

int rgbFrameBytes(int width, int height) {
    return width > 0 && height > 0 ? width * height * 3 : 0;
}

}

GstreamerVideoPipelineBackend::GstreamerVideoPipelineBackend()
    : GstreamerVideoPipelineBackend(loadAppRuntimeConfig(QString()).config) {
}

GstreamerVideoPipelineBackend::GstreamerVideoPipelineBackend(const AppRuntimeConfig &runtimeConfig)
    : runtimeConfig_(runtimeConfig)
    , defaultRgaFrameConverter_(runtimeConfig)
    , commandBuilder_(runtimeConfig)
    , analysisFramePublisher_(runtimeConfig, &dmaBufferAllocator_) {
    analysisFramePublisher_.setFrameSource(analysisFrameSource_);
    analysisFramePublisher_.setFrameConverter(analysisFrameConverter_);
    analysisFramePublisher_.setFallbackFrameConverter(&defaultRgaFrameConverter_);
}

GstreamerVideoPipelineBackend::~GstreamerVideoPipelineBackend() {
    stopAllPipelines();
}

void GstreamerVideoPipelineBackend::setObserver(VideoPipelineObserver *observer) {
    observer_ = observer;
}

void GstreamerVideoPipelineBackend::setAnalysisFrameSource(AnalysisFrameSource *source) {
    analysisFrameSource_ = source;
    analysisFramePublisher_.setFrameSource(source);
}

void GstreamerVideoPipelineBackend::setAnalysisFrameConverter(AnalysisFrameConverter *converter) {
    analysisFrameConverter_ = converter;
    analysisFramePublisher_.setFrameConverter(converter);
}

bool GstreamerVideoPipelineBackend::startPreview(
    const VideoChannelStatus &status, QString *previewUrl, QString *error) {
    const QString requestedBackend = runtimeConfig_.video.pipelineBackend.trimmed().toLower();
    const bool useInprocessBackend = requestedBackend == QStringLiteral("inproc_gst")
        || requestedBackend == QStringLiteral("inprocess_gstreamer")
        || requestedBackend == QStringLiteral("inprocess");
    if (useInprocessBackend && status.inputMode != QStringLiteral("test_file")) {
#if defined(RKAPP_ENABLE_INPROCESS_GSTREAMER) && RKAPP_ENABLE_INPROCESS_GSTREAMER
        return startInProcessPreview(status, previewUrl, error);
#else
        if (error) {
            *error = QStringLiteral("inprocess_gstreamer_not_built");
        }
        if (previewUrl) {
            previewUrl->clear();
        }
        return false;
#endif
    }

    const bool enableAnalysisTap = analysisTapEnabled(status);
    const AnalysisConvertBackend backend = analysisConvertBackendForProfile(status.previewProfile);
    return startCommand(status.cameraId, buildPreviewCommand(status), false, previewUrl, error,
        enableAnalysisTap ? status.previewProfile : VideoProfile(), backend);
}


bool GstreamerVideoPipelineBackend::startInProcessPreview(
    const VideoChannelStatus &status, QString *previewUrl, QString *error) {
#if defined(RKAPP_ENABLE_INPROCESS_GSTREAMER) && RKAPP_ENABLE_INPROCESS_GSTREAMER
    if (error) {
        error->clear();
    }

    QString stopError;
    if (!stopActivePipeline(status.cameraId, &stopError) && !stopError.isEmpty()) {
        if (error) {
            *error = stopError;
        }
        return false;
    }

    const bool enableAnalysisTap = analysisTapEnabled(status);
    const AnalysisConvertBackend backend = analysisConvertBackendForProfile(status.previewProfile);
    const AnalysisFrameInputFormat analysisInputFormat = inProcessAnalysisInputFormatForBackend(backend);

    PipelineSession pipeline;
    pipeline.recording = false;
    pipeline.testInput = status.inputMode == QStringLiteral("test_file");
    pipeline.previewUrl = previewUrlForCamera(status.cameraId);
    pipeline.cameraId = status.cameraId;
    pipeline.analysisConvertBackend = backend;
    pipeline.analysisInputFormat = analysisInputFormat;
    pipeline.analysisInputWidth = status.previewProfile.width;
    pipeline.analysisInputHeight = status.previewProfile.height;
    if (enableAnalysisTap && status.previewProfile.width > 0 && status.previewProfile.height > 0) {
        pipeline.analysisInputFrameBytes = backend == AnalysisConvertBackend::Rga
            ? frameBytesForFormat(analysisInputFormat, status.previewProfile.width, status.previewProfile.height)
            : rgbFrameBytes(kAnalysisOutputWidth, kAnalysisOutputHeight);
        pipeline.analysisOutputWidth = kAnalysisOutputWidth;
        pipeline.analysisOutputHeight = kAnalysisOutputHeight;
        pipeline.analysisOutputFrameBytes = rgbFrameBytes(kAnalysisOutputWidth, kAnalysisOutputHeight);
    }

    if (pipeline.analysisOutputFrameBytes > 0) {
        pipeline.frameRing = new SharedMemoryFrameRingWriter(status.cameraId,
            kAnalysisRingSlotCount, static_cast<quint32>(pipeline.analysisOutputFrameBytes));
        QString ringError;
        if (!pipeline.frameRing->initialize(&ringError)) {
            const QString finalError = ringError.isEmpty()
                ? QStringLiteral("analysis_ring_init_failed")
                : ringError;
            qWarning().noquote()
                << QStringLiteral("video_runtime camera=%1 event=analysis_ring_init_failed error=%2")
                       .arg(status.cameraId)
                       .arg(finalError);
            if (error) {
                *error = finalError;
            }
            delete pipeline.frameRing;
            return false;
        }
    }

    auto *inprocessPipeline = new InprocessGstreamerPipeline();
    inprocessPipeline->setFrameCallback([this, cameraId = status.cameraId](const QByteArray &frame) {
        processAnalysisFrameBytes(cameraId, frame);
    });
    inprocessPipeline->setDmaFrameCallback([this, cameraId = status.cameraId](const AnalysisDmaBuffer &frame) {
        return processAnalysisFrameDma(cameraId, frame);
    });
    inprocessPipeline->setRuntimeErrorCallback([this, cameraId = status.cameraId](const QString &runtimeError) {
        if (!pipelines_.contains(cameraId)) {
            return;
        }
        qWarning().noquote()
            << QStringLiteral("video_runtime camera=%1 event=pipeline_error error=%2")
                   .arg(cameraId)
                   .arg(runtimeError);
        if (observer_) {
            observer_->onPipelineRuntimeError(cameraId, runtimeError);
        }
    });
    pipeline.inprocessPipeline = inprocessPipeline;
    pipelines_.insert(status.cameraId, pipeline);

    InprocessGstreamerPipeline::Config config;
    config.status = status;
    config.previewBoundary = previewBoundaryForCamera(status.cameraId);
    config.previewPort = previewPortForCamera(status.cameraId);
    config.analysisEnabled = pipeline.analysisOutputFrameBytes > 0;
    config.rgaAnalysis = backend == AnalysisConvertBackend::Rga;
    config.analysisOutputWidth = kAnalysisOutputWidth;
    config.analysisOutputHeight = kAnalysisOutputHeight;
    config.analysisFps = status.previewProfile.fps > 0
        ? qMin(status.previewProfile.fps, kStableAnalysisTapFps)
        : kStableAnalysisTapFps;
    config.analysisInputPixelFormat = gstPixelFormatForAnalysisInput(analysisInputFormat);
    config.analysisInputStrideBytes = strideBytesForAnalysisInputFormat(
        analysisInputFormat, status.previewProfile.width);
    config.jpegQuality = kPreviewJpegQuality;
    config.preferDmaInput = runtimeConfig_.analysis.gstDmabufInput
        && runtimeConfig_.analysis.rgaOutputDmabuf;
    config.forceDmaIo = config.preferDmaInput && runtimeConfig_.analysis.gstForceDmabufIo;

    if (!inprocessPipeline->start(config, error)) {
        PipelineSession failedPipeline = pipelines_.take(status.cameraId);
        delete failedPipeline.frameRing;
        delete failedPipeline.inprocessPipeline;
        if (previewUrl) {
            previewUrl->clear();
        }
        return false;
    }

    qInfo().noquote()
        << QStringLiteral("video_runtime camera=%1 event=preview_started mode=%2 backend=inproc_gst analysis=%3 analysis_backend=%4")
               .arg(status.cameraId)
               .arg(pipeline.testInput ? QStringLiteral("test_file") : QStringLiteral("camera"))
               .arg(pipeline.analysisOutputFrameBytes > 0 ? 1 : 0)
               .arg(pipeline.analysisOutputFrameBytes <= 0
                       ? QStringLiteral("off")
                       : (pipeline.analysisConvertBackend == AnalysisConvertBackend::Rga
                               ? QStringLiteral("rga")
                               : QStringLiteral("gstreamer_cpu")));

    if (previewUrl) {
        *previewUrl = previewUrlForCamera(status.cameraId);
    }
    return true;
#else
    Q_UNUSED(status);
    if (error) {
        *error = QStringLiteral("inprocess_gstreamer_not_built");
    }
    if (previewUrl) {
        previewUrl->clear();
    }
    return false;
#endif
}

AnalysisFrameInputFormat
GstreamerVideoPipelineBackend::inProcessAnalysisInputFormatForBackend(
    AnalysisConvertBackend backend) const {
    return backend == AnalysisConvertBackend::Rga
            && runtimeConfig_.analysis.gstDmabufInput
            && runtimeConfig_.analysis.rgaOutputDmabuf
            && runtimeConfig_.analysis.gstForceDmabufIo
        ? AnalysisFrameInputFormat::Uyvy
        : AnalysisFrameInputFormat::Nv12;
}

int GstreamerVideoPipelineBackend::strideBytesForAnalysisInputFormat(
    AnalysisFrameInputFormat format, int width) const {
    if (width <= 0) {
        return 0;
    }
    return format == AnalysisFrameInputFormat::Uyvy ? width * 2 : width;
}

bool GstreamerVideoPipelineBackend::stopPreview(const QString &cameraId, QString *error) {
    return stopActivePipeline(cameraId, error);
}

bool GstreamerVideoPipelineBackend::captureSnapshot(
    const VideoChannelStatus &status, const QString &outputPath, QString *error) {
    if (!status.previewUrl.isEmpty()) {
        QByteArray jpegBytes;
        if (!previewStreamReader_.readJpegFrame(status.previewUrl, &jpegBytes, error)) {
            return false;
        }
        QFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) {
                *error = output.errorString();
            }
            return false;
        }
        if (output.write(jpegBytes) != jpegBytes.size()) {
            if (error) {
                *error = output.errorString().isEmpty()
                    ? QStringLiteral("snapshot_write_failed")
                    : output.errorString();
            }
            return false;
        }
        if (error) {
            error->clear();
        }
        return true;
    }
    return runOneShotCommand(buildSnapshotCommand(status, outputPath), error);
}

bool GstreamerVideoPipelineBackend::startRecording(
    const VideoChannelStatus &status, const QString &outputPath, QString *error) {
    if (!status.previewUrl.isEmpty()) {
        const QString command = buildPreviewStreamRecordingCommand(status.previewUrl, outputPath, error);
        if (command.isEmpty()) {
            return false;
        }
        return startRecordingProcess(status.cameraId, command, error);
    }

    QString previewUrl;
    const bool enableAnalysisTap = analysisTapEnabled(status);
    const AnalysisConvertBackend backend = analysisConvertBackendForProfile(status.recordProfile);
    return startCommand(status.cameraId, buildRecordingCommand(status, outputPath), true,
        &previewUrl, error, enableAnalysisTap ? status.recordProfile : VideoProfile(), backend);
}

bool GstreamerVideoPipelineBackend::stopRecording(const QString &cameraId, QString *error) {
    return stopRecordingProcess(cameraId, error);
}

QString GstreamerVideoPipelineBackend::gstLaunchBinary() const {
    const QString configuredBinary = runtimeConfig_.video.gstLaunchBin.trimmed();
    return configuredBinary.isEmpty() ? QString::fromUtf8(kDefaultGstLaunchBinary) : configuredBinary;
}

QString GstreamerVideoPipelineBackend::shellQuote(const QString &value) const {
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString GstreamerVideoPipelineBackend::previewUrlForCamera(const QString &cameraId) const {
    return commandBuilder_.previewUrlForCamera(cameraId);
}

QString GstreamerVideoPipelineBackend::previewBoundaryForCamera(const QString &cameraId) const {
    return commandBuilder_.previewBoundaryForCamera(cameraId);
}

quint16 GstreamerVideoPipelineBackend::previewPortForCamera(const QString &cameraId) const {
    return commandBuilder_.previewPortForCamera(cameraId);
}

QString GstreamerVideoPipelineBackend::buildAnalysisTapCommandFragment(
    const VideoChannelStatus &status, const VideoProfile &sourceProfile) const {
    if (!analysisTapEnabled(status)) {
        return QString();
    }

    const int analysisTapFps = status.previewProfile.fps > 0
        ? qMin(status.previewProfile.fps, kStableAnalysisTapFps)
        : kStableAnalysisTapFps;

    if (analysisConvertBackendForProfile(sourceProfile) == AnalysisConvertBackend::Rga) {
        return QStringLiteral(
            " t. ! queue leaky=downstream max-size-buffers=1 ! "
            "videorate drop-only=true ! "
            "video/x-raw,format=NV12,width=%1,height=%2,framerate=%3/1 ! "
            "fdsink fd=1 sync=false")
            .arg(sourceProfile.width)
            .arg(sourceProfile.height)
            .arg(analysisTapFps);
    }

    return QStringLiteral(
        " t. ! queue leaky=downstream max-size-buffers=1 ! "
        "videorate drop-only=true ! "
        "videoconvert ! videoscale ! "
        "video/x-raw,format=RGB,width=%1,height=%2,framerate=%3/1 ! "
        "fdsink fd=1 sync=false")
        .arg(kAnalysisOutputWidth)
        .arg(kAnalysisOutputHeight)
        .arg(analysisTapFps);
}

AnalysisConvertBackend
GstreamerVideoPipelineBackend::analysisConvertBackendForProfile(const VideoProfile &sourceProfile) const {
    const QString requested = runtimeConfig_.video.analysisConvertBackend.trimmed().toLower();
    if (requested == QStringLiteral("gstreamer_cpu") || requested == QStringLiteral("cpu")) {
        return AnalysisConvertBackend::GstreamerCpu;
    }
    if (sourceProfile.pixelFormat.compare(QStringLiteral("NV12"), Qt::CaseInsensitive) != 0) {
        return AnalysisConvertBackend::GstreamerCpu;
    }
    if (requested == QStringLiteral("rga")) {
        return AnalysisConvertBackend::Rga;
    }

#if defined(RKAPP_ENABLE_RGA_ANALYSIS_CONVERT) && RKAPP_ENABLE_RGA_ANALYSIS_CONVERT
    return AnalysisConvertBackend::Rga;
#else
    return AnalysisConvertBackend::GstreamerCpu;
#endif
}

bool GstreamerVideoPipelineBackend::analysisTapEnabled(const VideoChannelStatus &status) const {
    return analysisFrameSource_ && analysisFrameSource_->acceptsFrames(status.cameraId);
}

QString GstreamerVideoPipelineBackend::buildPreviewCommand(const VideoChannelStatus &status) const {
    return commandBuilder_.buildPreviewCommand(status, analysisTapEnabled(status));
}

QString GstreamerVideoPipelineBackend::buildRecordingCommand(
    const VideoChannelStatus &status, const QString &outputPath) const {
    return commandBuilder_.buildRecordingCommand(status, outputPath, analysisTapEnabled(status));
}

QString GstreamerVideoPipelineBackend::buildSnapshotCommand(
    const VideoChannelStatus &status, const QString &outputPath) const {
    return commandBuilder_.buildSnapshotCommand(status, outputPath);
}

QString GstreamerVideoPipelineBackend::buildPreviewStreamRecordingCommand(
    const QString &previewUrl, const QString &outputPath, QString *error) const {
    return commandBuilder_.buildPreviewStreamRecordingCommand(previewUrl, outputPath, error);
}

void GstreamerVideoPipelineBackend::processAnalysisStdout(const QString &cameraId) {
    if (!pipelines_.contains(cameraId)) {
        return;
    }

    PipelineSession &pipeline = pipelines_[cameraId];
    if (!pipeline.process || pipeline.analysisInputFrameBytes <= 0) {
        if (pipeline.process) {
            pipeline.process->readAllStandardOutput();
        }
        return;
    }

    pipeline.stdoutBuffer.append(pipeline.process->readAllStandardOutput());
    while (pipeline.stdoutBuffer.size() >= pipeline.analysisInputFrameBytes) {
        const QByteArray inputFrame = pipeline.stdoutBuffer.left(pipeline.analysisInputFrameBytes);
        pipeline.stdoutBuffer.remove(0, pipeline.analysisInputFrameBytes);
        processAnalysisFrameBytes(cameraId, inputFrame);
    }
}

bool GstreamerVideoPipelineBackend::processAnalysisFrameDma(
    const QString &cameraId, const AnalysisDmaBuffer &inputFrame) {
    if (!pipelines_.contains(cameraId) || !analysisFrameSource_
        || !analysisFrameSource_->supportsDmaBufFrames()) {
        return false;
    }

    PipelineSession &pipeline = pipelines_[cameraId];
    return analysisFramePublisher_.publishFrameDma(&pipeline, inputFrame);
}

void GstreamerVideoPipelineBackend::processAnalysisFrameBytes(
    const QString &cameraId, const QByteArray &inputFrame) {
    if (!pipelines_.contains(cameraId)) {
        return;
    }

    PipelineSession &pipeline = pipelines_[cameraId];
    analysisFramePublisher_.publishFrameBytes(&pipeline, inputFrame);
}

bool GstreamerVideoPipelineBackend::startCommand(const QString &cameraId, const QString &command,
    bool recording, QString *previewUrl, QString *error, const VideoProfile &analysisInputProfile,
    AnalysisConvertBackend analysisConvertBackend) {
    if (error) {
        error->clear();
    }

    QString stopError;
    if (!stopActivePipeline(cameraId, &stopError) && !stopError.isEmpty()) {
        if (error) {
            *error = stopError;
        }
        return false;
    }

    const GstProcessRunner::Callbacks callbacks{
        [this, cameraId]() {
            processAnalysisStdout(cameraId);
        },
        [this, cameraId](int exitCode, QProcess::ExitStatus exitStatus) {
            if (!pipelines_.contains(cameraId)) {
                return;
            }

            const PipelineSession pipeline = pipelines_.take(cameraId);
            const bool testInput = pipeline.testInput;
            if (pipeline.recordingProcess != nullptr) {
                QObject::disconnect(pipeline.recordingProcess, nullptr, nullptr, nullptr);
                const qint64 recordingPid = pipeline.recordingProcess->processId();
                if (recordingPid > 0) {
                    ::kill(static_cast<pid_t>(recordingPid), SIGINT);
                }
                if (!pipeline.recordingProcess->waitForFinished(kStopTimeoutMs)) {
                    pipeline.recordingProcess->kill();
                    pipeline.recordingProcess->waitForFinished(kStopTimeoutMs);
                }
                delete pipeline.recordingProcess;
            }
            delete pipeline.frameRing;
            if (pipeline.previewRunner) {
                pipeline.previewRunner->deleteLater();
            }

            if (!observer_) {
                return;
            }
            if (testInput && exitStatus == QProcess::NormalExit && exitCode == 0) {
                qInfo().noquote()
                    << QStringLiteral("video_runtime camera=%1 event=playback_finished")
                           .arg(cameraId);
                observer_->onPipelinePlaybackFinished(cameraId);
                return;
            }
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                qWarning().noquote()
                    << QStringLiteral("video_runtime camera=%1 event=pipeline_error error=%2")
                           .arg(cameraId)
                           .arg(QStringLiteral("preview_pipeline_failed"));
                observer_->onPipelineRuntimeError(cameraId, QStringLiteral("preview_pipeline_failed"));
            }
        },
    };

    PipelineSession pipeline;
    pipeline.recording = recording;
    pipeline.testInput = command.contains(QStringLiteral("filesrc location="));
    pipeline.previewUrl = previewUrlForCamera(cameraId);
    pipeline.cameraId = cameraId;
    pipeline.analysisConvertBackend = analysisConvertBackend;
    pipeline.analysisInputWidth = analysisInputProfile.width;
    pipeline.analysisInputHeight = analysisInputProfile.height;
    pipeline.analysisInputFrameBytes = 0;
    if (analysisInputProfile.width > 0 && analysisInputProfile.height > 0) {
        pipeline.analysisInputFrameBytes = analysisConvertBackend == AnalysisConvertBackend::Rga
            ? nv12FrameBytes(analysisInputProfile.width, analysisInputProfile.height)
            : rgbFrameBytes(kAnalysisOutputWidth, kAnalysisOutputHeight);
        pipeline.analysisOutputWidth = kAnalysisOutputWidth;
        pipeline.analysisOutputHeight = kAnalysisOutputHeight;
        pipeline.analysisOutputFrameBytes = rgbFrameBytes(kAnalysisOutputWidth, kAnalysisOutputHeight);
    }
    if (pipeline.analysisOutputFrameBytes > 0) {
        pipeline.frameRing = new SharedMemoryFrameRingWriter(
            cameraId, kAnalysisRingSlotCount, static_cast<quint32>(pipeline.analysisOutputFrameBytes));
        QString ringError;
        if (!pipeline.frameRing->initialize(&ringError)) {
            const QString finalError = ringError.isEmpty()
                ? QStringLiteral("analysis_ring_init_failed")
                : ringError;
            qWarning().noquote()
                << QStringLiteral("video_runtime camera=%1 event=analysis_ring_init_failed error=%2")
                       .arg(cameraId)
                       .arg(finalError);
            if (error) {
                *error = finalError;
            }
            delete pipeline.frameRing;
            return false;
        }
    }
    pipeline.previewRunner = PipelineRunnerFactory(runtimeConfig_).createPreviewRunner(
        command, QProcess::SeparateChannels, callbacks);
    pipelines_.insert(cameraId, pipeline);
    if (!pipelines_[cameraId].previewRunner->startPreview(pipelines_[cameraId], error)) {
        PipelineSession failedPipeline = pipelines_.take(cameraId);
        delete failedPipeline.frameRing;
        delete failedPipeline.previewRunner;
        return false;
    }
    processAnalysisStdout(cameraId);

    qInfo().noquote()
        << QStringLiteral("video_runtime camera=%1 event=preview_started mode=%2 analysis=%3 analysis_backend=%4")
               .arg(cameraId)
               .arg(pipeline.testInput ? QStringLiteral("test_file") : QStringLiteral("camera"))
               .arg(pipeline.analysisOutputFrameBytes > 0 ? 1 : 0)
               .arg(pipeline.analysisOutputFrameBytes <= 0
                       ? QStringLiteral("off")
                       : (pipeline.analysisConvertBackend == AnalysisConvertBackend::Rga
                               ? QStringLiteral("rga")
                               : QStringLiteral("gstreamer_cpu")));

    if (previewUrl) {
        *previewUrl = pipeline.previewUrl;
    }
    return true;
}

bool GstreamerVideoPipelineBackend::startRecordingProcess(
    const QString &cameraId, const QString &command, QString *error) {
    if (error) {
        error->clear();
    }
    if (!pipelines_.contains(cameraId)) {
        if (error) {
            *error = QStringLiteral("preview_not_running");
        }
        return false;
    }

    PipelineSession &pipeline = pipelines_[cameraId];
    if (pipeline.recordingProcess != nullptr) {
        if (error) {
            *error = QStringLiteral("already_recording");
        }
        return false;
    }

    auto *process = new QProcess();
    process->setProgram(QStringLiteral("/bin/bash"));
    process->setArguments({QStringLiteral("-lc"), QStringLiteral("exec %1").arg(command)});
    process->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(process,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, cameraId, process](int exitCode, QProcess::ExitStatus exitStatus) {
            if (!pipelines_.contains(cameraId)) {
                delete process;
                return;
            }
            PipelineSession &pipeline = pipelines_[cameraId];
            if (pipeline.recordingProcess != process) {
                delete process;
                return;
            }
            pipeline.recordingProcess = nullptr;
            pipeline.recording = false;
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                qWarning().noquote()
                    << QStringLiteral("video_runtime camera=%1 event=recording_pipeline_error error=%2")
                           .arg(cameraId)
                           .arg(QStringLiteral("recording_pipeline_failed"));
                if (observer_) {
                    observer_->onPipelineRuntimeError(cameraId, QStringLiteral("recording_pipeline_failed"));
                }
            }
            delete process;
        });

    process->start();
    if (!process->waitForStarted(kStartTimeoutMs)) {
        if (error) {
            *error = process->errorString();
        }
        delete process;
        return false;
    }
    if (process->waitForFinished(kStartupProbeMs)) {
        if (error) {
            const QString startupOutput = QString::fromUtf8(process->readAll()).trimmed();
            *error = startupOutput.isEmpty()
                ? QStringLiteral("recording_pipeline_exited_during_startup")
                : startupOutput;
        }
        delete process;
        return false;
    }

    pipeline.recordingProcess = process;
    pipeline.recording = true;
    return true;
}

bool GstreamerVideoPipelineBackend::stopRecordingProcess(const QString &cameraId, QString *error) {
    if (error) {
        error->clear();
    }
    if (!pipelines_.contains(cameraId)) {
        return true;
    }

    PipelineSession &pipeline = pipelines_[cameraId];
    QProcess *process = pipeline.recordingProcess;
    if (process == nullptr) {
        pipeline.recording = false;
        return true;
    }

    const qint64 processId = process->processId();
    QObject::disconnect(process, nullptr, nullptr, nullptr);
    pipeline.recordingProcess = nullptr;
    pipeline.recording = false;
    if (processId > 0) {
        ::kill(static_cast<pid_t>(processId), SIGINT);
    }
    if (!process->waitForFinished(kStopTimeoutMs)) {
        process->kill();
        process->waitForFinished(kStopTimeoutMs);
    }
    if (process->state() != QProcess::NotRunning && error) {
        *error = QStringLiteral("recording_pipeline_stop_failed");
    }
    delete process;
    return error ? error->isEmpty() : true;
}

bool GstreamerVideoPipelineBackend::runOneShotCommand(const QString &command, QString *error) const {
    QProcess process;
    process.setProgram(QStringLiteral("/bin/bash"));
    process.setArguments({QStringLiteral("-lc"), command});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(kStartTimeoutMs)) {
        if (error) {
            *error = process.errorString();
        }
        return false;
    }
    if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        if (error) {
            *error = QString::fromUtf8(process.readAll()).trimmed();
            if (error->isEmpty()) {
                *error = QStringLiteral("snapshot_command_failed");
            }
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool GstreamerVideoPipelineBackend::stopActivePipeline(const QString &cameraId, QString *error) {
    if (error) {
        error->clear();
    }

    if (!pipelines_.contains(cameraId)) {
        return true;
    }

    QString recordingStopError;
    if (!stopRecordingProcess(cameraId, &recordingStopError) && error && error->isEmpty()) {
        *error = recordingStopError;
    }

    PipelineSession pipeline = pipelines_.take(cameraId);
#if defined(RKAPP_ENABLE_INPROCESS_GSTREAMER) && RKAPP_ENABLE_INPROCESS_GSTREAMER
    if (pipeline.inprocessPipeline) {
        pipeline.inprocessPipeline->stop();
        delete pipeline.inprocessPipeline;
        delete pipeline.frameRing;
        qInfo().noquote()
            << QStringLiteral("video_runtime camera=%1 event=preview_stopped").arg(cameraId);
        return true;
    }
#endif
    if (!pipeline.previewRunner) {
        delete pipeline.frameRing;
        return true;
    }

    pipeline.previewRunner->stopPreview(pipeline, error);
    qInfo().noquote()
        << QStringLiteral("video_runtime camera=%1 event=preview_stopped").arg(cameraId);
    delete pipeline.frameRing;
    delete pipeline.previewRunner;
    return error ? error->isEmpty() : true;
}

void GstreamerVideoPipelineBackend::stopAllPipelines() {
    const auto cameraIds = pipelines_.keys();
    for (const QString &cameraId : cameraIds) {
        QString error;
        stopActivePipeline(cameraId, &error);
    }
}
