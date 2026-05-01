#include "pipeline/inprocess_launch_description_builder.h"

#include "pipeline/inprocess_gstreamer_pipeline.h"

namespace {
QString gstQuote(const QString &value) {
    QString escaped = value;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}
}

QString InprocessLaunchDescriptionBuilder::build(
    const InprocessGstreamerPipeline::Config &config) const {
    const VideoProfile &profile = config.status.previewProfile;
    const int fps = profile.fps > 0 ? profile.fps : 30;
    const QString sourceFormat = config.preferDmaInput && config.rgaAnalysis
        ? config.analysisInputPixelFormat
        : profile.pixelFormat;

    const bool forceDmaIo = config.preferDmaInput && config.rgaAnalysis && config.forceDmaIo;
    const QString sourceElement = forceDmaIo
        ? QStringLiteral("v4l2src device=%1 io-mode=dmabuf").arg(gstQuote(config.status.devicePath))
        : QStringLiteral("v4l2src device=%1").arg(gstQuote(config.status.devicePath));

    QString description = QStringLiteral(
        "%1 ! "
        "video/x-raw,format=%2,width=%3,height=%4,framerate=%5/1 ! "
        "tee name=t "
        "t. ! queue ! mppjpegenc rc-mode=fixqp q-factor=%6 ! multipartmux boundary=%7 ! "
        "tcpserversink host=127.0.0.1 port=%8")
        .arg(sourceElement)
        .arg(sourceFormat)
        .arg(profile.width)
        .arg(profile.height)
        .arg(fps)
        .arg(config.jpegQuality)
        .arg(config.previewBoundary)
        .arg(config.previewPort);

    if (!config.analysisEnabled) {
        return description;
    }

    const int analysisFps = config.analysisFps > 0 ? config.analysisFps : 15;
    if (config.rgaAnalysis) {
        description += QStringLiteral(
            " t. ! queue leaky=downstream max-size-buffers=1 ! "
            "videorate drop-only=true ! "
            "video/x-raw,format=%1,width=%2,height=%3,framerate=%4/1 ! "
            "appsink name=analysis_sink emit-signals=false sync=false max-buffers=1 drop=true")
            .arg(sourceFormat)
            .arg(profile.width)
            .arg(profile.height)
            .arg(analysisFps);
    } else {
        description += QStringLiteral(
            " t. ! queue leaky=downstream max-size-buffers=1 ! "
            "videorate drop-only=true ! videoconvert ! videoscale ! "
            "video/x-raw,format=RGB,width=%1,height=%2,framerate=%3/1 ! "
            "appsink name=analysis_sink emit-signals=false sync=false max-buffers=1 drop=true")
            .arg(config.analysisOutputWidth)
            .arg(config.analysisOutputHeight)
            .arg(analysisFps);
    }
    return description;
}
