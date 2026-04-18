# UI Warm-Up and ByteTrack First-Frame Bugfixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate two user-visible correctness bugs: the UI must not show `no person` while a person is still warming up for 45-frame classification, and a newly created ByteTrack track must remain `Tracked` on its creation frame.

**Architecture:** Keep the fix set minimal and low-coupling. For the UI issue, do not change the `health-videod -> analysis socket -> health-falld` contract; instead, refine `health_ui` semantics so `no person` is shown only when the backend explicitly emits an empty batch, while timeout-based staleness clears the overlay back to an unknown/blank state. For the ByteTrack issue, patch only the local track lifecycle in `health_falld` so newly spawned tracks are treated as matched during their creation frame.

**Tech Stack:** Qt 5.15 / C++, QLocalSocket IPC, RK3588 backend services, existing QtTest-based unit and end-to-end tests.

---

## File Map

- Modify: `rk_app/src/health_ui/pages/video_monitor_page.cpp`
  - Refine timeout behavior so stale data clears overlay instead of falsely asserting `no person`.
  - Keep empty-batch handling as the only source of truth for the `no person` label.
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.h`
  - Rename or clarify timeout semantics to reflect stale/unknown overlay state rather than `no person` assertion.
- Modify: `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`
  - Replace the current stale-timeout expectation with a regression test proving warm-up does not show `no person`.
  - Keep the explicit empty-batch test proving `no person` still appears when backend reports zero people.
- Modify: `rk_app/src/health_falld/tracking/byte_tracker.cpp`
  - Mark newly created tracks as matched on their creation frame so they are not immediately converted to `Lost`.
- Modify: `rk_app/src/tests/fall_daemon_tests/byte_tracker_test.cpp`
  - Add a regression test asserting a new track remains `Tracked`, with zero misses, immediately after first detection.
- Verification only: `/tmp/rk_health_station-bytetrack-build`
  - Rebuild only the affected tests first, then run the broader fall/UI regression suite.

---

### Task 1: Fix UI Warm-Up vs `no person` Semantics

**Files:**
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.cpp`
- Modify: `rk_app/src/health_ui/pages/video_monitor_page.h`
- Test: `rk_app/src/tests/ui_tests/video_monitor_page_test.cpp`

- [ ] **Step 1: Write the failing warm-up regression test**

Add a test that proves preview + fall connection + no classification message must not become `no person` just because the stale timer fires.

```cpp
void VideoMonitorPageTest::doesNotShowNoPersonWhileClassificationIsStillWarmingUp() {
    FakeVideoIpcClient client;
    FakeFallIpcClient fallClient;
    VideoMonitorPage page(&client, &fallClient);

    VideoChannelStatus status;
    status.cameraId = QStringLiteral("front_cam");
    status.cameraState = VideoCameraState::Previewing;
    status.previewUrl = QStringLiteral("tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview");
    status.previewProfile.width = 640;
    status.previewProfile.height = 480;
    emit client.statusReceived(status);
    emit fallClient.connectionChanged(true);

    QTest::qWait(1700);
    QCOMPARE(page.previewOverlayRows(), QStringList());
}
```

- [ ] **Step 2: Run the test to verify it fails for the right reason**

Run:

```bash
cmake --build /tmp/rk_health_station-bytetrack-build --target video_monitor_page_test -j4
ctest --test-dir /tmp/rk_health_station-bytetrack-build -R video_monitor_page_test --output-on-failure
```

Expected: FAIL because the current timeout path sets the overlay to `no person` after `kNoPersonOverlayTimeoutMs`.

- [ ] **Step 3: Implement the minimal UI behavior change**

Update `VideoMonitorPage` so timeout means “classification is stale / unknown” rather than “nobody is present”. The minimal patch is:

```cpp
void VideoMonitorPage::onNoPersonTimeout() {
    hasFreshClassification_ = false;
    if (!previewAvailable_ || !fallConnected_) {
        return;
    }

    previewWidget_->clearClassificationOverlay();
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
    // keep the existing per-person rows
}
```

If desired for readability, rename the timeout constant in `video_monitor_page.h` from `kNoPersonOverlayTimeoutMs` to something like `kClassificationStaleTimeoutMs`, but do not change public behavior beyond the stale/unknown semantics.

- [ ] **Step 4: Expand UI tests around the new semantics**

Keep the existing explicit empty-batch regression and ensure these expectations hold together:

```cpp
void VideoMonitorPageTest::showsNoPersonOverlayForEmptyBatch();
void VideoMonitorPageTest::showsMultiPersonClassificationOverlay();
void VideoMonitorPageTest::doesNotShowNoPersonWhileClassificationIsStillWarmingUp();
```

The key contract after the fix:
- empty batch => `no person`
- real classified batch => rows like `stand 0.91`, `fall 0.96`
- stale timeout without any batch => empty overlay, not `no person`

- [ ] **Step 5: Run the focused UI regression suite**

Run:

```bash
cmake --build /tmp/rk_health_station-bytetrack-build --target video_monitor_page_test video_preview_widget_test fall_ipc_client_test -j4
ctest --test-dir /tmp/rk_health_station-bytetrack-build -R 'video_monitor_page_test|video_preview_widget_test|fall_ipc_client_test' --output-on-failure
```

Expected: all three tests PASS.

- [ ] **Step 6: Commit the UI fix**

```bash
git add \
  rk_app/src/health_ui/pages/video_monitor_page.cpp \
  rk_app/src/health_ui/pages/video_monitor_page.h \
  rk_app/src/tests/ui_tests/video_monitor_page_test.cpp

git commit -m "fix: avoid no-person overlay during classification warm-up"
```

---

### Task 2: Keep New ByteTrack Tracks `Tracked` on Their Creation Frame

**Files:**
- Modify: `rk_app/src/health_falld/tracking/byte_tracker.cpp`
- Test: `rk_app/src/tests/fall_daemon_tests/byte_tracker_test.cpp`

- [ ] **Step 1: Write the failing tracker regression test**

Add a test that checks the very first `update()` result for a new person.

```cpp
void ByteTrackerTest::keepsNewTrackTrackedOnCreationFrame() {
    ByteTracker tracker(makeConfig());

    const auto tracks = tracker.update({makePerson(10, 10, 40, 80, 0.95f)}, 1000);
    QCOMPARE(tracks.size(), 1);
    QCOMPARE(tracks.first().state, ByteTrackState::Tracked);
    QCOMPARE(tracks.first().missCount, 0);
    QCOMPARE(tracks.first().lostSinceTs, 0);
}
```

- [ ] **Step 2: Run the test to verify it fails for the current bug**

Run:

```bash
cmake --build /tmp/rk_health_station-bytetrack-build --target byte_tracker_test -j4
ctest --test-dir /tmp/rk_health_station-bytetrack-build -R byte_tracker_test --output-on-failure
```

Expected: FAIL because a newly created track gets swept by the unmatched-track pass and becomes `Lost` with `missCount == 1`.

- [ ] **Step 3: Implement the minimal tracker fix**

Treat newly created tracks as matched during their creation frame.

```cpp
tracks_.push_back(track);
const int newTrackIndex = tracks_.size() - 1;
matchedTrackIndexes.insert(newTrackIndex);
matchedDetectionIndexes.insert(detectionIndex);
```

Place this immediately after a new `TrackedPerson` is appended in the high-score new-track creation block.

Do not refactor the full tracker lifecycle in this task; keep the fix local and easy to reason about.

- [ ] **Step 4: Run tracker-focused regression tests**

Run:

```bash
cmake --build /tmp/rk_health_station-bytetrack-build --target byte_tracker_test association_test track_action_context_test -j4
ctest --test-dir /tmp/rk_health_station-bytetrack-build -R 'byte_tracker_test|association_test|track_action_context_test' --output-on-failure
```

Expected: PASS, including the new creation-frame regression test.

- [ ] **Step 5: Commit the tracker fix**

```bash
git add \
  rk_app/src/health_falld/tracking/byte_tracker.cpp \
  rk_app/src/tests/fall_daemon_tests/byte_tracker_test.cpp

git commit -m "fix: keep new ByteTrack tracks tracked on first frame"
```

---

### Task 3: Run Cross-Cutting Regression Verification

**Files:**
- No new code files
- Verification target set only

- [ ] **Step 1: Rebuild the affected backend/UI tests**

Run:

```bash
cmake --build /tmp/rk_health_station-bytetrack-build \
  --target \
    action_classifier_factory_test \
    association_test \
    byte_tracker_test \
    track_action_context_test \
    fall_gateway_test \
    fall_event_policy_test \
    fall_detector_service_test \
    fall_end_to_end_status_test \
    video_monitor_page_test \
    video_preview_widget_test \
    fall_ipc_client_test \
  -j4
```

Expected: build completes with exit code 0.

- [ ] **Step 2: Run the full relevant regression suite**

Run:

```bash
ctest --test-dir /tmp/rk_health_station-bytetrack-build \
  -R 'action_classifier_factory_test|association_test|byte_tracker_test|track_action_context_test|fall_gateway_test|fall_event_policy_test|fall_detector_service_test|fall_end_to_end_status_test|video_monitor_page_test|video_preview_widget_test|fall_ipc_client_test' \
  --output-on-failure
```

Expected: PASS with 0 failed tests.

- [ ] **Step 3: Rebuild RK3588 deployable binaries**

Run:

```bash
cmake --build /tmp/rk_health_station-bytetrack-build --target health-falld health-videod health-ui -j4
```

Expected: all three deployable targets PASS.

- [ ] **Step 4: Commit the verification-complete state**

If Task 1 and Task 2 were committed separately, do not add another code commit here unless verification required source changes. If verification revealed no source changes, record the results in the final handoff only.

---

## Self-Review

- **Spec coverage:**
  - UI warm-up false `no person` state: covered in Task 1
  - ByteTrack first-frame `Lost` state: covered in Task 2
  - Regression verification across UI/backend: covered in Task 3
- **Placeholder scan:** no `TODO`/`TBD` placeholders left in the steps.
- **Type consistency:** all method names and test targets match the current codebase (`VideoMonitorPage`, `ByteTracker`, `byte_tracker_test`, `video_monitor_page_test`).
