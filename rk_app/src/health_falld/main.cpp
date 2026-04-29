#include "app/fall_daemon_app.h"
#include "runtime_config/app_runtime_config_loader.h"

#include <QCoreApplication>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    const LoadedAppRuntimeConfig loaded = loadAppRuntimeConfig(QString());
    if (!loaded.ok) {
        for (const QString &error : loaded.errors) {
            qCritical().noquote() << "runtime_config error:" << error;
        }
        return 1;
    }

    FallDaemonApp daemon(loadFallRuntimeConfig(loaded.config));
    if (!daemon.start()) {
        return 1;
    }
    return app.exec();
}
