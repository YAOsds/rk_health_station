#include "ipc_client/fall_ipc_client.h"
#include "pages/video_monitor_page.h"

#include "ipc_client/video_ipc_client.h"
#include "widgets/video_preview_widget.h"

#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QDebug>
#include <utility>

VideoMonitorPage::VideoMonitorPage(
    AbstractVideoClient *client, AbstractFallClient *fallClient, QWidget *parent,
    TestVideoPicker pickTestVideo)
    : QWidget(parent)
    , client_(client)
    , fallClient_(fallClient)
    , cameraStateValue_(new QLabel(QStringLiteral("Unavailable"), this))
    , storageDirValue_(new QLabel(QStringLiteral("/home/elf/videosurv/"), this))
    , inputModeValue_(new QLabel(QStringLiteral("Camera"), this))
    , testFileValue_(new QLabel(QStringLiteral("--"), this))
    , lastSnapshotValue_(new QLabel(QStringLiteral("--"), this))
    , currentRecordValue_(new QLabel(QStringLiteral("--"), this))
    , lastErrorValue_(new QLabel(QStringLiteral("--"), this))
    , storageDirEdit_(new QLineEdit(QStringLiteral("/home/elf/videosurv/"), this))
    , takeSnapshotButton_(new QPushButton(QStringLiteral("Take Snapshot"), this))
    , startRecordingButton_(new QPushButton(QStringLiteral("Start Recording"), this))
    , stopRecordingButton_(new QPushButton(QStringLiteral("Stop Recording"), this))
    , selectTestVideoButton_(new QPushButton(QStringLiteral("Select Test Video"), this))
    , exitTestModeButton_(new QPushButton(QStringLiteral("Exit Test Mode"), this))
    , refreshStatusButton_(new QPushButton(QStringLiteral("Refresh Status"), this))
    , applyDirectoryButton_(new QPushButton(QStringLiteral("Apply Directory"), this))
    , previewWidget_(new VideoPreviewWidget(this))
    , noPersonTimer_(new QTimer(this))
    , pickTestVideo_(std::move(pickTestVideo)) {
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->addWidget(previewWidget_);

    auto *statusLayout = new QFormLayout();
    statusLayout->addRow(QStringLiteral("Camera State"), cameraStateValue_);
    statusLayout->addRow(QStringLiteral("Storage Dir"), storageDirValue_);
    statusLayout->addRow(QStringLiteral("Input Mode"), inputModeValue_);
    statusLayout->addRow(QStringLiteral("Test File"), testFileValue_);
    statusLayout->addRow(QStringLiteral("Last Snapshot"), lastSnapshotValue_);
    statusLayout->addRow(QStringLiteral("Current Record"), currentRecordValue_);
    statusLayout->addRow(QStringLiteral("Last Error"), lastErrorValue_);
    rootLayout->addLayout(statusLayout);

    auto *directoryLayout = new QHBoxLayout();
    directoryLayout->addWidget(storageDirEdit_);
    directoryLayout->addWidget(applyDirectoryButton_);
    rootLayout->addLayout(directoryLayout);

    auto *controlsLayout = new QHBoxLayout();
    controlsLayout->addWidget(takeSnapshotButton_);
    controlsLayout->addWidget(startRecordingButton_);
    controlsLayout->addWidget(stopRecordingButton_);
    controlsLayout->addWidget(refreshStatusButton_);
    rootLayout->addLayout(controlsLayout);

    auto *testControlsLayout = new QHBoxLayout();
    testControlsLayout->addWidget(selectTestVideoButton_);
    testControlsLayout->addWidget(exitTestModeButton_);
    testControlsLayout->addStretch(1);
    rootLayout->addLayout(testControlsLayout);

    stopRecordingButton_->setEnabled(false);
    exitTestModeButton_->setEnabled(false);
    noPersonTimer_->setSingleShot(true);

    connect(client_, &AbstractVideoClient::statusReceived,
        this, &VideoMonitorPage::onStatusReceived);
    connect(client_, &AbstractVideoClient::commandFinished,
        this, &VideoMonitorPage::onCommandFinished);
    if (fallClient_ != nullptr) {
        connect(fallClient_, &AbstractFallClient::classificationUpdated,
            this, &VideoMonitorPage::onClassificationUpdated);
        connect(fallClient_, &AbstractFallClient::classificationBatchUpdated,
            this, &VideoMonitorPage::onClassificationBatchUpdated);
        connect(fallClient_, &AbstractFallClient::connectionChanged,
            this, &VideoMonitorPage::onFallConnectionChanged);
    }
    connect(noPersonTimer_, &QTimer::timeout, this, &VideoMonitorPage::onNoPersonTimeout);

    connect(refreshStatusButton_, &QPushButton::clicked, this, [this]() {
        client_->requestStatus(currentCameraId_);
    });
    connect(takeSnapshotButton_, &QPushButton::clicked, this, [this]() {
        client_->takeSnapshot(currentCameraId_);
    });
    connect(startRecordingButton_, &QPushButton::clicked, this, [this]() {
        client_->startRecording(currentCameraId_);
    });
    connect(stopRecordingButton_, &QPushButton::clicked, this, [this]() {
        client_->stopRecording(currentCameraId_);
    });
    connect(selectTestVideoButton_, &QPushButton::clicked, this, [this]() {
        const QString path = pickTestVideo_ ? pickTestVideo_() : QString();
        if (!path.isEmpty()) {
            client_->startTestInput(currentCameraId_, path);
        }
    });
    connect(exitTestModeButton_, &QPushButton::clicked, this, [this]() {
        client_->stopTestInput(currentCameraId_);
    });
    connect(applyDirectoryButton_, &QPushButton::clicked, this, [this]() {
        client_->setStorageDir(currentCameraId_, storageDirEdit_->text());
    });
}

QString VideoMonitorPage::cameraStateText() const {
    return cameraStateValue_->text();
}

QString VideoMonitorPage::storageDirText() const {
    return storageDirValue_->text();
}

QString VideoMonitorPage::inputModeText() const {
    return inputModeValue_->text();
}

QString VideoMonitorPage::testFileText() const {
    return testFileValue_->text();
}

QString VideoMonitorPage::previewOverlayText() const {
    return previewWidget_->classificationText();
}

QStringList VideoMonitorPage::previewOverlayRows() const {
    return previewWidget_->classificationRows();
}

QPushButton *VideoMonitorPage::takeSnapshotButton() const {
    return takeSnapshotButton_;
}

QPushButton *VideoMonitorPage::startRecordingButton() const {
    return startRecordingButton_;
}

QPushButton *VideoMonitorPage::stopRecordingButton() const {
    return stopRecordingButton_;
}

QPushButton *VideoMonitorPage::exitTestModeButton() const {
    return exitTestModeButton_;
}

void VideoMonitorPage::onStatusReceived(const VideoChannelStatus &status) {
    qInfo() << "health-ui video: status received"
            << "camera_id=" << status.cameraId
            << "state=" << cameraStateLabel(status.cameraState)
            << "preview_url=" << status.previewUrl
            << "recording=" << status.recording;
    currentCameraId_ = status.cameraId;
    cameraStateValue_->setText(cameraStateLabel(status.cameraState));
    storageDirValue_->setText(status.storageDir);
    inputModeValue_->setText(inputModeLabel(status));
    testFileValue_->setText(
        status.testFilePath.isEmpty() ? QStringLiteral("--") : QFileInfo(status.testFilePath).fileName());
    storageDirEdit_->setText(status.storageDir);
    lastSnapshotValue_->setText(status.lastSnapshotPath.isEmpty() ? QStringLiteral("--") : status.lastSnapshotPath);
    currentRecordValue_->setText(status.currentRecordPath.isEmpty() ? QStringLiteral("--") : status.currentRecordPath);
    lastErrorValue_->setText(status.lastError.isEmpty() ? QStringLiteral("--") : status.lastError);
    previewWidget_->setPreviewSource(
        status.previewUrl, status.previewProfile.width, status.previewProfile.height);
    previewWidget_->setSourceBadge(
        status.inputMode == QStringLiteral("test_file") ? QStringLiteral("TEST MODE") : QString(),
        status.inputMode == QStringLiteral("test_file") ? QFileInfo(status.testFilePath).fileName() : QString());
    previewAvailable_ = !status.previewUrl.isEmpty();
    if (!previewAvailable_) {
        resetClassificationState();
    } else {
        refreshNoPersonTimer();
    }
    refreshButtonState(status);
}

void VideoMonitorPage::onCommandFinished(const VideoCommandResult &result) {
    qInfo() << "health-ui video: command finished"
            << "action=" << result.action
            << "ok=" << result.ok
            << "error=" << result.errorCode;
    if (!result.ok) {
        lastErrorValue_->setText(result.errorCode);
        previewWidget_->setErrorText(result.errorCode);
        resetClassificationState();
        return;
    }

    if (result.action != QStringLiteral("get_status")) {
        client_->requestStatus(currentCameraId_);
    }
}

void VideoMonitorPage::onClassificationUpdated(const FallClassificationResult &result) {
    if (!result.cameraId.isEmpty() && result.cameraId != currentCameraId_) {
        return;
    }
    if (!previewAvailable_) {
        return;
    }

    FallClassificationBatch batch;
    batch.cameraId = result.cameraId;
    batch.timestampMs = result.timestampMs;
    FallClassificationEntry entry;
    entry.state = result.state;
    entry.confidence = result.confidence;
    batch.results.push_back(entry);
    hasFreshClassification_ = true;
    previewWidget_->setClassificationRows(overlayRowsForBatch(batch));
    refreshNoPersonTimer();
}

void VideoMonitorPage::onClassificationBatchUpdated(const FallClassificationBatch &batch) {
    if (!batch.cameraId.isEmpty() && batch.cameraId != currentCameraId_) {
        return;
    }
    if (!previewAvailable_) {
        return;
    }

    hasFreshClassification_ = !batch.results.isEmpty();
    previewWidget_->setClassificationRows(overlayRowsForBatch(batch));
    refreshNoPersonTimer();
}

void VideoMonitorPage::onFallConnectionChanged(bool connected) {
    fallConnected_ = connected;
    if (!fallConnected_) {
        resetClassificationState();
        return;
    }
    refreshNoPersonTimer();
}

void VideoMonitorPage::onNoPersonTimeout() {
    hasFreshClassification_ = false;
    if (!previewAvailable_ || !fallConnected_) {
        return;
    }

    previewWidget_->clearClassificationOverlay();
}

QString VideoMonitorPage::cameraStateLabel(VideoCameraState state) {
    switch (state) {
    case VideoCameraState::Unavailable:
        return QStringLiteral("Unavailable");
    case VideoCameraState::Idle:
        return QStringLiteral("Idle");
    case VideoCameraState::Previewing:
        return QStringLiteral("Previewing");
    case VideoCameraState::Recording:
        return QStringLiteral("Recording");
    case VideoCameraState::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unavailable");
}

QString VideoMonitorPage::inputModeLabel(const VideoChannelStatus &status) {
    if (status.inputMode != QStringLiteral("test_file")) {
        return QStringLiteral("Camera");
    }
    if (status.testPlaybackState == QStringLiteral("finished")) {
        return QStringLiteral("Test Video (Finished)");
    }
    if (status.testPlaybackState == QStringLiteral("error")) {
        return QStringLiteral("Test Video (Error)");
    }
    return QStringLiteral("Test Video");
}

QString VideoMonitorPage::overlayTextForResult(const FallClassificationResult &result) {
    return QStringLiteral("%1 %2")
        .arg(result.state)
        .arg(QString::number(result.confidence, 'f', 2));
}

QVector<VideoPreviewWidget::ClassificationOverlayRow> VideoMonitorPage::overlayRowsForBatch(
    const FallClassificationBatch &batch) {
    QVector<VideoPreviewWidget::ClassificationOverlayRow> rows;
    if (batch.results.isEmpty()) {
        rows.push_back({
            QStringLiteral("no person"),
            VideoPreviewWidget::OverlaySeverity::Muted,
        });
        return rows;
    }

    const int maxRows = qMin(batch.results.size(), 5);
    rows.reserve(maxRows);
    for (int index = 0; index < maxRows; ++index) {
        const FallClassificationEntry &entry = batch.results.at(index);
        const QString prefix = entry.iconId > 0
            ? QStringLiteral("[%1] ").arg(entry.iconId)
            : QString();
        rows.push_back({
            QStringLiteral("%1%2 %3")
                .arg(prefix)
                .arg(entry.state)
                .arg(QString::number(entry.confidence, 'f', 2)),
            overlaySeverityForState(entry.state),
        });
    }
    return rows;
}

VideoPreviewWidget::OverlaySeverity VideoMonitorPage::overlaySeverityForState(
    const QString &state) {
    if (state == QStringLiteral("fall")) {
        return VideoPreviewWidget::OverlaySeverity::Alert;
    }
    if (state == QStringLiteral("lie")) {
        return VideoPreviewWidget::OverlaySeverity::Warning;
    }
    if (state == QStringLiteral("stand")) {
        return VideoPreviewWidget::OverlaySeverity::Normal;
    }
    return VideoPreviewWidget::OverlaySeverity::Muted;
}

void VideoMonitorPage::refreshButtonState(const VideoChannelStatus &status) {
    const bool testMode = status.inputMode == QStringLiteral("test_file");
    const bool isRecording = status.cameraState == VideoCameraState::Recording || status.recording;
    takeSnapshotButton_->setEnabled(!testMode);
    startRecordingButton_->setEnabled(!testMode && !isRecording);
    stopRecordingButton_->setEnabled(!testMode && isRecording);
    selectTestVideoButton_->setEnabled(!currentCameraId_.isEmpty());
    exitTestModeButton_->setEnabled(testMode);
}

void VideoMonitorPage::resetClassificationState() {
    hasFreshClassification_ = false;
    noPersonTimer_->stop();
    previewWidget_->clearClassificationOverlay();
}

void VideoMonitorPage::refreshNoPersonTimer() {
    if (!previewAvailable_ || !fallConnected_) {
        noPersonTimer_->stop();
        return;
    }

    noPersonTimer_->start(kNoPersonOverlayTimeoutMs);
}
