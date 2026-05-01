#pragma once

#include "pipeline/gst_process_runner.h"
#include "pipeline/video_pipeline_runner.h"

class QString;
struct PipelineSession;

class ExternalGstreamerRunner : public VideoPipelineRunner {
public:
    ExternalGstreamerRunner(const QString &command,
        QProcess::ProcessChannelMode mode,
        const GstProcessRunner::Callbacks &callbacks,
        QObject *parent = nullptr);

    bool startPreview(PipelineSession &session, QString *error) override;
    bool stopPreview(PipelineSession &session, QString *error) override;

private:
    QString command_;
    QProcess::ProcessChannelMode mode_ = QProcess::SeparateChannels;
    GstProcessRunner::Callbacks callbacks_;
    GstProcessRunner runner_;
};
