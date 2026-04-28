# In-Process GStreamer Analysis Pipeline Design

## Goal

Replace the analysis side of `health-videod`'s external `gst-launch + fdsink` flow with an optional in-process GStreamer appsink path, so the daemon can eventually receive `GstBuffer` metadata and DMABUF fds directly instead of parsing raw bytes from stdout.

## Scope

- Add an opt-in runtime path selected by `RK_VIDEO_PIPELINE_BACKEND=inproc_gst`.
- Keep the current `gst-launch` path as the default and as a fallback.
- Implement preview + analysis first. Recording and snapshots remain on the current `gst-launch` commands.
- The in-process path must publish preview MJPEG on the same TCP URL and publish analysis frames through the existing `AnalysisFrameSource` interface.
- Analysis transport remains compatible with current shared-memory and DMABUF descriptor paths.

## Architecture

Add a small focused class under `rk_app/src/health_videod/pipeline/` that owns a `GstElement *pipeline`, a `GstElement *analysisSink`, and a GLib main context/thread. It builds a launch string equivalent to the current preview command, but uses `appsink name=analysis_sink` instead of `fdsink fd=1` for the analysis branch. The existing `GstreamerVideoPipelineBackend` chooses this class only when both the runtime env var is enabled and the binary was compiled with GStreamer app support.

Host builds do not have GStreamer development packages, so the GStreamer class is behind `RKAPP_ENABLE_INPROCESS_GSTREAMER`. Cross builds enable that option because the RK3588 sysroot has `gstreamer-1.0`, `gstreamer-app-1.0`, and `gstreamer-video-1.0` pkg-config files.

## Data Flow

Initial in-process path:

```text
v4l2src / filesrc
  -> tee
  -> preview branch: mppjpegenc -> multipartmux -> tcpserversink
  -> analysis branch: appsink
  -> GstSample map to bytes
  -> existing RGA converter or CPU RGB handling
  -> existing DMABUF/shared-memory publish path
```

This first implementation removes the external `gst-launch` process and stdout/fdsink boundary. If the `GstBuffer` exposes DMABUF memory, a later narrow patch can publish that fd directly without changing `health-falld`.

## Error Handling

- If in-process GStreamer is requested but the binary is not compiled with support, `startPreview()` returns a clear error and the caller can switch env/config back to default.
- If pipeline construction, state transition, appsink lookup, or bus error occurs, the backend reports `preview_pipeline_failed` through the existing observer path.
- `stopPreview()` tears down the Gst pipeline and joins its loop thread.

## Testing

- Host unit tests cover backend selection and unsupported-build error behavior without linking GStreamer.
- Existing pipeline command tests remain valid for the default `gst-launch` path.
- RK3588 cross-build verifies GStreamer symbols link.
- Board smoke test starts with `RK_VIDEO_PIPELINE_BACKEND=inproc_gst RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf` and checks process status, video/fall perf, transport markers, and absence of crash/fatal logs.
