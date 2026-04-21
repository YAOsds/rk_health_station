#include "widgets/video_preview_widget.h"

#include "widgets/video_preview_consumer.h"

#include <QColor>
#include <QDebug>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace {
QColor colorForIcon(int iconId) {
    static const QVector<QColor> palette = {
        QColor(QStringLiteral("#1d4ed8")),
        QColor(QStringLiteral("#7c3aed")),
        QColor(QStringLiteral("#047857")),
        QColor(QStringLiteral("#b45309")),
        QColor(QStringLiteral("#be123c")),
    };
    if (iconId <= 0) {
        return QColor(QStringLiteral("#475569"));
    }
    return palette.at((iconId - 1) % palette.size());
}
}

VideoPreviewWidget::VideoPreviewWidget(QWidget *parent)
    : QWidget(parent)
    , frameLabel_(new QLabel(QStringLiteral("Preview unavailable"), this))
    , overlayLabel_(new QLabel(QStringLiteral("Preview unavailable"), this))
    , sourceBadgeLabel_(new QLabel(frameLabel_))
    , consumer_(new VideoPreviewConsumer(this))
{
    auto *layout = new QVBoxLayout(this);
    frameLabel_->setAlignment(Qt::AlignCenter);
    frameLabel_->setMinimumSize(320, 240);
    frameLabel_->setStyleSheet(QStringLiteral("background-color: #000; color: #ddd;"));
    layout->addWidget(frameLabel_, 1);
    layout->addWidget(overlayLabel_);

    sourceBadgeLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    sourceBadgeLabel_->setMargin(8);
    sourceBadgeLabel_->setWordWrap(true);
    sourceBadgeLabel_->setStyleSheet(
        QStringLiteral("color: #ffffff; background-color: rgba(18, 18, 18, 190); "
                       "border-radius: 10px; padding: 6px 10px; font: 700 16px;"));
    sourceBadgeLabel_->hide();

    connect(consumer_, &VideoPreviewConsumer::frameReady, this, [this](const QImage &frame) {
        currentFrame_ = frame;
        renderFrame();
    });
    connect(consumer_, &VideoPreviewConsumer::errorTextChanged,
        this, &VideoPreviewWidget::setErrorText);
}

void VideoPreviewWidget::setPreviewSource(const QString &url, int width, int height) {
    qInfo() << "health-ui video: set preview source" << url << width << height;

    if (url.isEmpty()) {
        consumer_->stop();
        currentFrame_ = QImage();
        frameLabel_->setPixmap(QPixmap());
        frameLabel_->setText(QStringLiteral("Preview unavailable"));
        overlayLabel_->setText(QStringLiteral("Preview unavailable"));
        setOverlayEntries({});
        return;
    }

    overlayLabel_->setText(QStringLiteral("Playing: %1").arg(url));
    if (currentFrame_.isNull()) {
        frameLabel_->setPixmap(QPixmap());
        frameLabel_->setText(QStringLiteral("Connecting preview..."));
    }

    VideoPreviewSource source;
    source.url = url;
    source.width = width;
    source.height = height;
    consumer_->start(source);
}

void VideoPreviewWidget::setErrorText(const QString &text) {
    qWarning() << "health-ui video: preview error text" << text;
    overlayLabel_->setText(text);
    if (currentFrame_.isNull()) {
        frameLabel_->setPixmap(QPixmap());
        frameLabel_->setText(text);
    }
}

void VideoPreviewWidget::setClassificationOverlay(
    const QString &text, OverlaySeverity severity) {
    setClassificationRows({ClassificationOverlayRow{text, severity}});
}

void VideoPreviewWidget::setClassificationRows(const QVector<ClassificationOverlayRow> &rows) {
    const int visibleRowCount = qMin(rows.size(), 5);
    ensureClassificationLabels(visibleRowCount);

    for (int index = 0; index < visibleRowCount; ++index) {
        QLabel *label = classificationLabels_.at(index);
        label->setText(rows.at(index).text);
        applyClassificationStyle(label, rows.at(index).severity);
        label->adjustSize();
        label->show();
        label->raise();
    }

    for (int index = visibleRowCount; index < classificationLabels_.size(); ++index) {
        classificationLabels_.at(index)->clear();
        classificationLabels_.at(index)->hide();
    }

    updateClassificationGeometry();
}

void VideoPreviewWidget::setOverlayEntries(const QVector<OverlayEntry> &entries) {
    overlayEntries_ = entries;
    ensureOverlayLabels(overlayEntries_.size());
    updateOverlayGeometry();
}

QVector<VideoPreviewWidget::OverlayEntry> VideoPreviewWidget::overlayEntries() const {
    return overlayEntries_;
}

void VideoPreviewWidget::clearClassificationOverlay() {
    setClassificationRows({});
    setOverlayEntries({});
}

void VideoPreviewWidget::setSourceBadge(const QString &title, const QString &subtitle) {
    const QString text = subtitle.isEmpty()
        ? title
        : QStringLiteral("%1\n%2").arg(title, subtitle);
    sourceBadgeLabel_->setText(text);
    sourceBadgeLabel_->setVisible(!text.isEmpty());
    sourceBadgeLabel_->adjustSize();
    updateSourceBadgeGeometry();
}

bool VideoPreviewWidget::hasRenderedFrame() const {
    return !currentFrame_.isNull();
}

QSize VideoPreviewWidget::renderedFrameSize() const {
    return currentFrame_.size();
}

QString VideoPreviewWidget::statusText() const {
    return overlayLabel_->text();
}

QString VideoPreviewWidget::classificationText() const {
    return classificationRows().join(QLatin1Char('\n'));
}

QStringList VideoPreviewWidget::classificationRows() const {
    QStringList rows;
    for (QLabel *label : classificationLabels_) {
        if (label != nullptr && !label->isHidden() && !label->text().isEmpty()) {
            rows.push_back(label->text());
        }
    }
    return rows;
}

QString VideoPreviewWidget::sourceBadgeText() const {
    return sourceBadgeLabel_->text();
}

void VideoPreviewWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    renderFrame();
    updateClassificationGeometry();
    updateSourceBadgeGeometry();
    updateOverlayGeometry();
}

void VideoPreviewWidget::renderFrame() {
    if (currentFrame_.isNull() || frameLabel_->size().isEmpty()) {
        updateOverlayGeometry();
        return;
    }

    frameLabel_->setPixmap(QPixmap::fromImage(
        currentFrame_.scaled(frameLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    frameLabel_->setText(QString());
    updateOverlayGeometry();
}

void VideoPreviewWidget::updateClassificationGeometry() {
    const int margin = 12;
    const int spacing = 8;
    int y = margin;
    for (QLabel *label : classificationLabels_) {
        if (label == nullptr || label->isHidden() || label->text().isEmpty()) {
            continue;
        }
        label->adjustSize();
        label->move(margin, y);
        y += label->height() + spacing;
    }
}

void VideoPreviewWidget::updateSourceBadgeGeometry() {
    if (sourceBadgeLabel_ == nullptr || sourceBadgeLabel_->isHidden() || frameLabel_ == nullptr) {
        return;
    }
    const int margin = 12;
    sourceBadgeLabel_->adjustSize();
    sourceBadgeLabel_->move(
        qMax(margin, frameLabel_->width() - sourceBadgeLabel_->width() - margin),
        margin);
    sourceBadgeLabel_->raise();
}

void VideoPreviewWidget::updateOverlayGeometry() {
    ensureOverlayLabels(overlayEntries_.size());

    for (int index = 0; index < overlayEntries_.size(); ++index) {
        const OverlayEntry &entry = overlayEntries_.at(index);
        QLabel *label = overlayLabels_.at(index);
        const QPointF anchor = entry.anchor.isNull()
            ? QPointF(entry.bbox.center().x(), entry.bbox.top())
            : entry.anchor;
        const QPointF scaled = scalePointFromSource(anchor);
        const int badgeWidth = 34;
        const int badgeHeight = 24;
        const int x = qRound(scaled.x()) - (badgeWidth / 2);
        const int y = qRound(scaled.y()) - 36;

        label->setText(entry.iconId > 0 ? QString::number(entry.iconId) : QStringLiteral("?"));
        label->setAlignment(Qt::AlignCenter);
        label->setFixedSize(badgeWidth, badgeHeight);
        const QColor fill = colorForIcon(entry.iconId);
        const QString border = entry.state == QStringLiteral("fall")
            ? QStringLiteral("2px solid #d62828")
            : QStringLiteral("none");
        label->setStyleSheet(QStringLiteral(
            "color: #ffffff; background-color: %1; border: %2; border-radius: 12px; font: 700 15px;")
                .arg(fill.name())
                .arg(border));
        label->move(x, qMax(0, y));
        label->show();
        label->raise();
    }

    for (int index = overlayEntries_.size(); index < overlayLabels_.size(); ++index) {
        overlayLabels_.at(index)->clear();
        overlayLabels_.at(index)->hide();
    }
}

QPointF VideoPreviewWidget::scalePointFromSource(const QPointF &point) const {
    if (currentFrame_.isNull() || frameLabel_ == nullptr || frameLabel_->size().isEmpty()) {
        return point;
    }

    const QSize scaled = currentFrame_.size().scaled(frameLabel_->size(), Qt::KeepAspectRatio);
    const qreal offsetX = (frameLabel_->width() - scaled.width()) / 2.0;
    const qreal offsetY = (frameLabel_->height() - scaled.height()) / 2.0;
    const qreal scaleX = scaled.width() / static_cast<qreal>(currentFrame_.width());
    const qreal scaleY = scaled.height() / static_cast<qreal>(currentFrame_.height());

    return QPointF(offsetX + (point.x() * scaleX), offsetY + (point.y() * scaleY));
}

void VideoPreviewWidget::applyClassificationStyle(
    QLabel *label, OverlaySeverity severity) {
    QString style =
        QStringLiteral("color: #ffffff; border-radius: 10px; padding: 4px 10px;");
    switch (severity) {
    case OverlaySeverity::Normal:
        style += QStringLiteral("background-color: rgba(18, 128, 74, 190); font: 600 18px;");
        break;
    case OverlaySeverity::Warning:
        style += QStringLiteral("background-color: rgba(188, 108, 0, 205); font: 700 20px;");
        break;
    case OverlaySeverity::Alert:
        style += QStringLiteral("background-color: rgba(184, 28, 28, 225); font: 800 26px;");
        break;
    case OverlaySeverity::Muted:
        style += QStringLiteral("background-color: rgba(70, 70, 70, 190); font: 600 18px;");
        break;
    }
    label->setStyleSheet(style);
}

void VideoPreviewWidget::ensureClassificationLabels(int count) {
    while (classificationLabels_.size() < count) {
        auto *label = new QLabel(frameLabel_);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMargin(8);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->hide();
        classificationLabels_.push_back(label);
    }
}

void VideoPreviewWidget::ensureOverlayLabels(int count) {
    while (overlayLabels_.size() < count) {
        auto *label = new QLabel(frameLabel_);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->hide();
        overlayLabels_.push_back(label);
    }
}
