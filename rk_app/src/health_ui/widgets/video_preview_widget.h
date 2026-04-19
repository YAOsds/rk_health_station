#pragma once

#include <QStringList>
#include <QImage>
#include <QSize>
#include <QVector>
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

    struct ClassificationOverlayRow {
        QString text;
        OverlaySeverity severity = OverlaySeverity::Muted;
    };

    explicit VideoPreviewWidget(QWidget *parent = nullptr);

    void setPreviewSource(const QString &url, int width, int height);
    void setErrorText(const QString &text);
    void setClassificationOverlay(const QString &text, OverlaySeverity severity);
    void setClassificationRows(const QVector<ClassificationOverlayRow> &rows);
    void clearClassificationOverlay();
    void setSourceBadge(const QString &title, const QString &subtitle = QString());
    bool hasRenderedFrame() const;
    QSize renderedFrameSize() const;
    QString statusText() const;
    QString classificationText() const;
    QStringList classificationRows() const;
    QString sourceBadgeText() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void renderFrame();
    void updateClassificationGeometry();
    void updateSourceBadgeGeometry();
    void applyClassificationStyle(QLabel *label, OverlaySeverity severity);
    void ensureClassificationLabels(int count);

    QLabel *frameLabel_ = nullptr;
    QLabel *overlayLabel_ = nullptr;
    QLabel *sourceBadgeLabel_ = nullptr;
    QVector<QLabel *> classificationLabels_;
    VideoPreviewConsumer *consumer_ = nullptr;
    QImage currentFrame_;
};
