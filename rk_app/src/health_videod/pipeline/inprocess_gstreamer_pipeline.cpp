#include "pipeline/inprocess_gstreamer_pipeline.h"

#include <QMetaObject>
#include <QDebug>
#include <QTimer>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include <mutex>

#include <fcntl.h>
#include <unistd.h>

namespace {
const int kStartTimeoutMs = 5000;
const int kBusPollIntervalMs = 200;

void ensureGstreamerInitialized() {
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        gst_init(nullptr, nullptr);
    });
}

QString gstQuote(const QString &value) {
    QString escaped = value;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}


GstPadProbeReturn answerAppsinkAllocationQuery(GstPad *, GstPadProbeInfo *info, gpointer) {
    if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM)) {
        return GST_PAD_PROBE_OK;
    }
    GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);
    if (!query || GST_QUERY_TYPE(query) != GST_QUERY_ALLOCATION) {
        return GST_PAD_PROBE_OK;
    }

    GstCaps *caps = nullptr;
    gboolean needPool = FALSE;
    gst_query_parse_allocation(query, &caps, &needPool);
    Q_UNUSED(needPool);
    if (!caps) {
        return GST_PAD_PROBE_OK;
    }

    GstVideoInfo videoInfo;
    if (!gst_video_info_from_caps(&videoInfo, caps)) {
        return GST_PAD_PROBE_OK;
    }

    GstBufferPool *pool = gst_buffer_pool_new();
    GstStructure *config = gst_buffer_pool_get_config(pool);
    const guint size = static_cast<guint>(GST_VIDEO_INFO_SIZE(&videoInfo));
    gst_buffer_pool_config_set_params(config, caps, size, 2, 8);
    gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
    if (!gst_buffer_pool_set_config(pool, config)) {
        gst_object_unref(pool);
        return GST_PAD_PROBE_OK;
    }

    gst_query_add_allocation_pool(query, pool, size, 2, 8);
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, nullptr);
    gst_object_unref(pool);
    return GST_PAD_PROBE_HANDLED;
}

bool mapSamplePayload(GstSample *sample, QByteArray *payload) {
    if (!sample || !payload) {
        return false;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        return false;
    }
    GstMapInfo mapInfo{};
    if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ)) {
        return false;
    }
    *payload = QByteArray(reinterpret_cast<const char *>(mapInfo.data), static_cast<int>(mapInfo.size));
    gst_buffer_unmap(buffer, &mapInfo);
    return true;
}

QString gstErrorMessage(GError *error, const QString &fallback) {
    if (!error) {
        return fallback;
    }
    const QString message = QString::fromUtf8(error->message);
    g_error_free(error);
    return message.isEmpty() ? fallback : message;
}
}

InprocessGstreamerPipeline::InprocessGstreamerPipeline(QObject *parent)
    : QObject(parent) {
}

InprocessGstreamerPipeline::~InprocessGstreamerPipeline() {
    stop();
}

void InprocessGstreamerPipeline::setFrameCallback(FrameCallback callback) {
    frameCallback_ = std::move(callback);
}

void InprocessGstreamerPipeline::setDmaFrameCallback(DmaFrameCallback callback) {
    dmaFrameCallback_ = std::move(callback);
}

void InprocessGstreamerPipeline::setRuntimeErrorCallback(RuntimeErrorCallback callback) {
    runtimeErrorCallback_ = std::move(callback);
}

bool InprocessGstreamerPipeline::start(const Config &config, QString *error) {
    if (error) {
        error->clear();
    }
    stop();

    if (config.status.inputMode == QStringLiteral("test_file")) {
        if (error) {
            *error = QStringLiteral("inprocess_gstreamer_test_file_unsupported");
        }
        return false;
    }

    ensureGstreamerInitialized();

    preferDmaInput_ = config.preferDmaInput && config.rgaAnalysis;
    fallbackStrideBytes_ = config.analysisInputStrideBytes > 0
        ? config.analysisInputStrideBytes
        : config.status.previewProfile.width;
    loggedDmaInputAvailable_ = false;
    loggedDmaInputUnavailable_ = false;

    GError *parseError = nullptr;
    const QString launchDescription = buildLaunchDescription(config);
    pipeline_ = gst_parse_launch(launchDescription.toUtf8().constData(), &parseError);
    if (!pipeline_) {
        if (error) {
            *error = gstErrorMessage(parseError, QStringLiteral("inprocess_gstreamer_parse_failed"));
        } else if (parseError) {
            g_error_free(parseError);
        }
        return false;
    }
    if (parseError) {
        g_error_free(parseError);
    }

    if (config.analysisEnabled) {
        analysisSink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "analysis_sink");
        if (!analysisSink_) {
            if (error) {
                *error = QStringLiteral("inprocess_gstreamer_appsink_missing");
            }
            stop();
            return false;
        }

        GstAppSinkCallbacks callbacks{};
        callbacks.new_sample = &InprocessGstreamerPipeline::onNewSample;
        gst_app_sink_set_callbacks(GST_APP_SINK(analysisSink_), &callbacks, this, nullptr);
        if (preferDmaInput_) {
            installAllocationProbe();
        }
    }

    const GstStateChangeReturn stateResult = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (stateResult == GST_STATE_CHANGE_FAILURE) {
        if (error) {
            *error = QStringLiteral("inprocess_gstreamer_play_failed");
        }
        stop();
        return false;
    }

    const GstStateChangeReturn waitResult = gst_element_get_state(
        pipeline_, nullptr, nullptr, static_cast<GstClockTime>(kStartTimeoutMs) * GST_MSECOND);
    if (waitResult == GST_STATE_CHANGE_FAILURE) {
        if (error) {
            *error = QStringLiteral("inprocess_gstreamer_startup_failed");
        }
        stop();
        return false;
    }

    auto *timer = new QTimer(this);
    timer->setInterval(kBusPollIntervalMs);
    connect(timer, &QTimer::timeout, this, &InprocessGstreamerPipeline::pollBus);
    timer->start();
    return true;
}

void InprocessGstreamerPipeline::stop() {
    const auto childrenBeforeStop = children();
    for (QObject *child : childrenBeforeStop) {
        delete child;
    }

    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }
    if (analysisSink_) {
        gst_object_unref(analysisSink_);
        analysisSink_ = nullptr;
    }
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
}

GstFlowReturn InprocessGstreamerPipeline::onNewSample(GstAppSink *sink, gpointer userData) {
    auto *self = static_cast<InprocessGstreamerPipeline *>(userData);
    if (!self) {
        return GST_FLOW_ERROR;
    }

    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_ERROR;
    }
    self->dispatchFrame(sample);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

QString InprocessGstreamerPipeline::buildLaunchDescription(const Config &config) const {
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


void InprocessGstreamerPipeline::installAllocationProbe() {
    if (!analysisSink_) {
        return;
    }
    GstPad *sinkPad = gst_element_get_static_pad(analysisSink_, "sink");
    if (!sinkPad) {
        return;
    }
    gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
        &answerAppsinkAllocationQuery, nullptr, nullptr);
    gst_object_unref(sinkPad);
}

void InprocessGstreamerPipeline::pollBus() {
    if (!pipeline_) {
        return;
    }

    GstBus *bus = gst_element_get_bus(pipeline_);
    if (!bus) {
        return;
    }

    GstMessage *message = nullptr;
    while ((message = gst_bus_pop(bus)) != nullptr) {
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *gstError = nullptr;
            gchar *debugInfo = nullptr;
            gst_message_parse_error(message, &gstError, &debugInfo);
            const QString errorText = gstErrorMessage(gstError, QStringLiteral("inprocess_gstreamer_runtime_error"));
            if (debugInfo) {
                g_free(debugInfo);
            }
            reportRuntimeError(errorText);
            break;
        }
        case GST_MESSAGE_EOS:
            reportRuntimeError(QStringLiteral("inprocess_gstreamer_eos"));
            break;
        default:
            break;
        }
        gst_message_unref(message);
    }

    gst_object_unref(bus);
}

bool InprocessGstreamerPipeline::dispatchDmaFrame(GstSample *sample) {
    if (!preferDmaInput_ || !dmaFrameCallback_ || !sample) {
        return false;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        return false;
    }
    if (gst_buffer_n_memory(buffer) != 1) {
        if (!loggedDmaInputUnavailable_) {
            qInfo().noquote() << QStringLiteral("video_runtime event=gst_dmabuf_input unavailable reason=memory_count_%1")
                                     .arg(gst_buffer_n_memory(buffer));
            loggedDmaInputUnavailable_ = true;
        }
        return false;
    }

    GstMemory *memory = gst_buffer_peek_memory(buffer, 0);
    if (!memory || !gst_is_dmabuf_memory(memory)) {
        if (!loggedDmaInputUnavailable_) {
            qInfo().noquote() << QStringLiteral("video_runtime event=gst_dmabuf_input unavailable reason=not_dmabuf");
            loggedDmaInputUnavailable_ = true;
        }
        return false;
    }

    gsize offset = 0;
    gsize maxSize = 0;
    const gsize memorySize = gst_memory_get_sizes(memory, &offset, &maxSize);
    if (offset != 0) {
        if (!loggedDmaInputUnavailable_) {
            qInfo().noquote() << QStringLiteral("video_runtime event=gst_dmabuf_input unavailable reason=offset_%1")
                                     .arg(static_cast<qulonglong>(offset));
            loggedDmaInputUnavailable_ = true;
        }
        return false;
    }

    const int sourceFd = gst_dmabuf_memory_get_fd(memory);
    const int ownedFd = sourceFd >= 0 ? ::fcntl(sourceFd, F_DUPFD_CLOEXEC, 3) : -1;
    if (ownedFd < 0) {
        if (!loggedDmaInputUnavailable_) {
            qInfo().noquote() << QStringLiteral("video_runtime event=gst_dmabuf_input unavailable reason=fd_dup_failed");
            loggedDmaInputUnavailable_ = true;
        }
        return false;
    }

    quint32 strideBytes = static_cast<quint32>(fallbackStrideBytes_ > 0 ? fallbackStrideBytes_ : 0);
    if (GstVideoMeta *videoMeta = gst_buffer_get_video_meta(buffer)) {
        if (videoMeta->n_planes > 0 && videoMeta->stride[0] > 0) {
            strideBytes = static_cast<quint32>(videoMeta->stride[0]);
        }
    }

    AnalysisFrameInputFormat inputFormat = AnalysisFrameInputFormat::Nv12;
    if (GstCaps *caps = gst_sample_get_caps(sample)) {
        if (GstStructure *structure = gst_caps_get_structure(caps, 0)) {
            const gchar *format = gst_structure_get_string(structure, "format");
            if (format && QString::fromLatin1(format) == QStringLiteral("UYVY")) {
                inputFormat = AnalysisFrameInputFormat::Uyvy;
            }
        }
    }

    AnalysisDmaBuffer dmaBuffer;
    dmaBuffer.fd = ownedFd;
    dmaBuffer.inputFormat = inputFormat;
    dmaBuffer.offset = 0;
    dmaBuffer.payloadBytes = static_cast<quint32>(memorySize > 0 ? memorySize : gst_buffer_get_size(buffer));
    dmaBuffer.strideBytes = strideBytes;

    if (!loggedDmaInputAvailable_) {
        qInfo().noquote() << QStringLiteral("video_runtime event=gst_dmabuf_input available payload_bytes=%1 stride=%2 format=%3")
                                 .arg(dmaBuffer.payloadBytes)
                                 .arg(dmaBuffer.strideBytes)
                                 .arg(inputFormat == AnalysisFrameInputFormat::Uyvy ? QStringLiteral("UYVY") : QStringLiteral("NV12"));
        loggedDmaInputAvailable_ = true;
    }

    gst_sample_ref(sample);
    if (!QMetaObject::invokeMethod(this, [this, dmaBuffer, sample]() {
            bool handled = false;
            if (dmaFrameCallback_) {
                handled = dmaFrameCallback_(dmaBuffer);
            }
            if (dmaBuffer.fd >= 0) {
                ::close(dmaBuffer.fd);
            }
            if (!handled && frameCallback_) {
                QByteArray payload;
                if (mapSamplePayload(sample, &payload)) {
                    frameCallback_(payload);
                } else {
                    reportRuntimeError(QStringLiteral("inprocess_gstreamer_buffer_map_failed"));
                }
            }
            gst_sample_unref(sample);
        }, Qt::QueuedConnection)) {
        gst_sample_unref(sample);
        if (dmaBuffer.fd >= 0) {
            ::close(dmaBuffer.fd);
        }
        return false;
    }
    return true;
}

void InprocessGstreamerPipeline::dispatchFrame(GstSample *sample) {
    if (!frameCallback_) {
        return;
    }

    if (dispatchDmaFrame(sample)) {
        return;
    }

    QByteArray payload;
    if (!mapSamplePayload(sample, &payload)) {
        reportRuntimeError(QStringLiteral("inprocess_gstreamer_buffer_map_failed"));
        return;
    }

    QMetaObject::invokeMethod(this, [this, payload]() {
        if (frameCallback_) {
            frameCallback_(payload);
        }
    }, Qt::QueuedConnection);
}

void InprocessGstreamerPipeline::reportRuntimeError(const QString &error) {
    if (!runtimeErrorCallback_) {
        return;
    }

    QMetaObject::invokeMethod(this, [this, error]() {
        if (runtimeErrorCallback_) {
            runtimeErrorCallback_(error);
        }
    }, Qt::QueuedConnection);
}
