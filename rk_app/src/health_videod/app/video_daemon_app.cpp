#include "app/video_daemon_app.h"

#include "core/video_service.h"
#include "ipc/video_gateway.h"

VideoDaemonApp::VideoDaemonApp(QObject *parent)
    : QObject(parent)
    , service_(new VideoService(nullptr, nullptr, this))
    , gateway_(new VideoGateway(service_, this)) {
}

bool VideoDaemonApp::start() {
    if (!gateway_->start()) {
        return false;
    }
    service_->startPreview(QStringLiteral("front_cam"));
    return true;
}
