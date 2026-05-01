#include "pipeline/inprocess_gstreamer_pipeline.h"

#include "pipeline/gst_appsink_frame_dispatcher.h"
#include "pipeline/gst_bus_monitor.h"
#include "pipeline/inprocess_launch_description_builder.h"

#include <QMetaObject>
#include <QTimer>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <mutex>

namespace {
const int kStartTimeoutMs = 5000;
const int kBusPollIntervalMs = 200;

void ensureGstreamerInitialized() {
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        gst_init(nullptr, nullptr);
    });
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

QString gstErrorMessage(GError *error, const QString &fallback) {
    if (!error) {
        return fallback;
    }
    const QString message = QString::fromUtf8(error->message);
    g_error_free(error);
    return message.isEmpty() ? fallback : message;
}

GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData) {
    auto *dispatcher = static_cast<GstAppSinkFrameDispatcher *>(userData);
    if (!dispatcher) {
        return GST_FLOW_ERROR;
    }

    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_ERROR;
    }
    dispatcher->dispatchSample(sample);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
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
    if (frameDispatcher_) {
        frameDispatcher_->setFrameCallback(frameCallback_);
    }
}

void InprocessGstreamerPipeline::setDmaFrameCallback(DmaFrameCallback callback) {
    dmaFrameCallback_ = std::move(callback);
    if (frameDispatcher_) {
        frameDispatcher_->setDmaFrameCallback(dmaFrameCallback_);
    }
}

void InprocessGstreamerPipeline::setRuntimeErrorCallback(RuntimeErrorCallback callback) {
    runtimeErrorCallback_ = std::move(callback);
    if (frameDispatcher_) {
        frameDispatcher_->setRuntimeErrorCallback(
            [this](const QString &runtimeError) { reportRuntimeError(runtimeError); });
    }
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

    GError *parseError = nullptr;
    const QString launchDescription = InprocessLaunchDescriptionBuilder().build(config);
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

        frameDispatcher_ = new GstAppSinkFrameDispatcher(this);
        frameDispatcher_->setFrameCallback(frameCallback_);
        frameDispatcher_->setDmaFrameCallback(dmaFrameCallback_);
        frameDispatcher_->setRuntimeErrorCallback(
            [this](const QString &runtimeError) { reportRuntimeError(runtimeError); });
        frameDispatcher_->configureDmaInput(preferDmaInput_, fallbackStrideBytes_);

        GstAppSinkCallbacks callbacks{};
        callbacks.new_sample = &::onNewSample;
        gst_app_sink_set_callbacks(
            GST_APP_SINK(analysisSink_), &callbacks, frameDispatcher_, nullptr);
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

    busMonitor_ = new GstBusMonitor();
    auto *timer = new QTimer(this);
    timer->setInterval(kBusPollIntervalMs);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (busMonitor_) {
            busMonitor_->poll(pipeline_, [this](const QString &runtimeError) {
                reportRuntimeError(runtimeError);
            });
        }
    });
    timer->start();
    return true;
}

void InprocessGstreamerPipeline::stop() {
    delete busMonitor_;
    busMonitor_ = nullptr;

    const auto childrenBeforeStop = children();
    for (QObject *child : childrenBeforeStop) {
        delete child;
    }
    frameDispatcher_ = nullptr;

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
