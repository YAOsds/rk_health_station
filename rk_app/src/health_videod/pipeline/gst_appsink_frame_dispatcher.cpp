#include "pipeline/gst_appsink_frame_dispatcher.h"

#include <QDebug>
#include <QMetaObject>

#include <gst/gst.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include <fcntl.h>
#include <unistd.h>

namespace {
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
}

GstAppSinkFrameDispatcher::GstAppSinkFrameDispatcher(QObject *parent)
    : QObject(parent) {
}

void GstAppSinkFrameDispatcher::setFrameCallback(FrameCallback callback) {
    frameCallback_ = std::move(callback);
}

void GstAppSinkFrameDispatcher::setDmaFrameCallback(DmaFrameCallback callback) {
    dmaFrameCallback_ = std::move(callback);
}

void GstAppSinkFrameDispatcher::setRuntimeErrorCallback(RuntimeErrorCallback callback) {
    runtimeErrorCallback_ = std::move(callback);
}

void GstAppSinkFrameDispatcher::configureDmaInput(bool preferDmaInput, int fallbackStrideBytes) {
    preferDmaInput_ = preferDmaInput;
    fallbackStrideBytes_ = fallbackStrideBytes;
    loggedDmaInputAvailable_ = false;
    loggedDmaInputUnavailable_ = false;
}

void GstAppSinkFrameDispatcher::dispatchSample(GstSample *sample) {
    if (!frameCallback_ || !sample) {
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

bool GstAppSinkFrameDispatcher::dispatchDmaFrame(GstSample *sample) {
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

void GstAppSinkFrameDispatcher::reportRuntimeError(const QString &error) {
    if (!runtimeErrorCallback_) {
        return;
    }

    QMetaObject::invokeMethod(this, [this, error]() {
        if (runtimeErrorCallback_) {
            runtimeErrorCallback_(error);
        }
    }, Qt::QueuedConnection);
}
