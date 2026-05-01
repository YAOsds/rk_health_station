#include "pipeline/gst_process_runner.h"

#include <signal.h>
#include <unistd.h>

namespace {
const int kStartTimeoutMs = 5000;
const int kStopTimeoutMs = 5000;
const int kStartupProbeMs = 750;

QString startupErrorFor(QProcess *process) {
    QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
    if (error.isEmpty()) {
        error = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
    }
    if (error.isEmpty()) {
        error = QString::fromUtf8(process->readAll()).trimmed();
    }
    return error.isEmpty() ? QStringLiteral("pipeline_exited_during_startup") : error;
}
}

GstProcessRunner::GstProcessRunner(QObject *parent)
    : QObject(parent) {
}

GstProcessRunner::~GstProcessRunner() {
    QString error;
    stop(&error);
}

bool GstProcessRunner::start(const QString &command,
    QProcess::ProcessChannelMode mode,
    const Callbacks &callbacks,
    QString *error) {
    if (error) {
        error->clear();
    }
    if (process_) {
        if (error) {
            *error = QStringLiteral("process_already_running");
        }
        return false;
    }

    callbacks_ = callbacks;
    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("/bin/bash"));
    process->setArguments({QStringLiteral("-lc"), QStringLiteral("exec %1").arg(command)});
    process->setProcessChannelMode(mode);

    QObject::connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        if (process_ != process || !callbacks_.onStdoutReady) {
            return;
        }
        callbacks_.onStdoutReady();
    });
    QObject::connect(process,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this,
        [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
            if (process_ == process) {
                process_ = nullptr;
            }
            const auto onFinished = callbacks_.onFinished;
            callbacks_ = {};
            if (onFinished) {
                onFinished(exitCode, exitStatus);
            }
            process->deleteLater();
        });

    process->start();
    if (!process->waitForStarted(kStartTimeoutMs)) {
        if (error) {
            *error = process->errorString();
        }
        process->deleteLater();
        callbacks_ = {};
        return false;
    }
    if (process->waitForFinished(kStartupProbeMs)) {
        if (error) {
            *error = startupErrorFor(process);
        }
        process->deleteLater();
        callbacks_ = {};
        return false;
    }

    process_ = process;
    return true;
}

bool GstProcessRunner::stop(QString *error) {
    if (error) {
        error->clear();
    }
    if (!process_) {
        callbacks_ = {};
        return true;
    }

    QProcess *process = process_;
    process_ = nullptr;
    callbacks_ = {};
    QObject::disconnect(process, nullptr, this, nullptr);

    const qint64 processId = process->processId();
    if (processId > 0) {
        ::kill(static_cast<pid_t>(processId), SIGINT);
    }
    if (!process->waitForFinished(kStopTimeoutMs)) {
        process->kill();
        process->waitForFinished(kStopTimeoutMs);
    }
    if (process->state() != QProcess::NotRunning && error) {
        *error = QStringLiteral("pipeline_stop_failed");
    }
    process->deleteLater();
    return error ? error->isEmpty() : true;
}

QProcess *GstProcessRunner::process() const {
    return process_;
}
