#pragma once

#include "analysis/analysis_frame_converter.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

struct _GstSample;
using GstSample = _GstSample;

class GstAppSinkFrameDispatcher : public QObject {
public:
    using FrameCallback = std::function<void(const QByteArray &)>;
    using DmaFrameCallback = std::function<bool(const AnalysisDmaBuffer &)>;
    using RuntimeErrorCallback = std::function<void(const QString &)>;

    explicit GstAppSinkFrameDispatcher(QObject *parent = nullptr);

    void setFrameCallback(FrameCallback callback);
    void setDmaFrameCallback(DmaFrameCallback callback);
    void setRuntimeErrorCallback(RuntimeErrorCallback callback);
    void configureDmaInput(bool preferDmaInput, int fallbackStrideBytes);
    void dispatchSample(GstSample *sample);

private:
    bool dispatchDmaFrame(GstSample *sample);
    void reportRuntimeError(const QString &error);

    FrameCallback frameCallback_;
    DmaFrameCallback dmaFrameCallback_;
    RuntimeErrorCallback runtimeErrorCallback_;
    bool preferDmaInput_ = false;
    int fallbackStrideBytes_ = 0;
    bool loggedDmaInputAvailable_ = false;
    bool loggedDmaInputUnavailable_ = false;
};
