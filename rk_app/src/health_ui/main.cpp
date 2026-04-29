#include "app/ui_app.h"
#include "runtime_config/app_runtime_config_loader.h"

#include <QApplication>
#include <QDebug>
#include <QMetaObject>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    const LoadedAppRuntimeConfig loaded = loadAppRuntimeConfig(QString());
    if (!loaded.ok) {
        for (const QString &error : loaded.errors) {
            qCritical().noquote() << "runtime_config error:" << error;
        }
        return 1;
    }

    UiApp uiApp(loaded.config);
    const bool started = uiApp.start();

    if (app.arguments().contains(QStringLiteral("--open-video-page"))) {
        QMetaObject::invokeMethod(&uiApp, [ &uiApp ]() {
            uiApp.openVideoPage();
        }, Qt::QueuedConnection);
    }

    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(200, &app, &QCoreApplication::quit);
    }

    const int exitCode = app.exec();
    return started ? exitCode : 1;
}
