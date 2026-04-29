#include "app/daemon_app.h"
#include "runtime_config/app_runtime_config_loader.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    qInfo() << "healthd lifecycle: process start" << app.arguments();

    const LoadedAppRuntimeConfig loaded = loadAppRuntimeConfig(QString());
    if (!loaded.ok) {
        for (const QString &error : loaded.errors) {
            qCritical().noquote() << "runtime_config error:" << error;
        }
        return 1;
    }

    DaemonApp daemon(loaded.config);
    if (!daemon.start()) {
        qCritical() << "healthd lifecycle: bootstrap failed";
        return 1;
    }

    qInfo() << "healthd lifecycle: entering event loop";
    const int exitCode = app.exec();
    qInfo() << "healthd lifecycle: event loop exited with code" << exitCode;
    return exitCode;
}
