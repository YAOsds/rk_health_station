#pragma once

#include <QImage>
#include <QSize>
#include <QWidget>

class QLabel;
class VideoPreviewConsumer;

class VideoPreviewWidget : public QWidget {
    Q_OBJECT

public:
    enum class OverlaySeverity {
        Normal,
        Warning,
        Alert,
        Muted
    };

    explicit VideoPreviewWidget(QWidget *parent = nullptr);

    void setPreviewSource(const QString &url, int width, int height);
    void setErrorText(const QString &text);
    void setClassificationOverlay(const QString &text, OverlaySeverity severity);
    void clearClassificationOverlay();
    bool hasRenderedFrame() const;
    QSize renderedFrameSize() const;
    QString statusText() const;
    QString classificationText() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void renderFrame();
    void updateClassificationGeometry();
    void applyClassificationStyle(OverlaySeverity severity);

    QLabel *frameLabel_ = nullptr;
    QLabel *overlayLabel_ = nullptr;
    QLabel *classificationLabel_ = nullptr;
    VideoPreviewConsumer *consumer_ = nullptr;
    QImage currentFrame_;
};
