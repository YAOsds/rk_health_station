#pragma once

#include <QObject>

#include "runtime_config/app_runtime_config.h"

class VideoGateway;
class VideoService;

class VideoDaemonApp : public QObject {
    Q_OBJECT

public:
    explicit VideoDaemonApp(QObject *parent = nullptr);
    explicit VideoDaemonApp(const AppRuntimeConfig &config, QObject *parent = nullptr);
    bool start();

private:
    AppRuntimeConfig config_;
    VideoService *service_ = nullptr;
    VideoGateway *gateway_ = nullptr;
};
