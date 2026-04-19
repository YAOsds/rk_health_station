# Test Video Input Mode Design

**Date:** 2026-04-19
**Branch:** `feature/test-video-input-mode`
**Base:** `feature/bytetrack-fall-detection`

## Goal

Add a user-facing "test fall detection" mode that temporarily replaces the live camera input with a user-selected local MP4 file, while keeping the existing RK3588 fall-detection pipeline intact. In test mode, the preview area must show the test video, fall detection must run against the same frames, and the UI must clearly indicate that the system is no longer showing the real camera.

## Scope

This design covers:

- entering test mode from the existing Qt video monitor page
- selecting a local MP4 file from the UI
- switching `health-videod` from camera input to file input
- keeping preview and analysis driven by the same selected input source
- exposing test-mode state to the UI through the existing video IPC layer
- disabling snapshot and recording while test mode is active
- remaining in test mode after playback finishes until the user manually exits or selects another file

This design does not cover:

- autoplay looping
- playlist management
- remote file selection
- new person-tracking logic
- UI redesign outside the video monitor page

## Requirements

### Functional Requirements

1. The video monitor page must expose two explicit controls:
   - `Select Test Video`
   - `Exit Test Mode`
2. Clicking `Select Test Video` must open a file chooser restricted to local video files, with MP4 as the primary supported format.
3. Once a valid file is selected, `health-videod` must switch the active input source for the camera channel from the physical camera to the selected file.
4. The preview shown in the existing preview widget must come from the selected file while test mode is active.
5. The analysis stream consumed by `health-falld` must be driven by the same selected file input, not by the physical camera.
6. The UI must clearly show that the channel is in test mode and display the selected file name.
7. `Take Snapshot`, `Start Recording`, and `Stop Recording` must be disabled in test mode.
8. When playback reaches EOF, the system must remain in test mode.
9. After EOF, the user must be able to:
   - choose a new file and continue testing
   - manually exit test mode and return to camera input
10. Exiting test mode must restore the original camera preview and analysis flow.

### Non-Functional Requirements

1. The existing camera path must remain the default and must not be deeply coupled to test-mode logic.
2. `health-falld` must remain source-agnostic and continue consuming only analysis frames.
3. The change must fit the current project complexity and avoid introducing a second video daemon.
4. Failures during test-mode entry or exit must leave the system in a well-defined state.

## Recommended Approach

Use a single-source-switch design inside `health-videod`.

- `health-ui` only issues commands and renders state.
- `health-videod` becomes the sole owner of the channel input mode (`camera` vs `test_file`).
- `health-falld` remains unchanged and continues reading from the analysis socket.
- Shared video IPC and status models are extended so the UI can render test-mode state without local inference.

This is preferred because it preserves the existing architecture boundary: the video daemon owns input selection, the fall daemon owns fall inference, and the UI remains a client of backend state.

## Architecture

### 1. UI Layer (`health-ui`)

`VideoMonitorPage` remains the entry point for video interactions.

New responsibilities:

- provide test-mode controls
- open a file chooser for MP4 selection
- request backend mode switches through video IPC
- render backend-driven test-mode status
- disable controls that are not valid in test mode

The UI must not locally simulate playback state and must not play the MP4 independently. It continues to trust `VideoChannelStatus.previewUrl` for preview content.

### 2. Video Control Layer (`shared/protocol` + `health_videod/ipc`)

The video IPC command set is extended with two new actions:

- `start_test_input`
- `stop_test_input`

`start_test_input` carries a payload containing at least:

- `file_path`

`VideoChannelStatus` is extended with backend-owned source metadata:

- `inputMode` with values `camera` or `test_file`
- `testFilePath`
- `testPlaybackState` with values `idle`, `playing`, `finished`, or `error`

These fields are returned on normal status responses, so the UI can update itself only from backend state.

### 3. Video Service Layer (`health-videod/core`)

`VideoService` becomes responsible for channel-level source switching.

It must:

- store the current source mode for each channel
- validate a selected test file before switching
- stop current preview/analysis pipelines before switching source
- start preview/analysis pipelines with the new source
- restore the camera source when leaving test mode
- protect invalid operations during test mode, especially snapshot and recording
- preserve a consistent `VideoChannelStatus` after command success or failure

The service must keep camera mode and test mode within the same channel model rather than inventing a parallel test channel.

### 4. Pipeline Layer (`health-videod/pipeline` and `health-videod/analysis`)

The pipeline backends currently assume a V4L2 camera source. They must be generalized to build pipelines from a source description.

Two concrete source kinds are needed initially:

- physical camera source
- local test-file source

The preview backend and analysis backend both consume the same source configuration for a channel. That keeps preview and fall analysis aligned on the same underlying input.

For test files, preview is produced by a GStreamer pipeline based on file demux/decode instead of `v4l2src`.

For example, preview moves conceptually from:

- camera: `v4l2src ... -> jpegenc -> tcpserversink`

To additionally supporting:

- test file: `filesrc ... -> demux/decode -> videoconvert/videoscale -> jpegenc -> tcpserversink`

The analysis backend must follow the same source mode so `health-falld` receives frames generated from the test file when test mode is active.

### 5. Fall Detection Layer (`health-falld`)

No architectural change is required.

`health-falld` must remain unaware of whether frames originated from the camera or a test MP4. Its contract stays unchanged: consume analysis packets, infer pose, run ByteTrack, classify fall state, and publish results.

This separation is intentional and should be preserved.

## Data Flow

### Enter Test Mode

1. User clicks `Select Test Video`.
2. UI opens a local file chooser.
3. UI sends `start_test_input` with `file_path`.
4. `VideoGateway` routes the command to `VideoService`.
5. `VideoService` validates the file.
6. `VideoService` stops active preview/analysis pipelines for the channel.
7. `VideoService` updates the channel source mode to `test_file`.
8. Preview and analysis backends restart using the selected MP4 as the source.
9. `VideoService` returns updated status.
10. UI refreshes the page from returned status and/or a follow-up `get_status`.

### Exit Test Mode

1. User clicks `Exit Test Mode`.
2. UI sends `stop_test_input`.
3. `VideoService` stops test-file pipelines.
4. `VideoService` restores the camera source configuration.
5. Preview and analysis pipelines restart against the camera.
6. Backend returns updated status.
7. UI re-enables camera-only controls.

### End of File

1. Test video reaches playback end.
2. Backend remains in `inputMode = test_file`.
3. Backend updates `testPlaybackState = finished`.
4. UI continues showing test mode and file name.
5. User may either choose another file or exit test mode.

## UI Design

The current video monitor page is extended rather than redesigned.

### New Controls

Add a dedicated test section in the existing controls area:

- `Select Test Video`
- `Exit Test Mode`

Behavior:

- `Select Test Video` is always enabled when the page has a valid channel.
- `Exit Test Mode` is enabled only when `inputMode == test_file`.

### New Status Rows

Add two rows to the status panel:

- `Input Mode`: `Camera` or `Test Video`
- `Test File`: selected file name or `--`

Optionally, if the status text budget allows, `Input Mode` can include playback state such as:

- `Test Video (Playing)`
- `Test Video (Finished)`

### Preview Overlay / Badge

Add a light source badge distinct from the fall-classification overlay.

Recommended content:

- badge line 1: `TEST MODE`
- badge line 2: `<filename>.mp4`

This badge must remain visually separate from the multi-person state overlay (`stand`, `fall`, `lie`, `no person`) so the two pieces of information do not overwrite each other.

### Disabled Controls in Test Mode

When `inputMode == test_file`:

- disable `Take Snapshot`
- disable `Start Recording`
- disable `Stop Recording`

This keeps test-mode semantics clear and avoids mixing camera capture features with file playback.

## Backend State Model

### `VideoChannelStatus` Additions

Add the following fields:

- `inputMode: QString`
- `testFilePath: QString`
- `testPlaybackState: QString`

Expected meanings:

- `inputMode`
  - `camera`
  - `test_file`
- `testPlaybackState`
  - `idle`: no test file active
  - `playing`: test file is actively producing frames
  - `finished`: playback ended normally, mode retained
  - `error`: backend failed while driving test input

The UI should derive presentation only from these fields, not from command history.

## Error Handling

### Start Test Mode Failures

If the selected file is invalid or the test pipeline fails to start:

- backend returns an error result
- channel remains in prior mode
- previous preview/analysis source remains active
- UI shows backend error in the existing error display

Suggested error codes:

- `test_file_not_found`
- `test_file_invalid`
- `test_input_start_failed`

### Replacing an Existing Test File

If the user is already in test mode and selects a new file:

- backend validates the new file first
- only after successful validation and pipeline startup should it replace the current test file state
- on failure, existing test mode remains active

### Exit Test Mode Failures

If camera restore fails:

- backend stays in the last known mode
- backend reports an explicit error
- UI remains backend-driven

Suggested error code:

- `test_input_stop_failed`

## Testing Strategy

### Protocol Tests

Add coverage for:

- new IPC command actions
- new `VideoChannelStatus` fields round-trip serialization

### Video Service Tests

Add coverage for:

- entering test mode from camera mode
- rejecting invalid file path without corrupting current state
- staying in test mode after playback finishes
- exiting test mode and restoring camera mode
- disabling snapshot/record commands in test mode
- replacing a current test file with a new one

### UI Tests

Add coverage for:

- rendering `Input Mode` and `Test File`
- correct enable/disable behavior for new and existing buttons
- file-name badge / visible test-mode indicator
- preserving classification overlay behavior while in test mode

### Integration Direction

At minimum, host-side tests should prove the backend state transitions. Board-side verification can later confirm that a real MP4 drives both preview and fall detection on RK3588.

## Implementation Notes

To keep coupling low, avoid making `health-falld` depend on video-mode details. Any code that needs to know about test mode should remain within:

- shared video models/protocol
- `health-ui`
- `health-videod`

Do not implement a UI-only MP4 preview shortcut. That would split preview and fall inference into separate sources and break the expected semantics of this feature.

## Open Decisions Already Resolved

The following were confirmed during design discussion:

- after EOF, remain in test mode
- snapshot/recording features are disabled in test mode
- test mode uses two buttons, not a single toggle
- UI must clearly display test mode and file name
- the recommended approach is backend source switching, not UI-only playback
