#include "app/video_daemon_app.h"

#include "core/video_service.h"
#include "ipc/video_gateway.h"
#include "runtime_config/app_runtime_config_loader.h"

#include <QDebug>

VideoDaemonApp::VideoDaemonApp(QObject *parent)
    : QObject(parent) {
    const LoadedAppRuntimeConfig loaded = loadAppRuntimeConfig(QString());
    config_ = loaded.config;
    configValid_ = loaded.ok;
    configErrors_ = loaded.errors;
    if (configValid_) {
        initializeRuntimeObjects();
    }
}

VideoDaemonApp::VideoDaemonApp(const AppRuntimeConfig &config, QObject *parent)
    : QObject(parent)
    , config_(config) {
    initializeRuntimeObjects();
}

bool VideoDaemonApp::hasValidRuntimeConfig() const {
    return configValid_;
}

void VideoDaemonApp::initializeRuntimeObjects() {
    service_ = new VideoService(config_, nullptr, nullptr, this);
    gateway_ = new VideoGateway(config_.ipc.videoSocketPath, service_, this);
}

bool VideoDaemonApp::start() {
    if (!configValid_) {
        for (const QString &error : configErrors_) {
            qCritical().noquote() << "runtime_config error:" << error;
        }
        return false;
    }

    if (!service_ || !gateway_) {
        qCritical() << "health-videod runtime: daemon objects not initialized";
        return false;
    }

    if (!gateway_->start()) {
        return false;
    }
    service_->startPreview(config_.video.cameraId);
    return true;
}
