#include "pipeline/gst_bus_monitor.h"

#include <gst/gst.h>

namespace {
QString gstErrorMessage(GError *error, const QString &fallback) {
    if (!error) {
        return fallback;
    }
    const QString message = QString::fromUtf8(error->message);
    g_error_free(error);
    return message.isEmpty() ? fallback : message;
}
}

void GstBusMonitor::poll(
    GstElement *pipeline, const std::function<void(const QString &)> &reportError) const {
    if (!pipeline || !reportError) {
        return;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
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
            const QString errorText =
                gstErrorMessage(gstError, QStringLiteral("inprocess_gstreamer_runtime_error"));
            if (debugInfo) {
                g_free(debugInfo);
            }
            reportError(errorText);
            break;
        }
        case GST_MESSAGE_EOS:
            reportError(QStringLiteral("inprocess_gstreamer_eos"));
            break;
        default:
            break;
        }
        gst_message_unref(message);
    }

    gst_object_unref(bus);
}
