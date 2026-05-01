#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

#include <functional>

class GstProcessRunner : public QObject {
public:
    struct Callbacks {
        std::function<void()> onStdoutReady;
        std::function<void(int, QProcess::ExitStatus)> onFinished;
    };

    explicit GstProcessRunner(QObject *parent = nullptr);
    ~GstProcessRunner() override;

    bool start(const QString &command,
        QProcess::ProcessChannelMode mode,
        const Callbacks &callbacks,
        QString *error);
    bool stop(QString *error);
    QProcess *process() const;

private:
    QProcess *process_ = nullptr;
    Callbacks callbacks_;
};
