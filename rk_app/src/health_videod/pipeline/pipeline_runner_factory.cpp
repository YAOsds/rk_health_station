#include "pipeline/pipeline_runner_factory.h"

#include "pipeline/external_gstreamer_runner.h"
#include "runtime_config/app_runtime_config.h"

PipelineRunnerFactory::PipelineRunnerFactory(const AppRuntimeConfig &runtimeConfig)
    : runtimeConfig_(runtimeConfig) {
}

VideoPipelineRunner *PipelineRunnerFactory::createPreviewRunner(const QString &command,
    QProcess::ProcessChannelMode mode,
    const GstProcessRunner::Callbacks &callbacks) const {
    Q_UNUSED(runtimeConfig_);
    return new ExternalGstreamerRunner(command, mode, callbacks);
}
