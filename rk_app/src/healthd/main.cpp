#include "app/daemon_app.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    qInfo() << "healthd lifecycle: process start" << app.arguments();

    DaemonApp daemon;
    if (!daemon.start()) {
        qCritical() << "healthd lifecycle: bootstrap failed";
        return 1;
    }

    qInfo() << "healthd lifecycle: entering event loop";
    const int exitCode = app.exec();
    qInfo() << "healthd lifecycle: event loop exited with code" << exitCode;
    return exitCode;
}
