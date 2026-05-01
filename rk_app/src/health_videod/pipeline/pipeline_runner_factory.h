#pragma once

#include "pipeline/gst_process_runner.h"

#include <QProcess>

class AppRuntimeConfig;
class QString;
class VideoPipelineRunner;

class PipelineRunnerFactory {
public:
    explicit PipelineRunnerFactory(const AppRuntimeConfig &runtimeConfig);

    VideoPipelineRunner *createPreviewRunner(const QString &command,
        QProcess::ProcessChannelMode mode,
        const GstProcessRunner::Callbacks &callbacks) const;

private:
    const AppRuntimeConfig &runtimeConfig_;
};
