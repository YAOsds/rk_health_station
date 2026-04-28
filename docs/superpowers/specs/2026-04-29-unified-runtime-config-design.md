# Unified Runtime Configuration Design

## Goal

Replace the RK3588 bundle's user-facing runtime environment variables and scattered hard-coded runtime paths with a single JSON configuration file plus a dedicated Qt configuration application, while preserving environment-variable overrides for development, testing, and board-side experiments.

## Scope

- Add one shared runtime configuration model used by `healthd`, `health-videod`, `health-falld`, `health-ui`, and a new `health-config-ui`.
- Move user-facing runtime settings out of per-module `qEnvironmentVariable(...)` reads and into a single JSON file.
- Cover all currently user-tuned runtime fields, including normal runtime settings, advanced performance knobs, and debug or probe output paths.
- Keep environment-variable override support for compatibility, but centralize it in one configuration layer.
- Add a dedicated standalone Qt application for editing, validating, and saving the JSON configuration file.
- Update bundle startup scripts and bundle packaging so the normal user path becomes "edit JSON or launch config UI, then run `start.sh`".

## Non-Goals

- Do not add hot-reload of runtime configuration while services are already running.
- Do not add automatic service restart orchestration after config changes in this first version.
- Do not expose every internal implementation constant as a user-facing setting; protocol literals, fixed internal timeouts, and compile-time feature flags remain code-owned.
- Do not merge configuration editing into the existing `health-ui`; configuration lives in a separate executable.

## Current Problems

The current project mixes three configuration mechanisms in ways that are hard for users to understand:

1. User-facing runtime behavior depends on many environment variables such as `RK_VIDEO_PIPELINE_BACKEND`, `RK_VIDEO_ANALYSIS_TRANSPORT`, `RK_VIDEO_RGA_OUTPUT_DMABUF`, `RK_FALL_RKNN_INPUT_DMABUF`, and multiple socket/model/debug paths.
2. Several runtime defaults are hard-coded inside service code, including camera id, video device path, storage directory, database path, socket names, and model paths.
3. Bundle startup scripts export a large set of values before launching services, which forces users to memorize shell commands instead of editing one discoverable configuration file.

This makes normal deployment unfriendly, makes configuration provenance unclear, and blocks a future user-facing editor because every service currently owns part of its own configuration loading logic.

## Recommended Architecture

Introduce a single shared `AppRuntimeConfig` model under `rk_app/src/shared/runtime_config/`. All runtime configuration loading, path expansion, validation, defaults, and environment-variable overrides happen there. Service code receives typed config objects and stops reading environment variables directly.

The bundle ships one runtime file at `config/runtime_config.json`. A new standalone executable, `health-config-ui`, edits that file offline and shows field-level validation errors. Existing services read the same file at startup. Environment variables remain supported, but only as a final override layer for advanced development and board-side experiments.

## Configuration Source Priority

Runtime values resolve in this order:

1. Explicit environment-variable override
2. `config/runtime_config.json`
3. Built-in default value

This preserves compatibility with existing debug and benchmarking workflows while making the JSON file the primary user-facing configuration source.

## Runtime File Location

- Source template in the repo: `deploy/config/runtime_config.json`
- Bundle runtime file on target: `rk3588_bundle/config/runtime_config.json`
- Optional explicit override path: `RK_APP_CONFIG_PATH=/path/to/runtime_config.json`

Bundle scripts should default to the bundled `config/runtime_config.json`. Users should not need to export individual runtime env vars for normal startup.

## JSON Structure

The configuration file should be organized by user-understandable functional areas instead of by daemon name. The top-level shape is:

```json
{
  "system": {},
  "paths": {},
  "ipc": {},
  "video": {},
  "analysis": {},
  "fall_detection": {},
  "debug": {}
}
```

Recommended representative structure:

```json
{
  "system": {
    "runtime_mode": "auto"
  },
  "paths": {
    "storage_dir": "/home/elf/videosurv/",
    "database_path": "./data/healthd.sqlite"
  },
  "ipc": {
    "health_socket": "./run/rk_health_station.sock",
    "video_socket": "./run/rk_video.sock",
    "analysis_socket": "./run/rk_video_analysis.sock",
    "fall_socket": "./run/rk_fall.sock"
  },
  "video": {
    "camera_id": "front_cam",
    "device_path": "/dev/video11",
    "pipeline_backend": "inproc_gst",
    "analysis_enabled": true,
    "analysis_convert_backend": "rga",
    "gst_launch_bin": "gst-launch-1.0"
  },
  "analysis": {
    "transport": "dmabuf",
    "dma_heap": "/dev/dma_heap/system-uncached-dma32",
    "rga_output_dmabuf": true,
    "gst_dmabuf_input": true,
    "gst_force_dmabuf_io": false
  },
  "fall_detection": {
    "enabled": true,
    "pose_model_path": "./assets/models/yolov8n-pose.rknn",
    "action_backend": "lstm_rknn",
    "lstm_model_path": "./assets/models/lstm_fall.rknn",
    "stgcn_model_path": "./assets/models/stgcn_fall.rknn",
    "rknn_input_dmabuf": true,
    "rknn_io_mem_mode": "default"
  },
  "debug": {
    "video_latency_marker_path": "",
    "fall_latency_marker_path": "",
    "fall_pose_timing_path": "",
    "fall_track_trace_path": "",
    "fall_action_debug": false,
    "fall_lstm_trace_path": ""
  }
}
```

## Field Coverage

The first version should cover all current user-tuned runtime knobs that are now spread across code and shell setup:

- Core runtime selection, bundle paths, and storage/database locations
- IPC socket paths and shared-memory naming overrides
- Camera identity and device path
- Video backend selection and analysis enablement
- GStreamer launch binary override for development
- Analysis transport choice and DMA heap path
- RGA, GstBuffer DMABUF, forced DMABUF I/O, and RKNN DMABUF/I/O-memory knobs
- Fall-detection models, action backend selection, tracker tuning, and sequence length
- Debug, marker, trace, and probe output paths

Pure code-owned constants such as protocol field names, fixed observer callbacks, and compile-time feature availability remain out of the JSON file.

## Shared Runtime Configuration Module

Create a new shared module at `rk_app/src/shared/runtime_config/` with these responsibilities:

- `app_runtime_config.h/.cpp`
  - Define the full typed configuration model.
- `app_runtime_config_loader.h/.cpp`
  - Load JSON, merge defaults, and apply environment-variable overrides.
- `app_runtime_config_validator.h/.cpp`
  - Validate enum values, numeric ranges, required paths, and user-facing consistency rules.
- `app_runtime_config_paths.h/.cpp`
  - Resolve relative paths against bundle root or config-file directory and normalize output paths.

The shared module should expose:

- Full application config: `AppRuntimeConfig`
- Service-specific views or mapped structs:
  - `HealthdRuntimeConfig`
  - `VideoRuntimeConfig`
  - `FallRuntimeConfig`
  - `UiRuntimeConfig`
- A validation result object that contains:
  - blocking errors
  - warnings
  - normalized effective values
  - per-field origin metadata such as `default`, `config`, or `environment`

## Service Integration Strategy

### `healthd`

Replace direct environment reads for database path and related runtime marker settings with values derived from shared configuration. `healthd` should receive typed config on startup and log the effective database path, socket path, and marker status with source information.

### `health-videod`

Centralize current runtime selection now spread across:

- `rk_app/src/health_videod/core/video_service.cpp`
- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.cpp`
- `rk_app/src/health_videod/analysis/gstreamer_analysis_output_backend.cpp`
- `rk_app/src/health_videod/analysis/rga_frame_converter.cpp`
- `rk_app/src/health_videod/ipc/video_gateway.cpp`

These files should stop reading environment variables directly and instead consume one injected `VideoRuntimeConfig`.

### `health-falld`

Keep the existing `FallRuntimeConfig` concept, but make it a mapped view from shared configuration instead of a local environment parser. This preserves current code shape while removing duplicate config-loading logic.

### `health-ui`

Limit `health-ui` to only the small runtime settings it actually needs, such as IPC connection targets. It should not become a configuration editor or own business-runtime defaults.

## Standalone Configuration Application

Add a new Qt executable:

- path: `rk_app/src/health_config_ui/`
- binary: `health-config-ui`

This application is a dedicated offline configuration editor and is not linked into the existing `health-ui` navigation.

### UI Requirements

- Open and edit the bundle JSON config without requiring `healthd`, `health-videod`, or `health-falld` to be running
- Show the current config file path prominently
- Track dirty state, validation state, and last save result
- Provide actions for:
  - reload
  - validate
  - save
  - restore defaults
- Organize fields into user-facing groups:
  - Basic
  - Paths
  - Video
  - Analysis
  - Fall Detection
  - Debug and Diagnostics
- Use user-friendly labels instead of environment-variable names
- Show field help text, default value hints, and warning badges for experimental items such as forced DMABUF I/O

### Validation Behavior

- Blocking errors must prevent save
- Non-blocking warnings should still allow save
- Errors and warnings should identify exact field paths such as `analysis.transport` or `video.device_path`
- Typical messages should be concrete and user-readable, for example:
  - `video.device_path does not exist`
  - `analysis.transport must be shared_memory or dmabuf`
  - `fall_detection.pose_model_path is empty`

## Startup Script Changes

Update bundle runtime scripts so users no longer need to export large lists of runtime variables.

### Bundle contents

The bundle should include:

- `config/runtime_config.json`
- `scripts/start.sh`
- `scripts/start_all.sh`
- `scripts/stop.sh`
- `scripts/status.sh`
- `scripts/config.sh`

### `start.sh`

`start.sh` should:

- locate bundle root
- locate the default config file
- accept an optional `--config /path/to/runtime_config.json`
- export at most `RK_APP_CONFIG_PATH`
- stop exporting large sets of individual runtime settings for normal operation
- print the effective config file path in startup output

### `config.sh`

Add a dedicated script that launches `bin/health-config-ui` against the bundle's active config file so the user mental model becomes:

- configure: `./scripts/config.sh`
- run backend or full bundle: `./scripts/start.sh` or `./scripts/start_all.sh`
- stop: `./scripts/stop.sh`

## Compatibility Strategy

Preserve environment-variable compatibility during the migration so existing board-side workflows continue to work:

- benchmarking scripts can still force `RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf`
- probe sessions can still force `RK_VIDEO_GST_FORCE_DMABUF_IO=1`
- tests can still use `qputenv(...)`

The key change is that service code no longer owns environment parsing; only the shared config layer does.

## Logging and Error Reporting

At startup, each service should log effective configuration with provenance. Example style:

- `video.pipeline_backend=inproc_gst source=config`
- `analysis.transport=dmabuf source=environment`
- `paths.database_path=/home/elf/rk3588_bundle/data/healthd.sqlite source=default`

If config loading fails:

- startup should stop early with a field-specific validation error
- `start.sh` should print a short human-readable failure summary
- `health-config-ui` should surface the same field-specific errors in the UI

## Migration Plan

Implement the change in four stages:

1. Add the shared runtime configuration module plus default `deploy/config/runtime_config.json`.
2. Convert `healthd`, `health-videod`, `health-falld`, and `health-ui` to consume typed config from the shared module while keeping env overrides.
3. Add standalone `health-config-ui` and bundle script integration.
4. Update README, bundle README, deployment docs, and runtime examples so the primary user workflow is JSON or config UI based instead of env export based.

## Testing and Verification

### Host verification

- Unit-test JSON parsing, defaults, path expansion, enum validation, and environment override precedence.
- Add focused tests for each service to confirm it receives config from the shared layer rather than direct env parsing.
- Add UI tests for `health-config-ui` covering load, edit, validate, save, and restore-default behavior.

### Bundle and cross-build verification

- Host build with `BUILD_TESTING=ON` must continue to pass.
- RK3588 bundle cross-build must package:
  - `config/runtime_config.json`
  - `bin/health-config-ui`
  - `scripts/config.sh`

### Board verification

- Verify bundle startup without manually exporting the current runtime variables.
- Verify full JSON-based startup for both a normal compatibility configuration and a zero-copy-oriented configuration.
- Verify env override precedence still works for board-side experiments.
- Verify `health-config-ui` can edit the bundle config on board and that saved changes affect the next startup.

## Success Criteria

The work is successful when:

- a normal user can configure the bundle without memorizing environment variables
- a single JSON file fully expresses the project's current runtime configuration surface
- a standalone Qt configuration application can edit and validate that file
- existing advanced env-based debugging and benchmarking workflows still work
- the board can still run both stable compatibility mode and advanced DMABUF mode through the new configuration system
