# RK Health Station

本工程是 `esp_health_watch` 的 RK3588 + ESP32 Qt 主站重构版本。

- rk_app/: RK3588 Qt/C++ 主站工程
- esp_fw/: ESP32 固件工程
- protocol/: 对外与本地 IPC 协议文档
- deploy/: systemd 与 bundle 部署脚本
- cmake/toolchains/: RK3588 交叉编译工具链文件

当前项目的主部署方式已经统一为：

- 宿主机 `Ubuntu 22.04` 上交叉编译
- 目标架构 `ARM64 / aarch64`
- 生成可直接传输到 RK3588 的 `rk3588_bundle/`
- 通过 `scp` / `rsync` / `ssh` 将 bundle 传到板端
- 在板端进入 bundle 目录后先编辑 `config/runtime_config.json`，或运行 `./scripts/config.sh`
- 然后使用 `./scripts/start.sh` 或 `./scripts/start_all.sh`

当前仓库默认对接的交叉 SDK 路径为：

- `/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot`

一键交叉编译并打包：

```bash
bash rk_health_station/deploy/scripts/build_rk3588_bundle.sh
```

只编译 `rk_app`：

```bash
# 当前 Ubuntu 主机本地编译
bash rk_health_station/deploy/scripts/build_rk_app.sh host

# RK3588 交叉编译
bash rk_health_station/deploy/scripts/build_rk_app.sh rk3588
```

默认输出目录：

- `rk_health_station/out/rk3588_bundle`

交叉编译完成后，必须再用 `file` 校验产物架构，例如：

```bash
file rk_health_station/out/rk3588_bundle/bin/healthd
file rk_health_station/out/rk3588_bundle/bin/health-ui
```

期望输出包含 `ARM aarch64`。

详细步骤见：

- `rk_health_station/docs/deployment/rk3588-backend-usage-guide.md`
- `rk_health_station/docs/deployment/rk3588-install.md`
- `rk_health_station/docs/architecture/camera-video-readme.md`

## RK3588 RGA 分析链路环境检查

RK3588 bundle 构建默认启用分析链路的 RGA 转换：

```text
camera/test input NV12 -> health-videod RGA -> RGB 640x640 -> shared memory -> health-falld
```

这个路径只影响跌倒检测所需的分析帧。UI 预览 JPEG 仍走 GStreamer 预览分支，预览编码使用 `mppjpegenc`。

板端运行前可以用下面命令确认环境是否具备 RGA 条件：

```bash
# 1. 确认 RGA 设备节点存在
ls -l /dev/rga

# 2. 确认当前用户具备访问权限；通常需要在 video 组
id

# 3. 确认系统可找到 librga
pkg-config --modversion librga
ldconfig -p | grep -E 'librga\.so'

# 4. 确认 health-videod 产物确实链接到 librga
ldd /home/elf/rk3588_bundle/bin/health-videod | grep rga
```

期望现象：

- `/dev/rga` 存在，且当前用户在 `video` 组或具备等效权限。
- `pkg-config --modversion librga` 能输出版本号。
- `ldd .../health-videod` 能看到 `librga.so`。

启动后可以继续确认当前分析后端：

```bash
cd /home/elf/rk3588_bundle
./scripts/start_all.sh

# 日志中应能看到 analysis_backend=rga
grep 'preview_started' logs/health-videod.log | tail -5

# 当前 GStreamer 分析分支应输出 NV12 到 fdsink，而不是 RGB videoconvert/videoscale
ps -ef | grep '[g]st-launch'
```

RGA 模式下的典型 GStreamer 分析分支形态为：

```text
... videorate drop-only=true ! video/x-raw,format=NV12,width=640,height=480,framerate=15/1 ! fdsink fd=1 sync=false
```

如果看到下面形态，则说明当前使用的是 CPU/GStreamer RGB 转换路径：

```text
... videoconvert ! videoscale ! video/x-raw,format=RGB,width=640,height=640,framerate=15/1 ! fdsink fd=1 sync=false
```

运行时可以用环境变量强制选择分析转换后端：

```bash
# 强制使用 RGA。仅建议在 RK3588 且已确认 /dev/rga 与 librga 可用时设置。
export RK_VIDEO_ANALYSIS_CONVERT_BACKEND=rga

# 强制回退到 GStreamer CPU 转换，便于 A/B 对比或排查 RGA 环境问题。
export RK_VIDEO_ANALYSIS_CONVERT_BACKEND=gstreamer_cpu
```

注意：如果在不具备 RGA 条件的环境中强制设置 `RK_VIDEO_ANALYSIS_CONVERT_BACKEND=rga`，分析分支可能会切到 NV12 输出，但 `health-videod` 无法完成 NV12 到 RGB 的 RGA 转换，导致跌倒检测收不到有效分析帧。遇到这种情况请取消该环境变量，或显式设置为 `gstreamer_cpu` 后重启 bundle。

## DMA / DMABUF 分析链路运行开关

当前仓库已经支持一条板端实测通过的 DMA 分析链路：

```text
GstBuffer / raw frame
-> health-videod 分析支路输入
-> RGA import 输入 fd 或映射后的 bytes
-> RGA 输出 RGB 到 DMA heap fd
-> fd descriptor 发给 health-falld
```

这条路径不是默认打开的，需要在 `config/runtime_config.json` 中显式打开；环境变量只建议作为临时 override。

### 推荐开关组合

在 RK3588 板端推荐直接把下面这些值写进 `config/runtime_config.json`：

```json
{
  "system": {
    "runtime_mode": "system"
  },
  "video": {
    "pipeline_backend": "inproc_gst",
    "analysis_convert_backend": "rga"
  },
  "analysis": {
    "transport": "dmabuf",
    "rga_output_dmabuf": true,
    "gst_dmabuf_input": true,
    "dma_heap": "/dev/dma_heap/system-uncached-dma32"
  },
  "fall_detection": {
    "rknn_input_dmabuf": true
  }
}
```

含义：

- `RK_VIDEO_PIPELINE_BACKEND=inproc_gst`
  - 启用进程内 GStreamer `appsink` 分析分支
  - 不再依赖外部 `gst-launch-1.0 + fdsink`
- `RK_VIDEO_ANALYSIS_CONVERT_BACKEND=rga`
  - 分析分支使用 RGA 做缩放和颜色转换
- `RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf`
  - `health-videod -> health-falld` 使用 fd descriptor transport
- `RK_VIDEO_RGA_OUTPUT_DMABUF=1`
  - RGA 直接把 RGB 输出写到 DMA heap fd
- `RK_VIDEO_GST_DMABUF_INPUT=1`
  - 只启用 `appsink` 输入侧的 DMABUF 探测与日志
  - 如果当前协商出来的 `GstBuffer` 本身就是 `memory:DMABuf`，则优先走输入 fd 路径
  - 这个开关本身不再强制改变摄像头主链格式，也不再默认强制 `v4l2src io-mode=dmabuf`
- `RK_FALL_RKNN_INPUT_DMABUF=1`
  - `health-falld` 优先走 RKNN 输入侧 DMABUF 路径
- `RK_VIDEO_ANALYSIS_DMA_HEAP=/dev/dma_heap/system-uncached-dma32`
  - 当前推荐的 DMA heap
  - 如果不设置，代码也会默认回到这个 heap

### 仍属实验/排查用途的开关

```bash
RK_VIDEO_GST_FORCE_DMABUF_IO=1 ./scripts/start.sh --backend-only
```

含义：

- 强制 `v4l2src io-mode=dmabuf`
- 同时允许 in-process GStreamer 切到实验性的 `UYVY` DMABUF 协商路径
- 仅用于继续排查 GStreamer 协商问题
- 不建议作为默认生产开关长期保留

### 默认行为和回退

如果不设置这些开关，默认还是原来的稳定路径：

```text
外部 gst-launch -> raw frame bytes -> RGA/CPU -> shared memory 或 DMABUF descriptor
```

如果只打开一部分开关，行为是分层回退的：

- 没有 `RK_VIDEO_PIPELINE_BACKEND=inproc_gst`
  - 不会启用进程内 `appsink`
- 没有 `RK_VIDEO_RGA_OUTPUT_DMABUF=1`
  - RGA 输出仍可能回到 `QByteArray` 再发布
- `RK_VIDEO_GST_DMABUF_INPUT=1` 但板端没有命中 DMABUF
  - 会自动回退到原始 bytes 输入路径
  - 不应丢帧
- `RK_VIDEO_PIPELINE_BACKEND=inproc_gst` 且 `input_mode=test_file`
  - 会自动回退到现有外部 `gst-launch` 文件播放路径
  - 不会再因为 in-process 后端未实现 `filesrc/decodebin` 而导致 `start_test_input` 失败

### 当前板端限制

当前这块 RK3588 板子截至 2026-04-28 的已验证结论是：

- 默认稳定通路下，`NV12` 分析分支在 `appsink` 端会退化成 `SystemMemory`
- 只设置 `RK_VIDEO_GST_DMABUF_INPUT=1` 时，日志会看到：

```text
video_runtime event=gst_dmabuf_input unavailable reason=not_dmabuf
```

- 也就是说，当前默认推荐开关组合仍然是：

```text
NV12/SystemMemory -> RGA -> RGB DMA fd
```

- 如果额外设置 `RK_VIDEO_GST_FORCE_DMABUF_IO=1`，则会进入实验性的 `UYVY + io-mode=dmabuf` 协商路径；是否真正命中输入侧 DMABUF，仍取决于板端驱动和插件协商结果
- 当前不应把“输入侧 0 拷贝已稳定落地”当作既成事实来使用或宣传

### 板端启动示例

```bash
cd /home/elf/rk3588_bundle

./scripts/config.sh
./scripts/start.sh --backend-only
printf '%s\n' '{"action":"start_preview","request_id":"dmabuf-readme","camera_id":"front_cam","payload":{}}' | \
  nc -q 1 -U /home/elf/rk3588_bundle/run/rk_video.sock
```

如需临时 A/B 覆盖某个开关，优先用单次命令覆盖，而不是长期修改 shell 环境。例如：

```bash
RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf ./scripts/start.sh --backend-only
```

### 板端验证要点

确认 `camera` 预览模式下没有外部 `gst-launch-1.0`：

```bash
ps -ef | grep gst-launch | grep -v grep
```

期望为空。

确认 `test_file` 模式会自动切回外部 pipeline：

```bash
ps -ef | grep '[g]st-launch'
```

期望能看到 `filesrc location=... ! decodebin ...` 的文件播放命令。

确认 `health-videod` 的 DMABUF 输入探测结果：

```bash
grep -E 'preview_started|gst_dmabuf_input' /home/elf/rk3588_bundle/logs/health-videod.log
```

当前板端默认推荐开关下，期望看到类似：

```text
video_runtime event=gst_dmabuf_input unavailable reason=not_dmabuf
video_runtime camera=front_cam event=preview_started mode=camera backend=inproc_gst analysis=1 analysis_backend=rga
```

只有在额外开启 `RK_VIDEO_GST_FORCE_DMABUF_IO=1` 并且板端协商成功时，才可能看到 `available ... format=UYVY`。

确认分析 descriptor 走 DMABUF：

```bash
grep 'analysis_descriptor_published' /tmp/rk_video_dmabuf_input.jsonl | head
```

当前默认稳定收益点仍然是输出侧 DMABUF，因此期望字段至少包含：

```json
{"transport":"dmabuf","rga_output_dmabuf":true}
```

如果额外开启 `RK_VIDEO_GST_FORCE_DMABUF_IO=1`，并且板端实际命中了 `UYVY + io-mode=dmabuf` 输入路径，则 marker 中还会进一步出现：

```json
{"rga_input_dmabuf":true,"rga_output_dmabuf":true}
```
