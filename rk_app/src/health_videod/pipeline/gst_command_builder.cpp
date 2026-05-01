#include "pipeline/gst_command_builder.h"

#include "pipeline/preview_stream_reader.h"

namespace {
const int kStableAnalysisTapFps = 15;
const int kAnalysisOutputWidth = 640;
const int kAnalysisOutputHeight = 640;
const int kPreviewJpegQuality = 95;
const char kDefaultGstLaunchBinary[] = "gst-launch-1.0";
}

GstCommandBuilder::GstCommandBuilder(const AppRuntimeConfig &runtimeConfig)
    : runtimeConfig_(runtimeConfig) {
}

QString GstCommandBuilder::gstLaunchBinary() const {
    const QString configuredBinary = runtimeConfig_.video.gstLaunchBin.trimmed();
    return configuredBinary.isEmpty() ? QString::fromUtf8(kDefaultGstLaunchBinary) : configuredBinary;
}

QString GstCommandBuilder::shellQuote(const QString &value) const {
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString GstCommandBuilder::previewUrlForCamera(const QString &cameraId) const {
    return QStringLiteral("tcp://127.0.0.1:%1?transport=tcp_mjpeg&boundary=%2")
        .arg(previewPortForCamera(cameraId))
        .arg(previewBoundaryForCamera(cameraId));
}

QString GstCommandBuilder::previewBoundaryForCamera(const QString &cameraId) const {
    Q_UNUSED(cameraId);
    return QStringLiteral("rkpreview");
}

quint16 GstCommandBuilder::previewPortForCamera(const QString &cameraId) const {
    if (cameraId == QStringLiteral("front_cam")) {
        return 5602;
    }
    return 5699;
}

QString GstCommandBuilder::buildAnalysisTapCommandFragment(
    const VideoChannelStatus &status, const VideoProfile &sourceProfile, bool enabled) const {
    if (!enabled) {
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

GstCommandBuilder::AnalysisConvertBackend
GstCommandBuilder::analysisConvertBackendForProfile(const VideoProfile &sourceProfile) const {
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

QString GstCommandBuilder::buildPreviewCommand(
    const VideoChannelStatus &status, bool analysisTapEnabled) const {
    const QString analysisTap = buildAnalysisTapCommandFragment(
        status, status.previewProfile, analysisTapEnabled);
    if (status.inputMode == QStringLiteral("test_file")) {
        if (!analysisTap.isEmpty()) {
            return QStringLiteral(
                "%1 -q -e filesrc location=%2 ! decodebin name=dec "
                "dec. ! queue ! videoconvert ! videoscale ! "
                "video/x-raw,format=NV12,width=%3,height=%4 ! tee name=t "
                "t. ! queue ! mppjpegenc rc-mode=fixqp q-factor=%5 ! multipartmux boundary=%6 ! "
                "tcpserversink host=127.0.0.1 port=%7%8 "
                "dec. ! queue ! audioconvert ! audioresample ! fakesink sync=false")
                .arg(shellQuote(gstLaunchBinary()))
                .arg(shellQuote(status.testFilePath))
                .arg(status.previewProfile.width)
                .arg(status.previewProfile.height)
                .arg(kPreviewJpegQuality)
                .arg(previewBoundaryForCamera(status.cameraId))
                .arg(previewPortForCamera(status.cameraId))
                .arg(analysisTap);
        }

        return QStringLiteral(
            "%1 -q -e filesrc location=%2 ! decodebin name=dec "
            "dec. ! queue ! videoconvert ! videoscale ! "
            "video/x-raw,format=NV12,width=%3,height=%4 ! mppjpegenc rc-mode=fixqp q-factor=%5 ! multipartmux boundary=%6 ! "
            "tcpserversink host=127.0.0.1 port=%7 "
            "dec. ! queue ! audioconvert ! audioresample ! fakesink sync=false")
            .arg(shellQuote(gstLaunchBinary()))
            .arg(shellQuote(status.testFilePath))
            .arg(status.previewProfile.width)
            .arg(status.previewProfile.height)
            .arg(kPreviewJpegQuality)
            .arg(previewBoundaryForCamera(status.cameraId))
            .arg(previewPortForCamera(status.cameraId));
    }

    if (!analysisTap.isEmpty()) {
        return QStringLiteral(
            "%1 -q -e v4l2src device=%2 ! "
            "video/x-raw,format=%3,width=%4,height=%5,framerate=%6/1 ! "
            "tee name=t "
            "t. ! queue ! mppjpegenc rc-mode=fixqp q-factor=%7 ! multipartmux boundary=%8 ! "
            "tcpserversink host=127.0.0.1 port=%9%10")
            .arg(shellQuote(gstLaunchBinary()))
            .arg(shellQuote(status.devicePath))
            .arg(status.previewProfile.pixelFormat)
            .arg(status.previewProfile.width)
            .arg(status.previewProfile.height)
            .arg(status.previewProfile.fps > 0 ? status.previewProfile.fps : 30)
            .arg(kPreviewJpegQuality)
            .arg(previewBoundaryForCamera(status.cameraId))
            .arg(previewPortForCamera(status.cameraId))
            .arg(analysisTap);
    }

    return QStringLiteral(
        "%1 -q -e v4l2src device=%2 ! "
        "video/x-raw,format=%3,width=%4,height=%5,framerate=%6/1 ! "
        "mppjpegenc rc-mode=fixqp q-factor=%7 ! multipartmux boundary=%8 ! "
        "tcpserversink host=127.0.0.1 port=%9")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.previewProfile.pixelFormat)
        .arg(status.previewProfile.width)
        .arg(status.previewProfile.height)
        .arg(status.previewProfile.fps > 0 ? status.previewProfile.fps : 30)
        .arg(kPreviewJpegQuality)
        .arg(previewBoundaryForCamera(status.cameraId))
        .arg(previewPortForCamera(status.cameraId));
}

QString GstCommandBuilder::buildRecordingCommand(
    const VideoChannelStatus &status, const QString &outputPath, bool analysisTapEnabled) const {
    const QString analysisTap = buildAnalysisTapCommandFragment(
        status, status.recordProfile, analysisTapEnabled);
    return QStringLiteral(
        "%1 -q -e v4l2src device=%2 ! "
        "video/x-raw,format=%3,width=%4,height=%5,framerate=%6/1 ! "
        "tee name=t "
        "t. ! queue ! videoscale ! video/x-raw,format=NV12,width=%7,height=%8 ! "
        "mppjpegenc rc-mode=fixqp q-factor=%9 ! multipartmux boundary=%10 ! "
        "tcpserversink host=127.0.0.1 port=%11%12 "
        "t. ! queue ! mpph264enc ! h264parse ! qtmux ! filesink location=%13")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.recordProfile.pixelFormat)
        .arg(status.recordProfile.width)
        .arg(status.recordProfile.height)
        .arg(status.recordProfile.fps > 0 ? status.recordProfile.fps : 30)
        .arg(status.previewProfile.width)
        .arg(status.previewProfile.height)
        .arg(kPreviewJpegQuality)
        .arg(previewBoundaryForCamera(status.cameraId))
        .arg(previewPortForCamera(status.cameraId))
        .arg(analysisTap)
        .arg(shellQuote(outputPath));
}

QString GstCommandBuilder::buildSnapshotCommand(
    const VideoChannelStatus &status, const QString &outputPath) const {
    return QStringLiteral(
        "%1 -q -e v4l2src device=%2 num-buffers=1 ! "
        "video/x-raw,format=%3,width=%4,height=%5 ! mppjpegenc ! filesink location=%6")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(status.devicePath))
        .arg(status.snapshotProfile.pixelFormat)
        .arg(status.snapshotProfile.width)
        .arg(status.snapshotProfile.height)
        .arg(shellQuote(outputPath));
}

QString GstCommandBuilder::buildPreviewStreamRecordingCommand(
    const QString &previewUrl, const QString &outputPath, QString *error) const {
    PreviewStreamReader reader;
    PreviewStreamReader::PreviewStreamConfig config;
    if (!reader.parsePreviewUrl(previewUrl, &config, error)) {
        return QString();
    }

    return QStringLiteral(
        "%1 -q -e tcpclientsrc host=%2 port=%3 ! "
        "\"multipart/x-mixed-replace,boundary=%4\" ! multipartdemux single-stream=true ! "
        "jpegparse ! jpegdec ! videoconvert ! "
        "mpph264enc ! h264parse ! qtmux ! filesink location=%5")
        .arg(shellQuote(gstLaunchBinary()))
        .arg(shellQuote(config.host))
        .arg(config.port)
        .arg(config.boundary)
        .arg(shellQuote(outputPath));
}
