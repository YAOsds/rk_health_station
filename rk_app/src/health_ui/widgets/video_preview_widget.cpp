#include "widgets/video_preview_widget.h"

#include "widgets/video_preview_consumer.h"

#include <QDebug>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QVBoxLayout>

VideoPreviewWidget::VideoPreviewWidget(QWidget *parent)
    : QWidget(parent)
    , frameLabel_(new QLabel(QStringLiteral("Preview unavailable"), this))
    , overlayLabel_(new QLabel(QStringLiteral("Preview unavailable"), this))
    , classificationLabel_(new QLabel(frameLabel_))
    , consumer_(new VideoPreviewConsumer(this))
{
    auto *layout = new QVBoxLayout(this);
    frameLabel_->setAlignment(Qt::AlignCenter);
    frameLabel_->setMinimumSize(320, 240);
    frameLabel_->setStyleSheet(QStringLiteral("background-color: #000; color: #ddd;"));
    layout->addWidget(frameLabel_, 1);
    layout->addWidget(overlayLabel_);

    classificationLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    classificationLabel_->setMargin(8);
    classificationLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    classificationLabel_->hide();

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
    classificationLabel_->setText(text);
    applyClassificationStyle(severity);
    classificationLabel_->adjustSize();
    updateClassificationGeometry();
    classificationLabel_->show();
    classificationLabel_->raise();
}

void VideoPreviewWidget::clearClassificationOverlay() {
    classificationLabel_->clear();
    classificationLabel_->hide();
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
    return classificationLabel_->text();
}

void VideoPreviewWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    renderFrame();
    updateClassificationGeometry();
}

void VideoPreviewWidget::renderFrame() {
    if (currentFrame_.isNull() || frameLabel_->size().isEmpty()) {
        return;
    }

    frameLabel_->setPixmap(QPixmap::fromImage(
        currentFrame_.scaled(frameLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    frameLabel_->setText(QString());
}

void VideoPreviewWidget::updateClassificationGeometry() {
    if (classificationLabel_->text().isEmpty()) {
        return;
    }

    classificationLabel_->adjustSize();
    const int margin = 12;
    classificationLabel_->move(margin, margin);
}

void VideoPreviewWidget::applyClassificationStyle(OverlaySeverity severity) {
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
    classificationLabel_->setStyleSheet(style);
}
