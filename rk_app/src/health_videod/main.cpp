#include "app/video_daemon_app.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSocketNotifier>

#include <csignal>

#include <sys/socket.h>
#include <unistd.h>

namespace {

int gSignalPipe[2] = {-1, -1};

void handleTerminationSignal(int signalNumber) {
    const char signalByte = static_cast<char>(signalNumber);
    if (gSignalPipe[1] >= 0) {
        const ssize_t ignored = ::write(gSignalPipe[1], &signalByte, sizeof(signalByte));
        (void)ignored;
    }
}

bool installSignalHandlers() {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, gSignalPipe) != 0) {
        return false;
    }

    struct sigaction action;
    action.sa_handler = handleTerminationSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    return ::sigaction(SIGINT, &action, nullptr) == 0
        && ::sigaction(SIGTERM, &action, nullptr) == 0;
}

void closeSignalPipe() {
    for (int &fd : gSignalPipe) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    qInfo() << "health-videod lifecycle: process start" << app.arguments();

    if (!installSignalHandlers()) {
        qCritical() << "health-videod lifecycle: failed to install signal handlers";
        return 1;
    }

    QSocketNotifier signalNotifier(gSignalPipe[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, [&app, &signalNotifier]() {
        signalNotifier.setEnabled(false);
        char signalByte = 0;
        ::read(gSignalPipe[0], &signalByte, sizeof(signalByte));
        qInfo() << "health-videod lifecycle: graceful shutdown requested" << int(signalByte);
        app.quit();
        signalNotifier.setEnabled(true);
    });

    VideoDaemonApp daemon;
    const bool started = daemon.start();
    qInfo() << "health-videod lifecycle: daemon start result" << started;
    if (!started) {
        closeSignalPipe();
        return 1;
    }

    const int exitCode = app.exec();
    closeSignalPipe();
    return exitCode;
}
