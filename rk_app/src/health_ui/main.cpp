#include "app/ui_app.h"

#include <QApplication>
#include <QDebug>
#include <QMetaObject>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    UiApp uiApp;
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
