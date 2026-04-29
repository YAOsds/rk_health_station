#include "app/video_daemon_app.h"

#include "core/video_service.h"
#include "ipc/video_gateway.h"
#include "runtime_config/app_runtime_config_loader.h"

VideoDaemonApp::VideoDaemonApp(QObject *parent)
    : VideoDaemonApp(loadAppRuntimeConfig(QString()).config, parent) {
}

VideoDaemonApp::VideoDaemonApp(const AppRuntimeConfig &config, QObject *parent)
    : QObject(parent)
    , config_(config)
    , service_(new VideoService(config_, nullptr, nullptr, this))
    , gateway_(new VideoGateway(service_, this)) {
}

bool VideoDaemonApp::start() {
    if (!gateway_->start()) {
        return false;
    }
    service_->startPreview(config_.video.cameraId);
    return true;
}
