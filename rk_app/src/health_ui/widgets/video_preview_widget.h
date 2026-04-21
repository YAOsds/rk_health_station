#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QStringList>
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

    struct OverlayEntry {
        int iconId = -1;
        QString state;
        double confidence = 0.0;
        QPointF anchor;
        QRectF bbox;
    };

    explicit VideoPreviewWidget(QWidget *parent = nullptr);

    void setPreviewSource(const QString &url, int width, int height);
    void setErrorText(const QString &text);
    void setClassificationOverlay(const QString &text, OverlaySeverity severity);
    void setClassificationRows(const QVector<ClassificationOverlayRow> &rows);
    void setOverlayEntries(const QVector<OverlayEntry> &entries);
    QVector<OverlayEntry> overlayEntries() const;
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
    void updateOverlayGeometry();
    QPointF scalePointFromSource(const QPointF &point) const;
    void applyClassificationStyle(QLabel *label, OverlaySeverity severity);
    void ensureClassificationLabels(int count);
    void ensureOverlayLabels(int count);

    QLabel *frameLabel_ = nullptr;
    QLabel *overlayLabel_ = nullptr;
    QLabel *sourceBadgeLabel_ = nullptr;
    QVector<QLabel *> classificationLabels_;
    QVector<QLabel *> overlayLabels_;
    QVector<OverlayEntry> overlayEntries_;
    VideoPreviewConsumer *consumer_ = nullptr;
    QImage currentFrame_;
};
