#pragma once

#include <QString>

#include <functional>

struct _GstElement;
using GstElement = _GstElement;

class GstBusMonitor {
public:
    void poll(GstElement *pipeline, const std::function<void(const QString &)> &reportError) const;
};
