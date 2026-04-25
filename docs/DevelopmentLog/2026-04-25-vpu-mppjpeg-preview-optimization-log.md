# 2026-04-25 RK3588 UI 预览 JPEG VPU 加速开发日志

## 1. 背景和目标

本轮工作的起点是视频后端的两条链路：

- `camera NV12 -> RGB 640x640 for AI`
- `camera NV12 -> JPEG for UI preview`

用户明确说明 STGCN/OpenCV 链路已经不再是当前关注重点，因此本轮只聚焦“原始视频流到 RGB/JPEG”的后端处理路径，尤其是 UI 预览 JPEG 分支是否能利用 RK3588 的硬件能力，而不是继续使用 CPU 软件编码。

查阅野火 RK3588 MPP/GStreamer 文档以及板端插件后，确认当前环境具备 Rockchip MPP GStreamer 插件，且 `mppjpegenc` 可用于 JPEG 编码。因此，本轮实现目标定为：

1. 先在 UI 预览 JPEG 链路上替换软件 `jpegenc`。
2. 保持现有 UI 预览协议不变，仍然使用 `multipartmux + tcpserversink` 输出本地 TCP MJPEG。
3. 尽量不触碰 AI RGB 分析支路，避免把 JPEG 优化和分析链路重构混在一起。
4. 与当前 main 基线做性能和画质对比，确保优化收益和风险可解释。

## 2. 改动范围

本轮代码改动集中在：

- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`
- `docs/architecture/camera-video-readme.md`

新增开发日志：

- `docs/DevelopmentLog/2026-04-25-vpu-mppjpeg-preview-optimization-log.md`

核心代码改动是把 UI 预览相关分支里的：

```bash
jpegenc ! multipartmux boundary=rkpreview ! tcpserversink ...
```

替换为：

```bash
mppjpegenc rc-mode=fixqp q-factor=95 ! multipartmux boundary=rkpreview ! tcpserversink ...
```

覆盖的后端分支包括：

1. camera 纯预览分支。
2. camera 预览 + AI analysis tap 分支。
3. test file 预览分支。
4. test file 预览 + AI analysis tap 分支。
5. recording pipeline 中 tee 出来的 UI 录像预览分支。

没有改变 snapshot 分支；snapshot 原本已经使用：

```bash
mppjpegenc ! filesink location=<snapshot_path>
```

## 3. 第一版方案：直接替换为 `mppjpegenc`

最初的低风险方案是把 UI 预览和录像预览分支里的 `jpegenc` 直接替换为 `mppjpegenc`。

设计理由：

- UI 协议仍然是 MJPEG multipart，不需要修改 UI 消费端。
- GStreamer pipeline 结构保持一致，只替换 JPEG encoder element。
- `mppjpegenc` 是板端环境最明确可用的硬件 JPEG 编码入口。

第一版替换后，板端可以运行，但用户反馈预览画面出现严重模糊：

- 画面模糊程度明显高于 main 工程。
- 点击一次截图后，画面会突然变清晰。
- 随后又逐渐回到非常模糊。

这个现象说明问题不是 UI 解码失败，也不是摄像头完全不可用，而是连续预览过程中 JPEG 质量随时间发生了变化。

## 4. 第二版方案：尝试 `q-factor=95`

为提高 JPEG 输出质量，第二版尝试增加：

```bash
mppjpegenc q-factor=95
```

这解决了部分初始画质问题，但没有解决“逐渐变糊”。板端连续抓帧后发现，单独设置 `q-factor=95` 时，`mppjpegenc` 仍然会在连续流中发生质量塌陷。

板端 180 帧独立测试结果如下：

| 编码器配置 | 首帧/前段表现 | 尾帧表现 | 平均帧大小 | 结论 |
| --- | ---: | ---: | ---: | --- |
| `jpegenc` | 正常 | 约 `33-46KB` | 约 `33KB` | main 基线，稳定 |
| `mppjpegenc q-factor=95` | 首帧约 `105KB` | 约 `5.4-5.8KB` | 约 `7.3KB` | 质量塌陷 |
| `mppjpegenc q-factor=99` | 首帧约 `220KB` | 约 `5.4-5.9KB` | 约 `8.2KB` | 仍然塌陷 |

这和用户看到的现象一致：截图会停止并重启预览 pipeline，所以刚重启后的前几帧较清晰；连续运行一段时间后，编码器输出又掉到几 KB，画面重新变糊。

## 5. 根因定位：MPP JPEG 编码码控模式导致连续流质量塌陷

继续对 `mppjpegenc` 参数进行最小变量测试后，发现关键不是单独提高 `q-factor`，而是需要固定码控模式。

有效配置是：

```bash
mppjpegenc rc-mode=fixqp q-factor=95
```

对比测试：

| 编码器配置 | 结果 |
| --- | --- |
| `mppjpegenc q-factor=95` | 尾帧稳定塌到约 `5426B`，画面严重模糊 |
| `mppjpegenc rc-mode=fixqp q-factor=95` | 帧大小稳定在几十 KB，无明显塌陷 |
| `mppjpegenc rc-mode=fixqp q-factor=99` | 稳定但帧过大，平均约 `118KB` |
| `mppjpegenc rc-mode=fixqp qf-min=95 qf-max=95` | 出现 GLib warning，不采用 |
| `mppjpegenc q-factor=95 bps=...` | 也能避免塌陷，但输出过大，不采用 |

最终选择：

```bash
mppjpegenc rc-mode=fixqp q-factor=95
```

选择原因：

1. 修复连续流质量塌陷。
2. 保持硬件 JPEG 编码路径。
3. 不引入 GLib warning。
4. 相比 `q-factor=99`，帧大小更可控。
5. 用户主观确认该版本预览清晰。

## 6. 最终实现方案

### 6.1 预览分支

camera 预览 pipeline 从：

```bash
v4l2src device=/dev/video11 ! \
video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
jpegenc ! multipartmux boundary=rkpreview ! \
tcpserversink host=127.0.0.1 port=5602
```

变为：

```bash
v4l2src device=/dev/video11 ! \
video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
mppjpegenc rc-mode=fixqp q-factor=95 ! multipartmux boundary=rkpreview ! \
tcpserversink host=127.0.0.1 port=5602
```

### 6.2 预览 + AI analysis tap 分支

默认板端运行时启用了 analysis tap，因此实际运行的完整预览命令是：

```bash
gst-launch-1.0 -q -e \
  v4l2src device=/dev/video11 ! \
  video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
  tee name=t \
  t. ! queue ! mppjpegenc rc-mode=fixqp q-factor=95 ! \
    multipartmux boundary=rkpreview ! \
    tcpserversink host=127.0.0.1 port=5602 \
  t. ! queue leaky=downstream max-size-buffers=1 ! \
    videorate drop-only=true ! \
    videoconvert ! videoscale ! \
    video/x-raw,format=RGB,width=640,height=640,framerate=15/1 ! \
    fdsink fd=1 sync=false
```

注意：本轮没有改变 AI RGB 分支，`videoconvert + videoscale` 仍在原路径中。

### 6.3 录像预览分支

录像时，UI 看到的是 recording pipeline tee 出来的预览支路。该支路同时固定为 `NV12` 输入，并从 `jpegenc` 改为：

```bash
video/x-raw,format=NV12,width=640,height=480 ! mppjpegenc rc-mode=fixqp q-factor=95 ! multipartmux boundary=rkpreview ! tcpserversink ...
```

录像文件主支路仍然使用：

```bash
mpph264enc ! h264parse ! qtmux ! filesink
```

### 6.4 Snapshot 分支

snapshot 不属于本轮修复重点，保持原有硬件 JPEG 单帧编码：

```bash
mppjpegenc ! filesink location=<snapshot_path>
```

## 7. 开发过程中遇到的问题

### 7.1 单独 `q-factor=95` 不足以修复模糊

最初以为模糊只是 JPEG quality 默认值偏低，所以先尝试了 `q-factor=95`。板端实测证明这个判断不完整。

真正的问题是连续流下 `mppjpegenc` 默认码控会把帧大小压到极低，导致画面逐渐模糊。必须加上 `rc-mode=fixqp` 才能稳定质量。

### 7.2 板端 bundle 同步时磁盘空间不足

第一次尝试把完整 `out/rk3588_bundle_mppjpeg_fixqp/` rsync 到板端 `/home/elf/rk3588_bundle_mppjpeg_fixqp/` 时失败：

```text
No space left on device (28)
```

当时板端根分区几乎满了，且存在多个历史 bundle。后续处理方式：

1. 先临时只替换已有 bundle 的 `bin/health-videod` 做验证。
2. 确认修复有效后，清理多余 bundle。
3. 使用 `rsync --link-dest=/home/elf/rk3588_bundle_mppjpeg_q95` 部署 hard-link 版本，形成：
   - `/home/elf/rk3588_bundle_mppjpeg_fixqp_linked`
4. 最终清理无效目录，只保留当前 VPU 修复版和 main 对比版。

清理后板端保留：

- `/home/elf/rk3588_bundle_mppjpeg_fixqp_linked`
- `/home/elf/rk3588_bundle_main_compare_linked`

### 7.3 `start_all.sh` 对旧进程残留比较敏感

验证过程中发现，如果旧服务仍在运行，直接启动另一个 bundle 容易造成“实际运行的进程”和“当前目录”不一致。后续验证统一采用：

```bash
./stop_all.sh
./start_all.sh
```

并通过 `pgrep -a gst-launch-1.0` 或 `/proc/<pid>/cmdline` 确认真实 GStreamer 命令。

### 7.4 完整 UI CPU 没有下降

纯 JPEG 编码链路中，VPU 版本 CPU 确实下降；但完整 UI + AI 默认链路中，总 CPU 没有下降，原因是：

1. `mppjpegenc rc-mode=fixqp q-factor=95` 输出帧更大，画质更高。
2. UI 仍然在 CPU 上用 `QImage::fromData(..., "JPEG")` 解码 JPEG。
3. UI 仍然使用 `SmoothTransformation` 做缩放显示。
4. AI RGB 分析支路仍然包含 `videoconvert + videoscale`。
5. 当前 640x480@15fps 的软件 `jpegenc` 本身不是全链路最大瓶颈。

因此本轮优化是“JPEG 编码点”收益明确，但“完整系统 CPU”受其它环节抵消。

## 8. 测试记录

### 8.1 Host 自动化测试

针对 GStreamer pipeline 构造和相关视频组件运行过：

```bash
ctest --test-dir out/build-rk_app-host \
  -R 'gstreamer_video_pipeline_backend_test|video_service_test|video_daemon_shutdown_test|video_preview_consumer_test|video_monitor_page_test' \
  --output-on-failure
```

结果：

```text
5/5 tests passed
```

其中 `gstreamer_video_pipeline_backend_test` 覆盖：

- camera 纯预览分支包含 `mppjpegenc rc-mode=fixqp q-factor=95`
- test file 预览分支包含 `mppjpegenc rc-mode=fixqp q-factor=95`
- analysis tap 预览分支包含 `mppjpegenc rc-mode=fixqp q-factor=95`
- recording preview 分支包含 `mppjpegenc rc-mode=fixqp q-factor=95`
- 预览分支不再包含 ` ! jpegenc ! `

### 8.2 RK3588 bundle 构建

构建过 RK3588 bundle：

- 首次修复验证构建目录：`/tmp/rk_health_station-build-rk3588-mppjpeg-fixqp`
- 首次修复验证输出目录：`out/rk3588_bundle_mppjpeg_fixqp`
- 提交前最终构建目录：`/tmp/rk_health_station-build-rk3588-vpu-final`
- 提交前最终输出目录：`out/rk3588_bundle_mppjpeg_fixqp_final`

两次构建均完成 ARM64 产物检查，包含：

- `healthd`
- `health-ui`
- `health-videod`
- `health-falld`

### 8.3 板端实际运行验证

最终板端运行目录：

```text
/home/elf/rk3588_bundle_mppjpeg_fixqp_linked
```

当前实际 GStreamer 命令：

```bash
gst-launch-1.0 -q -e v4l2src device=/dev/video11 ! \
video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
tee name=t \
t. ! queue ! mppjpegenc rc-mode=fixqp q-factor=95 ! \
multipartmux boundary=rkpreview ! \
tcpserversink host=127.0.0.1 port=5602 \
t. ! queue leaky=downstream max-size-buffers=1 ! \
videorate drop-only=true ! videoconvert ! videoscale ! \
video/x-raw,format=RGB,width=640,height=640,framerate=15/1 ! \
fdsink fd=1 sync=false
```

用户在板端运行：

```bash
cd /home/elf/rk3588_bundle_mppjpeg_fixqp_linked/scripts
./start_all.sh
```

并确认：

```text
这样运行确实是清晰的
```

### 8.4 连续 JPEG 质量验证

`mppjpegenc q-factor=95` 的失败特征：

```text
180 frames:
first10: [85035, 19562, 11121, 8712, 7717, 6942, 6428, 6258, 6267, 6220]
last10:  [5426, 5426, 5426, 5426, 5426, 5426, 5426, 5426, 5426, 5426]
avg:     6098.5 bytes
median:  5426 bytes
```

`mppjpegenc rc-mode=fixqp q-factor=95` 的稳定特征：

```text
180 frames:
first10: [96235, 53455, 47218, 44860, 45296, 45109, 42272, 40903, 41111, 42207]
last10:  [43491, 44474, 45719, 44480, 43837, 44667, 44533, 44797, 44469, 44386]
avg:     43805.9 bytes
median:  43143.5 bytes
```

UI 实际预览流抓取 `900` 帧后，仍没有掉到几 KB：

```text
count: 900
avg:   42976.1 bytes
median:41683.5 bytes
last10:[45351, 44963, 44631, 44231, 43982, 53755, 50304, 48904, 47742, 46727]
```

点击一次截图导致预览重启后，再抓 `300` 帧：

```text
post_snapshot_count: 300
avg:    50469.9 bytes
median: 49465.5 bytes
last10: [48581, 48231, 48682, 50133, 51586, 51936, 51943, 50160, 48132, 47923]
```

这证明“截图后短暂清晰再变糊”的问题已经由 `fixqp` 配置消除。

## 9. 性能对比

### 9.1 完整 UI + AI 默认链路

对比对象：

- main 基线：`/home/elf/rk3588_bundle_main_compare_linked`
- VPU 修复版：`/home/elf/rk3588_bundle_mppjpeg_fixqp_linked`

同样采集 `300` 帧，约 `20s`。

| 指标 | main `jpegenc` | VPU `mppjpegenc fixqp` | 说明 |
| --- | ---: | ---: | --- |
| 预览 FPS | `14.99` | `15.00` | 基本一致 |
| `gst-launch` CPU | `30.8%` | `31.9%` | VPU 版略高 |
| 全服务 CPU 合计 | `60.7%` | `62.2%` | VPU 版略高 |
| JPEG 平均帧大小 | `16.0KB` | `29.8KB` | VPU 版画质更高、数据更大 |
| JPEG 尾帧大小 | `8-11KB` | `36-38KB` | VPU 版无质量塌陷 |

解释：完整链路中，JPEG 编码只是其中一部分。VPU 版输出更大的 JPEG 后，UI 侧 CPU JPEG 解码、socket 读取、multipart 解析和缩放显示的成本增加，抵消了编码器本身的收益。

### 9.2 纯 JPEG 编码链路

为了隔离编码器本身，单独运行：

```bash
v4l2src device=/dev/video11 ! \
video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
<encoder> ! multipartmux boundary=rkpreview ! \
tcpserversink host=127.0.0.1 port=5602
```

结果：

| 指标 | `jpegenc` | `mppjpegenc rc-mode=fixqp q-factor=95` | 说明 |
| --- | ---: | ---: | --- |
| 预览 FPS | `15.00` | `14.99` | 基本一致 |
| `gst-launch` CPU | `7.0%` | `4.0%` | VPU 编码 CPU 降低约 `43%` |
| JPEG 平均帧大小 | `14.7KB` | `42.1KB` | VPU 版画质更高 |

结论：只看 JPEG 编码点，VPU 有明确 CPU 收益；但在完整默认应用中，由于输出画质更高、帧更大，下游 CPU 成本会上升。

## 10. 当前结论

本轮最终结果可以归纳为：

1. `mppjpegenc` 可以用于 RK3588 UI 预览 JPEG 硬件编码。
2. 不能只设置 `q-factor=95`，否则连续 MJPEG 流会出现质量塌陷。
3. 当前必须使用 `rc-mode=fixqp q-factor=95` 才能稳定解决模糊问题。
4. 该配置让预览画面比 main 更清晰，且用户已在板端确认。
5. 纯 JPEG 编码点 CPU 从约 `7.0%` 降到约 `4.0%`。
6. 完整 UI + AI 默认链路 CPU 暂未下降，因为 UI 解码/缩放和 AI RGB 分支仍占主要开销，且 VPU 版 JPEG 帧更大。

## 11. 后续优化建议

如果下一步目标是“完整 UI 默认运行时 CPU 也低于 main”，建议继续做两类实验：

1. 质量参数平衡：
   - 测试 `mppjpegenc rc-mode=fixqp q-factor=90`
   - 测试 `mppjpegenc rc-mode=fixqp q-factor=88`
   - 测试 `mppjpegenc rc-mode=fixqp q-factor=85`
   - 目标是在不复现 5KB 塌陷和主观模糊的前提下，降低 JPEG 平均帧大小。
2. 下游链路优化：
   - UI JPEG 解码是否可减少拷贝或降低缩放成本。
   - AI RGB 分支的 `videoconvert/videoscale` 是否能用 RGA/GStreamer 硬件路径替代。
   - 是否降低 UI 预览帧率，例如 UI 预览 15fps，AI 分析独立维持所需帧率。

## 12. 提交前代码审查详情

本次提交前代码审查按以下几个维度进行。

### 12.1 Pipeline 参数和分支覆盖

检查 `GstreamerVideoPipelineBackend::buildPreviewCommand()` 和 `buildRecordingCommand()` 后确认：

- camera 纯预览分支使用 `mppjpegenc rc-mode=fixqp q-factor=95`。
- camera + analysis tap 分支使用 `mppjpegenc rc-mode=fixqp q-factor=95`。
- test file 纯预览分支使用 `mppjpegenc rc-mode=fixqp q-factor=95`。
- test file + analysis tap 分支使用 `mppjpegenc rc-mode=fixqp q-factor=95`。
- recording preview tee 分支使用 `mppjpegenc rc-mode=fixqp q-factor=95`。
- snapshot 单帧拍照分支仍保持 `mppjpegenc ! filesink`，没有被误改为连续流参数。

### 12.2 审查中发现并修复的问题

审查时发现两个可以在提交前补强的问题：

1. test file 纯预览分支在 `videoconvert ! videoscale` 后只限制了 `width/height`，没有显式固定为 `NV12`。
   - 风险：`mppjpegenc` 在不同 GStreamer 协商结果下可能收到不稳定的 raw format。
   - 修复：改为 `video/x-raw,format=NV12,width=%3,height=%4`。
2. recording preview tee 分支在 `videoscale` 后只限制了 `width/height`，没有显式固定为 `NV12`。
   - 风险：未来 record profile 或 GStreamer 协商变化时，预览分支输入格式不够明确。
   - 修复：改为 `video/x-raw,format=NV12,width=%7,height=%8`。

对应测试也补充了 `video/x-raw,format=NV12` 和 `video/x-raw,format=NV12,width=640,height=480` 断言。

### 12.3 测试覆盖审查

提交前测试覆盖已经覆盖以下行为：

- camera 纯预览命令中包含硬件 JPEG encoder 和 fixqp 参数。
- test file 预览命令中包含硬件 JPEG encoder、fixqp 参数和 NV12 caps。
- analysis tap 分支仍然保留 RGB 640x640 输出和 15fps 限速。
- recording preview 分支同时包含 `mppjpegenc rc-mode=fixqp q-factor=95` 与 `mpph264enc`。
- 所有预览分支断言不再出现独立的软件 ` ! jpegenc ! `。

### 12.4 已知限制

当前提交没有解决以下问题，后续如有性能目标需要继续跟进：

- UI 侧 JPEG 解码仍然是 CPU 的 `QImage::fromData`。
- UI 显示缩放仍然使用 Qt 的 `SmoothTransformation`。
- AI RGB 分支仍使用 `videoconvert + videoscale`，未切换到 RGA 硬件路径。
- `q-factor=95` 保证清晰但帧更大；如果要降低完整 UI CPU，需要继续测试 `90/88/85` 等质量参数。


## 13. 提交前自审记录

提交前重点检查项：

- 预览和录像预览分支均已替换为 `mppjpegenc rc-mode=fixqp q-factor=95`。
- snapshot 分支未被误改。
- UI 预览协议未改变，仍然是 `multipartmux + tcpserversink`。
- analysis tap 分支未改变 RGB 输出格式、尺寸和帧率限制。
- 测试覆盖 camera 预览、test file 预览、analysis tap 预览和 recording preview。
- 架构文档已同步从 `jpegenc` 更新为 `mppjpegenc rc-mode=fixqp q-factor=95`。

