#include "pipeline/external_gstreamer_runner.h"

#include "pipeline/pipeline_session.h"

ExternalGstreamerRunner::ExternalGstreamerRunner(const QString &command,
    QProcess::ProcessChannelMode mode,
    const GstProcessRunner::Callbacks &callbacks,
    QObject *parent)
    : VideoPipelineRunner(parent)
    , command_(command)
    , mode_(mode)
    , callbacks_(callbacks)
    , runner_(this) {
}

bool ExternalGstreamerRunner::startPreview(PipelineSession &session, QString *error) {
    if (!runner_.start(command_, mode_, callbacks_, error)) {
        session.process = nullptr;
        return false;
    }
    session.process = runner_.process();
    return true;
}

bool ExternalGstreamerRunner::stopPreview(PipelineSession &session, QString *error) {
    session.process = nullptr;
    return runner_.stop(error);
}
