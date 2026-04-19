#include "pipeline/gstreamer_video_pipeline_backend.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <signal.h>

namespace {
const int kStartTimeoutMs = 5000;
const int kStopTimeoutMs = 5000;
const int kStartupProbeMs = 750;
const char kGstLaunchEnvVar[] = "RK_VIDEO_GST_LAUNCH_BIN";
const char kDefaultGstLaunchBinary[] = "gst-launch-1.0";
}

GstreamerVideoPipelineBackend::GstreamerVideoPipelineBackend() = default;

GstreamerVideoPipelineBackend::~GstreamerVideoPipelineBackend() {
    stopAllPipelines();
}

void GstreamerVideoPipelineBackend::setObserver(VideoPipelineObserver *observer) {
    observer_ = observer;
}

bool GstreamerVideoPipelineBackend::startPreview(
    const VideoChannelStatus &status, QString *previewUrl, QString *error) {
    return startCommand(status.cameraId, buildPreviewCommand(status), false, previewUrl, error);
}

bool GstreamerVideoPipelineBackend::stopPreview(const QString &cameraId, QString *error) {
    return stopActivePipeline(cameraId, error);
}

bool GstreamerVideoPipelineBackend::captureSnapshot(
    const VideoChannelStatus &status, const QString &outputPath, QString *error) {
    return runOneShotCommand(buildSnapshotCommand(status, outputPath), error);
}

bool GstreamerVideoPipelineBackend::startRecording(
    const VideoChannelStatus &status, const QString &outputPath, QString *error) {
    QString previewUrl;
    return startCommand(status.cameraId, buildRecordingCommand(status, outputPath), true,
        &previewUrl, error);
}

bool GstreamerVideoPipelineBackend::stopRecording(const QString &cameraId, QString *error) {
    return stopActivePipeline(cameraId, error);
}

QString GstreamerVideoPipelineBackend::gstLaunchBinary() const {
    const QString overrideBinary = qEnvironmentVariable(kGstLaunchEnvVar);
    return overrideBinary.isEmpty() ? QString::fromUtf8(kDefaultGstLaunchBinary) : overrideBinary;
}

QString GstreamerVideoPipelineBackend::shellQuote(const QString &value) const {
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString GstreamerVideoPipelineBackend::previewUrlForCamera(const QString &cameraId) const {
    return QStringLiteral("tcp://127.0.0.1:%1?transport=tcp_mjpeg&boundary=%2")
        .arg(previewPortForCamera(cameraId))
        .arg(previewBoundaryForCamera(cameraId));
}

QString GstreamerVideoPipelineBackend::previewBoundaryForCamera(const QString &cameraId) const {
    Q_UNUSED(cameraId);
    return QStringLiteral("rkpreview");
}

quint16 GstreamerVideoPipelineBackend::previewPortForCamera(const QString &cameraId) const {
    if (cameraId == QStringLiteral("front_cam")) {
        return 5602;
    }
    return 5699;
}

QString GstreamerVideoPipelineBackend::buildPreviewCommand(const VideoChannelStatus &status) const {
    if (status.inputMode == QStringLiteral("test_file")) {
        return QStringLiteral(
            "%1 -e filesrc location=%2 ! qtdemux ! decodebin ! videoconvert ! videoscale ! "
            "video/x-raw,width=%3,height=%4 ! jpegenc ! multipartmux boundary=%5 ! "
            "tcpserversink host=127.0.0.1 port=%6")
            .arg(shellQuote(gstLaunchBinary()))
            .arg(shellQuote(status.testFilePath))
            .arg(status.previewProfile.width)
            .arg(status.previewProfile.height)
            .arg(previewBoundaryForCamera(status.cameraId))
            .arg(previewPortForCamera(status.cameraId));
    }

    return QStringLiteral(
        "%1 -e v4l2src device=%2 ! "
        "video/x-raw,format=%3,width=%4,height=%5,framerate=%6/1 ! "
        "jpegenc ! multipartmux boundary=%7 ! "
        "tcpserversink host=127.0.0.1 port=%8")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.previewProfile.pixelFormat)
        .arg(status.previewProfile.width)
        .arg(status.previewProfile.height)
        .arg(status.previewProfile.fps > 0 ? status.previewProfile.fps : 30)
        .arg(previewBoundaryForCamera(status.cameraId))
        .arg(previewPortForCamera(status.cameraId));
}

QString GstreamerVideoPipelineBackend::buildRecordingCommand(
    const VideoChannelStatus &status, const QString &outputPath) const {
    return QStringLiteral(
        "%1 -e v4l2src device=%2 ! "
        "video/x-raw,format=%3,width=%4,height=%5,framerate=%6/1 ! "
        "tee name=t "
        "t. ! queue ! videoscale ! video/x-raw,width=%7,height=%8 ! "
        "jpegenc ! multipartmux boundary=%9 ! "
        "tcpserversink host=127.0.0.1 port=%10 "
        "t. ! queue ! mpph264enc ! h264parse ! qtmux ! filesink location=%11")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.recordProfile.pixelFormat)
        .arg(status.recordProfile.width)
        .arg(status.recordProfile.height)
        .arg(status.recordProfile.fps > 0 ? status.recordProfile.fps : 30)
        .arg(status.previewProfile.width)
        .arg(status.previewProfile.height)
        .arg(previewBoundaryForCamera(status.cameraId))
        .arg(previewPortForCamera(status.cameraId))
        .arg(shellQuote(outputPath));
}

QString GstreamerVideoPipelineBackend::buildSnapshotCommand(
    const VideoChannelStatus &status, const QString &outputPath) const {
    return QStringLiteral(
        "%1 -e v4l2src device=%2 num-buffers=1 ! "
        "video/x-raw,format=%3,width=%4,height=%5 ! mppjpegenc ! filesink location=%6")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.snapshotProfile.pixelFormat)
        .arg(status.snapshotProfile.width)
        .arg(status.snapshotProfile.height)
        .arg(shellQuote(outputPath));
}

bool GstreamerVideoPipelineBackend::startCommand(const QString &cameraId, const QString &command,
    bool recording, QString *previewUrl, QString *error) {
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

    auto *process = new QProcess();
    process->setProgram(QStringLiteral("/bin/bash"));
    process->setArguments({QStringLiteral("-lc"), QStringLiteral("exec %1").arg(command)});
    process->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(process,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, cameraId, process](int exitCode, QProcess::ExitStatus exitStatus) {
            if (!pipelines_.contains(cameraId) || pipelines_.value(cameraId).process != process) {
                return;
            }

            const ActivePipeline pipeline = pipelines_.take(cameraId);
            const bool testInput = pipeline.testInput;
            delete process;

            if (!observer_) {
                return;
            }
            if (testInput && exitStatus == QProcess::NormalExit && exitCode == 0) {
                observer_->onPipelinePlaybackFinished(cameraId);
                return;
            }
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                observer_->onPipelineRuntimeError(cameraId, QStringLiteral("preview_pipeline_failed"));
            }
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
                ? QStringLiteral("pipeline_exited_during_startup")
                : startupOutput;
        }
        delete process;
        return false;
    }

    ActivePipeline pipeline;
    pipeline.process = process;
    pipeline.recording = recording;
    pipeline.testInput = false;
    pipeline.previewUrl = previewUrlForCamera(cameraId);
    pipelines_.insert(cameraId, pipeline);
    pipelines_[cameraId].testInput = command.contains(QStringLiteral("filesrc location="));

    if (previewUrl) {
        *previewUrl = pipeline.previewUrl;
    }
    return true;
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

    ActivePipeline pipeline = pipelines_.take(cameraId);
    if (!pipeline.process) {
        return true;
    }

    const qint64 processId = pipeline.process->processId();
    if (processId > 0) {
        ::kill(static_cast<pid_t>(processId), SIGINT);
    }
    if (!pipeline.process->waitForFinished(kStopTimeoutMs)) {
        pipeline.process->kill();
        pipeline.process->waitForFinished(kStopTimeoutMs);
    }
    if (pipeline.process->state() != QProcess::NotRunning && error) {
        *error = QStringLiteral("pipeline_stop_failed");
    }
    delete pipeline.process;
    return error ? error->isEmpty() : true;
}

void GstreamerVideoPipelineBackend::stopAllPipelines() {
    const auto cameraIds = pipelines_.keys();
    for (const QString &cameraId : cameraIds) {
        QString error;
        stopActivePipeline(cameraId, &error);
    }
}
