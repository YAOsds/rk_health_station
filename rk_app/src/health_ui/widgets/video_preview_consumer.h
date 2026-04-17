#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

struct VideoPreviewSource {
    QString url;
    int width = 0;
    int height = 0;

    bool operator==(const VideoPreviewSource &other) const {
        return url == other.url && width == other.width && height == other.height;
    }
};

class VideoPreviewConsumer : public QObject {
    Q_OBJECT

public:
    explicit VideoPreviewConsumer(QObject *parent = nullptr);
    ~VideoPreviewConsumer() override;

    void start(const VideoPreviewSource &source);
    void stop();
    bool isActive() const;

signals:
    void frameReady(const QImage &frame);
    void errorTextChanged(const QString &text);
    void activeChanged(bool active);

private:
    bool configureSource(const VideoPreviewSource &source, QString *errorText);
    void clearBuffers();
    void setActive(bool active);
    void processPendingData();
    void processSocketData(const QByteArray &chunk);
    void emitJpegFrame(const QByteArray &jpegBytes);
    void trimBufferIfNeeded();
    QString previewHost_;
    quint16 previewPort_ = 0;
    QByteArray boundaryMarker_;
    QTcpSocket *socket_ = nullptr;
    VideoPreviewSource currentSource_;
    QByteArray streamBuffer_;
    bool active_ = false;
    bool stopping_ = false;
};
