#pragma once

#include "models/fall_models.h"
#include "protocol/video_ipc.h"
#include "widgets/video_preview_widget.h"

#include <QStringList>
#include <QWidget>

class AbstractVideoClient;
class AbstractFallClient;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

class VideoMonitorPage : public QWidget {
    Q_OBJECT

public:
    explicit VideoMonitorPage(
        AbstractVideoClient *client,
        AbstractFallClient *fallClient,
        QWidget *parent = nullptr);

    QString cameraStateText() const;
    QString storageDirText() const;
    QString previewOverlayText() const;
    QStringList previewOverlayRows() const;
    QPushButton *startRecordingButton() const;
    QPushButton *stopRecordingButton() const;

private slots:
    void onStatusReceived(const VideoChannelStatus &status);
    void onCommandFinished(const VideoCommandResult &result);
    void onClassificationUpdated(const FallClassificationResult &result);
    void onClassificationBatchUpdated(const FallClassificationBatch &batch);
    void onFallConnectionChanged(bool connected);
    void onNoPersonTimeout();

private:
    static constexpr int kNoPersonOverlayTimeoutMs = 1500;
    static QString cameraStateLabel(VideoCameraState state);
    static QString overlayTextForResult(const FallClassificationResult &result);
    static QVector<VideoPreviewWidget::ClassificationOverlayRow> overlayRowsForBatch(
        const FallClassificationBatch &batch);
    static VideoPreviewWidget::OverlaySeverity overlaySeverityForState(const QString &state);
    void refreshButtonState(const VideoChannelStatus &status);
    void resetClassificationState();
    void refreshNoPersonTimer();

    AbstractVideoClient *client_ = nullptr;
    AbstractFallClient *fallClient_ = nullptr;
    QString currentCameraId_ = QStringLiteral("front_cam");
    bool previewAvailable_ = false;
    bool fallConnected_ = false;
    bool hasFreshClassification_ = false;
    QLabel *cameraStateValue_ = nullptr;
    QLabel *storageDirValue_ = nullptr;
    QLabel *lastSnapshotValue_ = nullptr;
    QLabel *currentRecordValue_ = nullptr;
    QLabel *lastErrorValue_ = nullptr;
    QLineEdit *storageDirEdit_ = nullptr;
    QPushButton *takeSnapshotButton_ = nullptr;
    QPushButton *startRecordingButton_ = nullptr;
    QPushButton *stopRecordingButton_ = nullptr;
    QPushButton *refreshStatusButton_ = nullptr;
    QPushButton *applyDirectoryButton_ = nullptr;
    VideoPreviewWidget *previewWidget_ = nullptr;
    QTimer *noPersonTimer_ = nullptr;
};
