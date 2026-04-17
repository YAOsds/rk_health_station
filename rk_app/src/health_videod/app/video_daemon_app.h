#pragma once

#include <QObject>

class VideoGateway;
class VideoService;

class VideoDaemonApp : public QObject {
    Q_OBJECT

public:
    explicit VideoDaemonApp(QObject *parent = nullptr);
    bool start();

private:
    VideoService *service_ = nullptr;
    VideoGateway *gateway_ = nullptr;
};
