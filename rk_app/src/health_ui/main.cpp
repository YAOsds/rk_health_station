#include "app/ui_app.h"

#include <QApplication>
#include <QDebug>
#include <QMetaObject>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    qInfo() << "health-ui lifecycle: process start" << app.arguments();

    UiApp uiApp;
    const bool started = uiApp.start();
    qInfo() << "health-ui lifecycle: ui app start result" << started;

    if (app.arguments().contains(QStringLiteral("--open-video-page"))) {
        QMetaObject::invokeMethod(&uiApp, [ &uiApp ]() {
            uiApp.openVideoPage();
        }, Qt::QueuedConnection);
    }

    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(200, &app, &QCoreApplication::quit);
    }

    qInfo() << "health-ui lifecycle: entering event loop";
    const int exitCode = app.exec();
    qInfo() << "health-ui lifecycle: event loop exited with code" << exitCode;
    return exitCode;
}
