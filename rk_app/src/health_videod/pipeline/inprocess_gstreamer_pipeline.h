#pragma once

#include "models/video_models.h"
#include "analysis/analysis_frame_converter.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

struct _GstElement;
using GstElement = _GstElement;

class GstBusMonitor;
class GstAppSinkFrameDispatcher;

class InprocessGstreamerPipeline : public QObject {
    Q_OBJECT

public:
    struct Config {
        VideoChannelStatus status;
        QString previewBoundary;
        quint16 previewPort = 0;
        bool analysisEnabled = false;
        bool rgaAnalysis = false;
        int analysisOutputWidth = 0;
        int analysisOutputHeight = 0;
        QString analysisInputPixelFormat = QStringLiteral("NV12");
        int analysisInputStrideBytes = 0;
        int analysisFps = 0;
        int jpegQuality = 95;
        bool preferDmaInput = false;
        bool forceDmaIo = false;
    };

    using FrameCallback = std::function<void(const QByteArray &)>;
    using DmaFrameCallback = std::function<bool(const AnalysisDmaBuffer &)>;
    using RuntimeErrorCallback = std::function<void(const QString &)>;

    explicit InprocessGstreamerPipeline(QObject *parent = nullptr);
    ~InprocessGstreamerPipeline() override;

    void setFrameCallback(FrameCallback callback);
    void setDmaFrameCallback(DmaFrameCallback callback);
    void setRuntimeErrorCallback(RuntimeErrorCallback callback);
    bool start(const Config &config, QString *error);
    void stop();

private:
    void installAllocationProbe();
    void reportRuntimeError(const QString &error);

    GstElement *pipeline_ = nullptr;
    GstElement *analysisSink_ = nullptr;
    FrameCallback frameCallback_;
    DmaFrameCallback dmaFrameCallback_;
    bool preferDmaInput_ = false;
    int fallbackStrideBytes_ = 0;
    RuntimeErrorCallback runtimeErrorCallback_;
    GstBusMonitor *busMonitor_ = nullptr;
    GstAppSinkFrameDispatcher *frameDispatcher_ = nullptr;
};
