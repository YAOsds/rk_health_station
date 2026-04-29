#pragma once

#include "models/video_models.h"
#include "analysis/analysis_frame_converter.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

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
    static GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData);

    QString buildLaunchDescription(const Config &config) const;
    void installAllocationProbe();
    void pollBus();
    bool dispatchDmaFrame(GstSample *sample);
    void dispatchFrame(GstSample *sample);
    void reportRuntimeError(const QString &error);

    GstElement *pipeline_ = nullptr;
    GstElement *analysisSink_ = nullptr;
    FrameCallback frameCallback_;
    DmaFrameCallback dmaFrameCallback_;
    bool preferDmaInput_ = false;
    int fallbackStrideBytes_ = 0;
    bool loggedDmaInputAvailable_ = false;
    bool loggedDmaInputUnavailable_ = false;
    RuntimeErrorCallback runtimeErrorCallback_;
};
