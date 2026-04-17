# Video Preview Fall Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show realtime fall-classification results directly on the `health-ui` video preview, including `stand/fall/lie + confidence` and a `no person` fallback, without changing the existing video or fall backend boundaries.

**Architecture:** Keep `VideoIpcClient` and the video preview flow unchanged, add a dedicated `FallIpcClient` for `rk_fall.sock`, let `VideoMonitorPage` map backend state into overlay semantics, and keep `VideoPreviewWidget` as a pure renderer for preview frames plus a new classification overlay layer. The page remains the coordinator; the widget remains display-only.

**Tech Stack:** C++17, Qt Core/Network/Widgets/Test, QLocalSocket, CMake/CTest, RK3588 bundle deployment.

---

## File Structure Map

### New files

- `src/health_ui/ipc_client/fall_ipc_client.h`
- `src/health_ui/ipc_client/fall_ipc_client.cpp`
  - local client for `rk_fall.sock`, realtime `classification` parsing, and connection state signals
- `src/tests/ui_tests/fall_ipc_client_test.cpp`
  - protocol-level UI-side test for realtime classification subscription

### Existing files to modify

- `src/health_ui/CMakeLists.txt`
  - compile and link the new fall IPC client into `health-ui`
- `src/health_ui/pages/video_monitor_page.h`
- `src/health_ui/pages/video_monitor_page.cpp`
  - coordinate `VideoIpcClient` plus `FallIpcClient`, map overlay states, and expose test hooks
- `src/health_ui/widgets/video_preview_widget.h`
- `src/health_ui/widgets/video_preview_widget.cpp`
  - render a dedicated classification overlay with severity styling and `no person` display
- `src/health_ui/app/ui_app.cpp`
  - pass the new `FallIpcClient` into the video page
- `src/tests/ui_tests/video_monitor_page_test.cpp`
  - verify page-level mapping from realtime classification into overlay text
- `src/tests/ui_tests/video_preview_widget_test.cpp`
  - verify overlay rendering text/state behavior
- `src/tests/CMakeLists.txt`
  - register new UI test executables

### Existing files expected to remain unchanged

- `src/health_videod/**`
  - no preview pipeline changes
- `src/health_falld/**`
  - no new protocol coupling required; reuse existing realtime `classification` stream
- `src/health_ui/widgets/video_preview_consumer.*`
  - keep MJPEG consumption logic focused on preview transport only

## Notes Before Implementation

- This tree has no `.git`, so checkpoint commit steps remain documentary only.
- Follow TDD: every new UI behavior must begin with a failing test.
- Do not merge fall IPC into video IPC; keep the dual-client design intact.
- `no person` is a UI mapping, not a backend classification label.
- The preview error overlay still wins over classification overlay when preview transport is unavailable.

### Task 1: Add `FallIpcClient` for realtime `classification` subscription

**Files:**
- Create: `src/health_ui/ipc_client/fall_ipc_client.h`
- Create: `src/health_ui/ipc_client/fall_ipc_client.cpp`
- Modify: `src/health_ui/CMakeLists.txt`
- Modify: `src/tests/CMakeLists.txt`
- Create: `src/tests/ui_tests/fall_ipc_client_test.cpp`

- [ ] **Step 1: Write the failing UI-side protocol test**

Create `src/tests/ui_tests/fall_ipc_client_test.cpp` with this initial test:

```cpp
#include "ipc_client/fall_ipc_client.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QtTest/QTest>

class FallIpcClientTest : public QObject {
    Q_OBJECT

private slots:
    void receivesRealtimeClassification();
};

void FallIpcClientTest::receivesRealtimeClassification() {
    const QString socketName = QStringLiteral("/tmp/rk_fall_ui_client_test.sock");
    QLocalServer::removeServer(socketName);

    QLocalServer server;
    QVERIFY(server.listen(socketName));

    FallIpcClient client(socketName);
    QSignalSpy classificationSpy(&client, SIGNAL(classificationUpdated(FallClassificationResult)));

    QVERIFY(client.connectToBackend());
    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(socket->bytesAvailable() > 0 || socket->waitForReadyRead(50), 2000);
    QVERIFY(socket->readAll().contains("subscribe_classification"));

    const QByteArray line = QByteArrayLiteral(
        "{\"type\":\"classification\",\"camera_id\":\"front_cam\",\"ts\":1776359310534,\"state\":\"fall\",\"confidence\":0.93}\n");
    socket->write(line);
    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(classificationSpy.count(), 1, 2000);
    const FallClassificationResult result = classificationSpy.takeFirst().at(0).value<FallClassificationResult>();
    QCOMPARE(result.cameraId, QStringLiteral("front_cam"));
    QCOMPARE(result.state, QStringLiteral("fall"));
    QCOMPARE(result.confidence, 0.93);
}

QTEST_MAIN(FallIpcClientTest)
#include "fall_ipc_client_test.moc"
```

- [ ] **Step 2: Register and run the test to verify it fails**

Add a test target in `src/tests/CMakeLists.txt` and run:

```bash
cmake -S /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/rk_app -B /tmp/rk_health_station-ui
cmake --build /tmp/rk_health_station-ui --target fall_ipc_client_test -j4
ctest --test-dir /tmp/rk_health_station-ui -R fall_ipc_client_test --output-on-failure
```

Expected: FAIL because `FallIpcClient` does not exist yet.

- [ ] **Step 3: Implement the minimal client**

Create `src/health_ui/ipc_client/fall_ipc_client.h` with:

```cpp
#pragma once

#include "models/fall_models.h"

#include <QObject>

class QLocalSocket;

class FallIpcClient : public QObject {
    Q_OBJECT

public:
    explicit FallIpcClient(const QString &socketName = QStringLiteral("rk_fall.sock"), QObject *parent = nullptr);

    bool connectToBackend();
    void disconnectFromBackend();

signals:
    void classificationUpdated(const FallClassificationResult &result);
    void connectionChanged(bool connected);
    void errorOccurred(const QString &errorText);

private:
    void onReadyRead();

    QString socketName_;
    QLocalSocket *socket_ = nullptr;
    QByteArray readBuffer_;
};
```

Create `src/health_ui/ipc_client/fall_ipc_client.cpp` with the minimal behavior:

```cpp
#include "ipc_client/fall_ipc_client.h"

#include "protocol/fall_ipc.h"

#include <QJsonDocument>
#include <QLocalSocket>

FallIpcClient::FallIpcClient(const QString &socketName, QObject *parent)
    : QObject(parent)
    , socketName_(socketName)
    , socket_(new QLocalSocket(this)) {
    qRegisterMetaType<FallClassificationResult>("FallClassificationResult");
    connect(socket_, &QLocalSocket::readyRead, this, [this]() { onReadyRead(); });
    connect(socket_, &QLocalSocket::connected, this, [this]() { emit connectionChanged(true); });
    connect(socket_, &QLocalSocket::disconnected, this, [this]() { emit connectionChanged(false); });
}

bool FallIpcClient::connectToBackend() {
    if (socket_->state() == QLocalSocket::ConnectedState) {
        return true;
    }

    socket_->abort();
    socket_->connectToServer(socketName_);
    if (!socket_->waitForConnected(2000)) {
        emit errorOccurred(socket_->errorString());
        return false;
    }

    socket_->write("{\"action\":\"subscribe_classification\"}\n");
    socket_->flush();
    return true;
}

void FallIpcClient::disconnectFromBackend() {
    socket_->abort();
    readBuffer_.clear();
}

void FallIpcClient::onReadyRead() {
    readBuffer_.append(socket_->readAll());
    while (true) {
        const int newlineIndex = readBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        const QByteArray line = readBuffer_.left(newlineIndex).trimmed();
        readBuffer_.remove(0, newlineIndex + 1);
        if (line.isEmpty()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(line);
        FallClassificationResult result;
        if (document.isObject() && fallClassificationResultFromJson(document.object(), &result)) {
            emit classificationUpdated(result);
        }
    }
}
```

Add the new source files to `src/health_ui/CMakeLists.txt`.

- [ ] **Step 4: Re-run the test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-ui --target fall_ipc_client_test -j4
ctest --test-dir /tmp/rk_health_station-ui -R fall_ipc_client_test --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_ui/ipc_client/fall_ipc_client.h src/health_ui/ipc_client/fall_ipc_client.cpp src/health_ui/CMakeLists.txt src/tests/CMakeLists.txt src/tests/ui_tests/fall_ipc_client_test.cpp
git commit -m "feat: add fall ipc client for ui overlay"
```

### Task 2: Add a dedicated classification overlay layer to `VideoPreviewWidget`

**Files:**
- Modify: `src/health_ui/widgets/video_preview_widget.h`
- Modify: `src/health_ui/widgets/video_preview_widget.cpp`
- Modify: `src/tests/CMakeLists.txt`
- Create: `src/tests/ui_tests/video_preview_widget_test.cpp`

- [ ] **Step 1: Write the failing widget test**

Create `src/tests/ui_tests/video_preview_widget_test.cpp`:

```cpp
#include "widgets/video_preview_widget.h"

#include <QtTest/QTest>

class VideoPreviewWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void rendersFallOverlayText();
    void rendersNoPersonOverlayText();
};

void VideoPreviewWidgetTest::rendersFallOverlayText() {
    VideoPreviewWidget widget;
    widget.setClassificationOverlay(QStringLiteral("fall 0.93"), VideoPreviewWidget::OverlaySeverity::Alert);
    QCOMPARE(widget.classificationText(), QStringLiteral("fall 0.93"));
}

void VideoPreviewWidgetTest::rendersNoPersonOverlayText() {
    VideoPreviewWidget widget;
    widget.setClassificationOverlay(QStringLiteral("no person"), VideoPreviewWidget::OverlaySeverity::Muted);
    QCOMPARE(widget.classificationText(), QStringLiteral("no person"));
}

QTEST_MAIN(VideoPreviewWidgetTest)
#include "video_preview_widget_test.moc"
```

- [ ] **Step 2: Register and run the test to verify it fails**

Add a new target in `src/tests/CMakeLists.txt`, then run:

```bash
cmake --build /tmp/rk_health_station-ui --target video_preview_widget_test -j4
ctest --test-dir /tmp/rk_health_station-ui -R video_preview_widget_test --output-on-failure
```

Expected: FAIL because `setClassificationOverlay`, `classificationText`, and `OverlaySeverity` do not exist.

- [ ] **Step 3: Add the minimal overlay API and rendering**

Update `src/health_ui/widgets/video_preview_widget.h`:

```cpp
class VideoPreviewWidget : public QWidget {
    Q_OBJECT

public:
    enum class OverlaySeverity {
        Normal,
        Warning,
        Alert,
        Muted,
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
```

Add a new label member:

```cpp
    QLabel *classificationLabel_ = nullptr;
```

Update `src/health_ui/widgets/video_preview_widget.cpp` so the constructor creates `classificationLabel_`, styles it independently, and initially hides it:

```cpp
    , classificationLabel_(new QLabel(this))
```

Create it with a default style and add methods:

```cpp
void VideoPreviewWidget::setClassificationOverlay(const QString &text, OverlaySeverity severity) {
    classificationLabel_->setText(text);
    QString style = QStringLiteral("color:#e8f5e9;background:rgba(0,0,0,150);padding:6px 10px;border-radius:6px;font:600 16px 'DejaVu Sans';");
    switch (severity) {
    case OverlaySeverity::Alert:
        style = QStringLiteral("color:#fff1f1;background:rgba(183,28,28,190);padding:8px 14px;border-radius:8px;font:700 24px 'DejaVu Sans';");
        break;
    case OverlaySeverity::Warning:
        style = QStringLiteral("color:#fff8e1;background:rgba(245,124,0,180);padding:7px 12px;border-radius:7px;font:700 18px 'DejaVu Sans';");
        break;
    case OverlaySeverity::Muted:
        style = QStringLiteral("color:#eceff1;background:rgba(69,90,100,170);padding:6px 10px;border-radius:6px;font:600 15px 'DejaVu Sans';");
        break;
    case OverlaySeverity::Normal:
        break;
    }
    classificationLabel_->setStyleSheet(style);
    classificationLabel_->adjustSize();
    classificationLabel_->move(16, 16);
    classificationLabel_->show();
    classificationLabel_->raise();
}

void VideoPreviewWidget::clearClassificationOverlay() {
    classificationLabel_->clear();
    classificationLabel_->hide();
}

QString VideoPreviewWidget::classificationText() const {
    return classificationLabel_->text();
}
```

In `resizeEvent`, keep the label anchored near the top-left by repositioning it after resize if visible.

- [ ] **Step 4: Re-run the widget test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-ui --target video_preview_widget_test -j4
ctest --test-dir /tmp/rk_health_station-ui -R video_preview_widget_test --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_ui/widgets/video_preview_widget.h src/health_ui/widgets/video_preview_widget.cpp src/tests/CMakeLists.txt src/tests/ui_tests/video_preview_widget_test.cpp
git commit -m "feat: add video preview fall overlay rendering"
```

### Task 3: Connect `VideoMonitorPage` to fall classification and map `no person`

**Files:**
- Modify: `src/health_ui/pages/video_monitor_page.h`
- Modify: `src/health_ui/pages/video_monitor_page.cpp`
- Modify: `src/health_ui/app/ui_app.cpp`
- Modify: `src/tests/ui_tests/video_monitor_page_test.cpp`

- [ ] **Step 1: Write the failing page mapping test**

Extend `src/tests/ui_tests/video_monitor_page_test.cpp` with a fake fall client and a new test:

```cpp
#include "ipc_client/fall_ipc_client.h"
```

Use a small interface extraction in production first: define an abstract fall client in `video_monitor_page.h` if needed, then in the test add:

```cpp
class FakeFallClient : public QObject {
    Q_OBJECT
public:
    bool connectToBackend() { connectedCalled = true; return true; }
    bool connectedCalled = false;

signals:
    void classificationUpdated(const FallClassificationResult &result);
    void connectionChanged(bool connected);
    void errorOccurred(const QString &errorText);
};
```

Then add a new test:

```cpp
void VideoMonitorPageTest::showsFallOverlayTextOnRealtimeClassification() {
    FakeVideoIpcClient videoClient;
    FakeFallClient fallClient;
    VideoMonitorPage page(&videoClient, &fallClient);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    emit videoClient.statusReceived(status);

    FallClassificationResult result;
    result.cameraId = QStringLiteral("front_cam");
    result.state = QStringLiteral("fall");
    result.confidence = 0.93;
    emit fallClient.classificationUpdated(result);

    QCOMPARE(page.previewOverlayText(), QStringLiteral("fall 0.93"));
}
```

Also add a `no person` test by invoking a page-side timeout or explicit fallback method once implemented.

- [ ] **Step 2: Run the page test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-ui --target video_monitor_page_test -j4
ctest --test-dir /tmp/rk_health_station-ui -R video_monitor_page_test --output-on-failure
```

Expected: FAIL because `VideoMonitorPage` does not accept a fall client or expose overlay text.

- [ ] **Step 3: Add the minimal page integration**

Introduce a small abstract interface in `src/health_ui/pages/video_monitor_page.h`:

```cpp
class AbstractFallClient : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool connectToBackend() = 0;

signals:
    void classificationUpdated(const FallClassificationResult &result);
    void connectionChanged(bool connected);
    void errorOccurred(const QString &errorText);
};
```

Make `FallIpcClient` inherit from `AbstractFallClient`.

Update `VideoMonitorPage` constructor signature:

```cpp
explicit VideoMonitorPage(AbstractVideoClient *client, AbstractFallClient *fallClient, QWidget *parent = nullptr);
```

Add members:

```cpp
    AbstractFallClient *fallClient_ = nullptr;
    bool previewAvailable_ = false;
```

In `video_monitor_page.cpp`, on `onStatusReceived(...)`, set `previewAvailable_ = !status.previewUrl.isEmpty();` and call `fallClient_->connectToBackend();` once preview page becomes active.

Connect classification updates in the constructor:

```cpp
    connect(fallClient_, &AbstractFallClient::classificationUpdated, this,
        [this](const FallClassificationResult &result) {
            const QString text = QStringLiteral("%1 %2")
                .arg(result.state)
                .arg(QString::number(result.confidence, 'f', 2));

            VideoPreviewWidget::OverlaySeverity severity = VideoPreviewWidget::OverlaySeverity::Normal;
            if (result.state == QStringLiteral("fall")) {
                severity = VideoPreviewWidget::OverlaySeverity::Alert;
            } else if (result.state == QStringLiteral("lie")) {
                severity = VideoPreviewWidget::OverlaySeverity::Warning;
            }
            if (previewAvailable_) {
                previewWidget_->setClassificationOverlay(text, severity);
            }
        });
```

Add a small helper and test hook:

```cpp
QString VideoMonitorPage::previewOverlayText() const {
    return previewWidget_->classificationText();
}
```

For the first minimal `no person` mapping, add a page method used by the test and future timer integration:

```cpp
void VideoMonitorPage::showNoPersonOverlay() {
    if (previewAvailable_) {
        previewWidget_->setClassificationOverlay(QStringLiteral("no person"), VideoPreviewWidget::OverlaySeverity::Muted);
    }
}
```

- [ ] **Step 4: Re-run the page test to verify it passes**

Run:

```bash
cmake --build /tmp/rk_health_station-ui --target video_monitor_page_test -j4
ctest --test-dir /tmp/rk_health_station-ui -R video_monitor_page_test --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Wire the real `FallIpcClient` into `ui_app.cpp`**

In `src/health_ui/app/ui_app.cpp`, add:

```cpp
#include "ipc_client/fall_ipc_client.h"
```

Create the client near `videoClient_` and pass it into the page constructor:

```cpp
    , fallClient_(new FallIpcClient(QStringLiteral("rk_fall.sock"), this))
    , videoMonitorPage_(new VideoMonitorPage(videoClient_, fallClient_, stack_)) {
```

Also add `fallClient_` to `UiApp` members in `src/health_ui/app/ui_app.h`.

- [ ] **Step 6: Checkpoint**

```bash
git add src/health_ui/pages/video_monitor_page.h src/health_ui/pages/video_monitor_page.cpp src/health_ui/app/ui_app.h src/health_ui/app/ui_app.cpp src/tests/ui_tests/video_monitor_page_test.cpp
 git commit -m "feat: connect realtime fall overlay to video monitor page"
```

### Task 4: Add the `no person` fallback behavior and validate on RK3588

**Files:**
- Modify: `src/health_ui/pages/video_monitor_page.h`
- Modify: `src/health_ui/pages/video_monitor_page.cpp`
- Modify: `src/tests/ui_tests/video_monitor_page_test.cpp`
- No repo code changes expected for first board validation beyond UI files above

- [ ] **Step 1: Write the failing `no person` page test**

Add to `src/tests/ui_tests/video_monitor_page_test.cpp`:

```cpp
void VideoMonitorPageTest::showsNoPersonOverlayWhenRequested() {
    FakeVideoIpcClient videoClient;
    FakeFallClient fallClient;
    VideoMonitorPage page(&videoClient, &fallClient);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    emit videoClient.statusReceived(status);

    page.showNoPersonOverlay();
    QCOMPARE(page.previewOverlayText(), QStringLiteral("no person"));
}
```

- [ ] **Step 2: Run the page test to verify it fails if the hook is missing or hidden**

Run:

```bash
cmake --build /tmp/rk_health_station-ui --target video_monitor_page_test -j4
ctest --test-dir /tmp/rk_health_station-ui -R video_monitor_page_test --output-on-failure
```

Expected: FAIL before the public/test-visible hook exists.

- [ ] **Step 3: Add the minimal fallback timing behavior**

Use a simple timer-driven freshness rule in `video_monitor_page.cpp`:

```cpp
#include <QDateTime>
#include <QTimer>
```

Add members:

```cpp
    qint64 lastClassificationTs_ = 0;
    QTimer *noPersonTimer_ = nullptr;
```

Initialize in the constructor:

```cpp
    , noPersonTimer_(new QTimer(this)) {
    noPersonTimer_->setInterval(1500);
    connect(noPersonTimer_, &QTimer::timeout, this, [this]() {
        if (!previewAvailable_) {
            return;
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (lastClassificationTs_ <= 0 || (now - lastClassificationTs_) > 1500) {
            showNoPersonOverlay();
        }
    });
    noPersonTimer_->start();
```

When classification arrives, update:

```cpp
lastClassificationTs_ = QDateTime::currentMSecsSinceEpoch();
```

When preview becomes unavailable, clear the classification overlay.

- [ ] **Step 4: Re-run the UI tests and then build `health-ui`**

Run:

```bash
cmake --build /tmp/rk_health_station-ui --target fall_ipc_client_test video_preview_widget_test video_monitor_page_test health-ui -j4
ctest --test-dir /tmp/rk_health_station-ui -R 'fall_ipc_client_test|video_preview_widget_test|video_monitor_page_test' --output-on-failure
```

Expected: PASS and `health-ui` build succeeds.

- [ ] **Step 5: Rebuild bundle, deploy, and verify the overlay on RK3588**

Run:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station
bash deploy/scripts/build_rk3588_bundle.sh
rsync -av /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/ elf@192.168.137.179:~/rk3588_bundle/
ssh elf@192.168.137.179 'cd ~/rk3588_bundle && ./scripts/stop_all.sh >/dev/null 2>&1 || true && ./scripts/start_all.sh'
```

Then validate:

- open the video page in `health-ui`
- confirm preview still plays
- confirm overlay shows `state + confidence`
- confirm controlled validation can force `fall 0.xx`
- confirm no-human scene falls back to `no person`

If direct visual inspection is hard, also inspect the UI smoke logs and use any existing screenshot/offscreen hooks available locally.

- [ ] **Step 6: Checkpoint**

```bash
git add src/health_ui src/tests/ui_tests deploy
git commit -m "feat: show realtime fall classification on video preview"
```

