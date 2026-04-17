#include "app/fall_daemon_app.h"

#include <QCoreApplication>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    FallDaemonApp daemon;
    if (!daemon.start()) {
        return 1;
    }
    return app.exec();
}
