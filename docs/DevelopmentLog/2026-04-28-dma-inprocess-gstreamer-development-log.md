# 2026-04-28 DMA 与进程内 GStreamer 性能路径开发日志

## 1. 背景与目标

本轮工作的起点是前一轮 Scheme B 的性能收益偏小。前一轮已经把分析帧调整为模型侧更容易消费的 RGB 640x640，并让 `health-falld` 尽量走 RKNN 输入侧的快速路径，但板端对比显示整体收益只有约 `0.21 ms`，主要原因是主耗时仍集中在 RKNN 推理本身以及跨进程大帧传输/拷贝上。

本轮用户明确提出：

- 优先利用 RK3588 的 DMA/RGA 资源做性能提升。
- 尽量不要进行大幅度代码修改。
- 每次关键改动尽量上板验证，ESP32 部分不在本轮范围内。
- 方案按“先做可落地的一、二阶段，第三阶段后续继续”的节奏推进。

因此本轮目标被拆成两个可合并的阶段：

1. 将 `health-videod` 的视频分析链路从外部 `gst-launch + fdsink/stdout`，改为可选的进程内 GStreamer `appsink`，为直接读取 `GstBuffer` 元数据和 DMABUF fd 做准备。
2. 让 RGA 把 NV12 分析帧转换后的 RGB 640x640 输出直接写入 DMA heap buffer，再通过已有 fd descriptor transport 发给 `health-falld`，避免 `RGA -> QByteArray -> DMA heap` 的大块 RGB 二次拷贝。

第三阶段，即真正从 `GstBuffer` DMABUF fd 作为 RGA 输入，已做探测和接口预留，但本轮板端没有实际命中 DMABUF 输入，因此没有把它作为稳定收益点宣传。

## 2. 保留的设计与计划文档

本轮开发过程中形成的过程文档全部保留，后续可以继续作为第三阶段 DMA 输入优化的依据：

- `docs/superpowers/specs/2026-04-28-inprocess-gstreamer-design.md`
- `docs/superpowers/plans/2026-04-28-inprocess-gstreamer-plan.md`
- `docs/superpowers/specs/2026-04-28-dma-performance-path-design.md`
- `docs/superpowers/plans/2026-04-28-dma-performance-path-plan.md`

这些文档记录的是方案设计和任务拆分；本文档记录本轮实际开发、问题、验证与收益。

## 3. 本轮主要代码改动

### 3.1 新增进程内 GStreamer 后端

新增文件：

- `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.h`
- `rk_app/src/health_videod/pipeline/inprocess_gstreamer_pipeline.cpp`

主要行为：

- 通过 `RK_VIDEO_PIPELINE_BACKEND=inproc_gst` 显式启用。
- 默认外部 `gst-launch-1.0` 路径不变，作为稳定 fallback。
- RK3588 bundle 构建时启用 `RKAPP_ENABLE_INPROCESS_GSTREAMER=ON`。
- Host 构建默认不强依赖 GStreamer dev 包，未启用时请求 `inproc_gst` 会返回 `inprocess_gstreamer_not_built`。
- 预览分支仍输出本地 TCP MJPEG，保持 UI 消费协议不变。
- 分析分支使用 `appsink name=analysis_sink` 取帧，不再依赖外部进程 stdout/fdsink 传 raw frame。

进程内 GStreamer 的典型数据流为：

```text
v4l2src
  -> video/x-raw,NV12
  -> tee
  -> preview: mppjpegenc rc-mode=fixqp q-factor=95 -> multipartmux -> tcpserversink
  -> analysis: videorate -> NV12 appsink -> health-videod callback
```

这一步的直接意义不是立刻零拷贝，而是移除了外部 `gst-launch` 进程边界，并让 `health-videod` 可以看到 `GstSample/GstBuffer`，为后续读取 DMABUF memory 做准备。

### 3.2 新增 RGA DMA 输出接口

修改文件：

- `rk_app/src/health_videod/analysis/analysis_frame_converter.h`
- `rk_app/src/health_videod/analysis/rga_frame_converter.h`
- `rk_app/src/health_videod/analysis/rga_frame_converter.cpp`

新增结构：

```cpp
struct AnalysisDmaBuffer {
    int fd = -1;
    quint32 payloadBytes = 0;
    quint32 offset = 0;
    quint32 strideBytes = 0;
};
```

约定：

- 输入 fd 是借用，converter 不关闭。
- 输出 fd 成功返回后由调用方负责关闭。

新增转换能力：

- `convertNv12ToRgbDma(...)`
  - 输入仍是 appsink 映射出来的 NV12 bytes。
  - 输出直接申请 DMA heap fd。
  - RGA 将 RGB 640x640 写入该 fd。
- `convertNv12DmaToRgbDma(...)`
  - 为后续真正的 `GstBuffer DMABUF -> RGA input fd -> RGB DMA fd` 做预留。
  - 本轮板端未实际命中输入 DMABUF，因此该路径目前是探测/实验能力。

默认 DMA heap：

```text
/dev/dma_heap/system-uncached-dma32
```

可通过环境变量覆盖：

```text
RK_VIDEO_ANALYSIS_DMA_HEAP=/dev/dma_heap/system-uncached-dma32
```

选择 uncached dma32 的原因来自前一轮探测：该 heap 在 RGA fd-backed probe 中表现稳定，避免了 cached heap 中 CPU/RGA 可见性不一致的问题。

### 3.3 `health-videod` 发布路径改造

修改文件：

- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`
- `rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.h`

新增/使用的关键开关：

```text
RK_VIDEO_PIPELINE_BACKEND=inproc_gst
RK_VIDEO_RGA_OUTPUT_DMABUF=1
RK_VIDEO_GST_DMABUF_INPUT=1
RK_VIDEO_GST_FORCE_DMABUF_IO=1
RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf
RK_VIDEO_ANALYSIS_DMA_HEAP=/dev/dma_heap/system-uncached-dma32
```

其中：

- `RK_VIDEO_RGA_OUTPUT_DMABUF=1` 是本轮稳定收益路径。
- `RK_VIDEO_GST_DMABUF_INPUT=1` 只启用对 appsink `GstBuffer` 是否为 DMABUF memory 的探测。
- `RK_VIDEO_GST_FORCE_DMABUF_IO=1` 是后续实验开关，才会强制 `v4l2src io-mode=dmabuf`；本轮修复后不再由 `RK_VIDEO_GST_DMABUF_INPUT=1` 默认强制，以避免兼容性风险。

稳定路径的数据流变为：

```text
GStreamer appsink NV12 bytes
  -> RGA virtual input
  -> RGA RGB output DMA heap fd
  -> health-videod publish DMABUF descriptor + fd
  -> health-falld receive fd
  -> RKNN RGB input DMABUF fast path
```

这条路径仍不是从摄像头到 RGA 的完整零拷贝，但已经去掉了原先最明确的一次大块 RGB 拷贝：

```text
旧路径: RGA RGB QByteArray -> health-videod 再复制到 DMA heap
新路径: RGA 直接写 DMA heap RGB fd
```

### 3.4 测试覆盖

修改文件：

- `rk_app/src/tests/video_daemon_tests/gstreamer_video_pipeline_backend_test.cpp`
- `rk_app/src/tests/CMakeLists.txt`

新增/调整的测试点：

- 未编译进程内 GStreamer 时，请求 `RK_VIDEO_PIPELINE_BACKEND=inproc_gst` 会明确失败。
- `RK_VIDEO_RGA_OUTPUT_DMABUF=1` 时，RGA DMA 输出成功会直接发布 DMABUF descriptor，不再走 QByteArray RGB 发布。
- DMA 输入 + DMA 输出接口可通过 fake converter 验证 descriptor 元数据和 fd 有效性。
- DMA 输入转换失败时返回 `false`，不发布错误 descriptor、不递增 frame id。
- 移除了 `#define private public` 的测试方式，改为更明确的测试 seam，避免污染生产头文件可见性。
- 当 `RKAPP_ENABLE_INPROCESS_GSTREAMER=ON` 时，`gstreamer_video_pipeline_backend_test` 也会编译/link `inprocess_gstreamer_pipeline.cpp`，避免 RK 编译路径缺少测试编译覆盖。

## 4. 开发过程中遇到的问题与处理

### 4.1 “现在是否已经零拷贝”的理解偏差

开发过程中重点澄清过一个问题：已有的 `RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf` 代表 `health-videod -> health-falld` 描述符和 fd 传输可以走 DMABUF，但不代表摄像头采集、NV12 到 RGB、RGA 输出、RKNN 输入全链路都是零拷贝。

本轮之前仍存在：

```text
appsink/fdsink raw frame -> QByteArray
RGA 输出 -> QByteArray
QByteArray RGB -> 新 DMA heap fd
```

本轮修复的是其中最确定的一段：让 RGA 直接输出到 DMA heap fd，减少 `RGB QByteArray -> DMA heap` 的二次大块复制。

### 4.2 NV12 转 RGB 发生在哪里

当前稳定路径中，NV12 转 RGB 发生在 `health-videod` 进程内，由 `RgaFrameConverter` 调用 RK RGA 完成。

GStreamer 分析分支在 RGA 模式下只负责输出 NV12：

```text
video/x-raw,format=NV12,width=<camera_width>,height=<camera_height>
```

然后 `health-videod` 对该 NV12 做：

```text
NV12 camera size -> RGA letterbox/resize -> RGB 640x640
```

这点很重要：如果走 CPU/GStreamer 分支，RGB 转换可能由 `videoconvert/videoscale` 完成；但本轮 DMA/RGA 路径下，目标是让 GStreamer 保持 NV12，交给 RGA 做转换和缩放。

### 4.3 GstBuffer DMABUF 输入没有实际命中

本轮尝试过让 in-process GStreamer 直接从 `GstBuffer` 取 DMABUF fd。板端结果显示：

```text
video_runtime event=gst_dmabuf_input unavailable reason=not_dmabuf
```

也就是说，当前板端 appsink 拿到的 buffer memory 不是 DMABUF memory。

曾尝试更强的 DMABUF caps / io-mode 方向，但会出现没有分析帧或兼容性风险。因此最终采取保守策略：

- `RK_VIDEO_GST_DMABUF_INPUT=1` 只做探测和可用时使用。
- 不再默认强制 `v4l2src io-mode=dmabuf`。
- 如需继续实验，使用单独的 `RK_VIDEO_GST_FORCE_DMABUF_IO=1`。
- 如果 DMA 输入转换失败，必须回退到原始 byte path，不能直接丢帧。

### 4.4 DMA 输入失败后的丢帧风险

代码审查时发现一个重要风险：如果 appsink 某次真的给了 DMABUF memory，旧实现会排队调用 DMA callback 并返回 `true`，后续如果 RGA fd 导入或转换失败，就没有机会回退到 byte path，导致该帧被静默丢弃。

修复后：

- DMA callback 返回 `bool` 表示是否真正处理成功。
- 处理失败时，in-process pipeline 会重新映射同一个 `GstSample`，走原来的 QByteArray/RGA fallback。
- 单测覆盖了 converter 返回失败时不发布 descriptor、不递增 frame id。

### 4.5 fd 所有权需要明确

DMA fd 在多层之间传递，容易出现 double close 或泄漏。最终约定并写入代码注释：

- 输入 fd：借用，converter 不关闭。
- 输出 fd：converter 成功返回后转移给调用方，调用方发布后关闭。
- `publishDmaBufDescriptor()` 内部通过 Unix fd passing 发送 fd，发送后原 fd 仍由调用方关闭。

### 4.6 UI 上板启动 DISPLAY 问题

一次完整 bundle 上板启动时，UI 因板端 `DISPLAY=:11` 的 xcb 环境不可用失败：

```text
qt.qpa.xcb: could not connect to display :11
Could not load the Qt platform plugin "xcb"
```

这不是本轮 DMA 代码导致的问题。重新用：

```text
QT_QPA_PLATFORM=offscreen
```

启动后，四个服务均正常运行，后续板端验证基于该环境完成。

## 5. 板端验证结果

### 5.1 Host 与交叉构建

Host 全量验证命令：

```bash
git diff --check && cmake --build out/build-rk_app-host -j$(nproc) && ctest --test-dir out/build-rk_app-host --output-on-failure
```

结果：

```text
57/57 tests passed
100% tests passed, 0 tests failed out of 57
```

RK3588 交叉构建命令：

```bash
BUILD_DIR=/tmp/rk_health_station-build-rk3588-review-fix BUILD_TESTING=OFF bash deploy/scripts/build_rk3588_bundle.sh
```

结果：

- `healthd` 生成成功。
- `health-ui` 生成成功。
- `health-videod` 生成成功，并链接 GStreamer app/video/allocators。
- `health-falld` 生成成功。
- 仅第三方 `rknn_model_zoo` 的 `postprocess.cc` 有既有 string literal warning，不是本轮引入。

### 5.2 上板运行环境

板端路径：

```text
/home/elf/rk3588_bundle
```

实际验证使用的主要环境变量：

```bash
QT_QPA_PLATFORM=offscreen
RK_VIDEO_PIPELINE_BACKEND=inproc_gst
RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf
RK_VIDEO_ANALYSIS_DMA_HEAP=/dev/dma_heap/system-uncached-dma32
RK_VIDEO_RGA_OUTPUT_DMABUF=1
RK_VIDEO_GST_DMABUF_INPUT=1
RK_FALL_RKNN_INPUT_DMABUF=1
RK_VIDEO_LATENCY_MARKER_PATH=/home/elf/rk3588_bundle/logs/video_latency_review_fix.jsonl
RK_FALL_LATENCY_MARKER_PATH=/home/elf/rk3588_bundle/logs/fall_latency_review_fix.jsonl
```

服务状态：

```text
healthd: running
health-videod: running
health-falld: running
health-ui: running
video socket state: present
analysis socket state: present
fall socket state: present
```

外部 GStreamer 进程检查：

```text
pgrep -a gst-launch-1.0
```

结果为空，说明当前预览+分析链路确实没有再启动外部 `gst-launch-1.0`。

### 5.3 关键运行日志

`health-videod` 日志中可见进程内 GStreamer 后端和 RGA 分析后端：

```text
video_runtime camera=front_cam event=preview_started mode=camera backend=inproc_gst analysis=1 analysis_backend=rga
video_runtime event=gst_dmabuf_input unavailable reason=not_dmabuf
```

这说明：

- in-process GStreamer 已启用。
- 分析转换使用 RGA。
- GstBuffer 输入 DMABUF 当前未命中，实际稳定收益来自 RGA 输出 DMA fd。

video marker：

```json
{"event":"analysis_descriptor_published","transport":"dmabuf","rga_output_dmabuf":true}
```

fall marker：

```json
{"event":"analysis_descriptor_ingested"}
```

性能日志：

```text
video_perf camera=front_cam mode=camera state=previewing fps=15.0 published=75 dropped_total=0 dropped_delta=0 consumers=1
video_perf camera=front_cam mode=camera state=previewing fps=15.2 published=76 dropped_total=0 dropped_delta=0 consumers=1
fall_perf camera=front_cam ingest_fps=15.0 infer_fps=15.0 avg_infer_ms=24.2 people_frames=0 empty_frames=75 state=monitoring error=
fall_perf camera=front_cam ingest_fps=15.1 infer_fps=15.1 avg_infer_ms=24.3 people_frames=0 empty_frames=76 state=monitoring error=
```

## 6. 收益评估

### 6.1 已确认收益

本轮收益主要是架构和拷贝路径收益，而不是 RKNN 推理耗时本身的大幅下降：

1. `health-videod` 不再需要外部 `gst-launch-1.0` 进程来给分析分支吐 raw frame。
2. 分析帧进入 `health-videod` 后可以直接看到 `GstSample/GstBuffer`，后续具备继续做 DMABUF 输入的基础。
3. RGA 输出 RGB 640x640 时直接写 DMA heap fd，减少一次 `RGB QByteArray -> DMA heap` 的大块复制。
4. `health-videod -> health-falld` 维持 fd descriptor transport，`health-falld` 可继续走 RKNN RGB input DMABUF fast path。
5. 板端 15 FPS 稳定，无丢帧增长，无外部 gst-launch 进程。

### 6.2 为什么收益没有非常大

当前板端仍显示：

```text
gst_dmabuf_input unavailable reason=not_dmabuf
```

因此本轮还没有做到：

```text
camera/GStreamer DMABUF -> RGA input fd
```

也就是说，输入侧仍需要把 appsink frame 映射成 bytes，再交给 RGA virtual input。已经减少的是 RGA 输出后的 RGB 大块复制，而不是摄像头到 RGA 的输入侧拷贝。

另外，当前 15 FPS 下每帧时间预算约 66.7 ms，而 `fall_perf avg_infer_ms` 约 24 ms，系统还有余量。单次移除一个内存拷贝对平均 FPS 的表观提升不会像 25 FPS 压力测试时那么明显。它的价值更可能体现在：

- 降低 CPU/memory bandwidth 抖动。
- 为 25 FPS 做余量准备。
- 为后续真正 DMABUF 输入打通接口。
- 减少跨进程 raw frame 管道复杂度。

## 7. 当前结论

本轮可以认为完成了“DMA 性能路径的一、二阶段”：

- 进程内 GStreamer 后端可用。
- RGA DMA 输出可用。
- fd descriptor transport 继续可用。
- Host 测试、RK3588 交叉构建、板端完整服务运行均通过。
- DMABUF 输入探测已接入，但当前板端未命中，因此不应把它描述为已实现完整零拷贝。

当前稳定生产路径是：

```text
appsink NV12 bytes -> RGA -> RGB DMA heap fd -> fd descriptor -> health-falld/RKNN
```

下一阶段建议继续攻关：

```text
GstBuffer DMABUF NV12 -> RGA fd input -> RGB DMA heap fd
```

重点需要解决：

1. 让 GStreamer appsink 分支实际产出 DMABUF memory。
2. 处理多 plane、offset、stride 等真实 V4L2/GStreamer DMABUF 元数据。
3. 验证 `v4l2src io-mode`、capsfeature `memory:DMABuf`、或 Rockchip 专用 element 对当前摄像头驱动的兼容性。
4. 在 25 FPS 压力下重新评估 DMA 输入、RGA 输出、RKNN 输入 fd fast path 的综合收益。

## 8. 续调试更新（2026-04-28 晚）

连接中断后继续沿方案 A 追查，补做了板端最小 probe 和完整服务复测，结论比前半段更清晰：

### 8.1 真正的 GStreamer 协商边界

板端最小 `appsink` probe 结果不是“DMABUF 完全不可用”，而是：

- `NV12` 路径：
  - `v4l2src ! video/x-raw,format=NV12 ! appsink` 得到的是 `SystemMemory`
  - `v4l2src io-mode=dmabuf ! ... NV12 ... ! appsink` 仍然是 `SystemMemory`
  - 显式加 `video/x-raw(memory:DMABuf),format=NV12` 会直接协商失败
- `UYVY` 路径：
  - `v4l2src io-mode=dmabuf ! video/x-raw,format=UYVY ! appsink` 可以稳定得到 `dmabuf`
  - 加上 `queue ! videorate` 后仍保持 `dmabuf`
  - 加上 `tee` 和预览支路后，只要 appsink 端回答 allocation query，分析支路仍保持 `dmabuf`

因此，这块 RK3588 板上的可行条件不是“NV12 DMABUF appsink”，而是：

```text
v4l2src io-mode=dmabuf + UYVY + appsink allocation query
```

### 8.2 两个实际修复点

为了把这条板端可行路径真正落进主程序，本次续调试新增了两个修复：

1. `UYVY` DMA 输入的 fallback stride 修正为 `width * 2`
   - 之前没有 `GstVideoMeta` 时，fallback stride 一律取 `width`
   - 这对 `NV12` 尚可，但对 packed `UYVY` 明显错误
2. 修正 `wrapbuffer_handle(...)` 的参数顺序
   - `im2d_buffer.h` 在 C++ 下的签名是：

     ```cpp
     wrapbuffer_handle(handle, width, height, format, wstride, hstride)
     ```

   - 旧代码把 `format` 放到了最后，导致 RGA 把 `640` 之类的 stride 值当成 format
   - 板端错误 `Invalid src format [0x280]` 中的 `0x280` 正是 `640`

### 8.3 最终板端运行结果

修复后，使用：

```text
RK_RUNTIME_MODE=system
RK_VIDEO_PIPELINE_BACKEND=inproc_gst
RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf
RK_VIDEO_RGA_OUTPUT_DMABUF=1
RK_VIDEO_GST_DMABUF_INPUT=1
RK_FALL_RKNN_INPUT_DMABUF=1
```

在 `2026-04-28` 板端复测得到：

- 没有外部 `gst-launch-1.0` 进程
- `health-videod` 日志出现：

  ```text
  video_runtime event=gst_dmabuf_input available payload_bytes=614400 stride=1280 format=UYVY
  video_runtime camera=front_cam event=preview_started mode=camera backend=inproc_gst analysis=1 analysis_backend=rga
  ```

- `video` latency marker 头部连续出现：

  ```json
  {"event":"analysis_descriptor_published","transport":"dmabuf","rga_input_dmabuf":true,"rga_output_dmabuf":true}
  ```

- `fall` marker 出现：

  ```json
  {"event":"first_analysis_frame","pixel_format":"rgb"}
  ```

这说明：

- 分析支路已经稳定拿到 `GstBuffer(memory:DMABuf)`
- `health-videod` 已经直接用输入 fd 做 RGA 转换
- RGA 输出继续直接写 RGB DMA heap fd
- `health-falld` 成功 ingest 这条 descriptor/fd 路径

### 8.4 更新后的当前稳定路径

前半段文档中的“稳定路径”需要更新。当前验证通过的稳定路径已经不是：

```text
appsink NV12 bytes -> RGA -> RGB DMA heap fd -> fd descriptor -> health-falld/RKNN
```

而是：

```text
GstBuffer DMABUF UYVY -> RGA input fd -> RGB DMA heap fd -> fd descriptor -> health-falld/RKNN
```

需要保留的限制也应同步更新：

- 这块板子上当前命中的不是 `NV12 DMABUF`，而是 `UYVY DMABUF`
- `NV12` 路径仍然会在 appsink 端退化成 `SystemMemory`
- `RK_VIDEO_GST_DMABUF_INPUT=1` 目前代表“启用这条实验性但已验证可运行的 UYVY DMABUF 输入路径”，不是通用的“任意格式 DMABUF 输入”

### 8.5 本次实际使用的 probe / 运行命令

本次续调试为了避免继续在主程序里盲改，先在板端使用独立 probe 逐层缩小协商边界。

构建并上传 probe 后，核心验证命令包括：

```bash
/home/elf/gst_dmabuf_appsink_probe \
  "v4l2src device=/dev/video11 num-buffers=5 ! \
   video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
   appsink name=sink emit-signals=false sync=false max-buffers=1 drop=true" 3

/home/elf/gst_dmabuf_appsink_probe \
  "v4l2src device=/dev/video11 io-mode=dmabuf num-buffers=5 ! \
   video/x-raw,format=UYVY,width=640,height=480,framerate=30/1 ! \
   queue leaky=downstream max-size-buffers=1 ! \
   videorate drop-only=true ! \
   video/x-raw,format=UYVY,width=640,height=480,framerate=15/1 ! \
   appsink name=sink emit-signals=false sync=false max-buffers=1 drop=true" 3

/home/elf/gst_dmabuf_appsink_probe \
  "v4l2src device=/dev/video11 io-mode=dmabuf num-buffers=5 ! \
   video/x-raw,format=UYVY,width=640,height=480,framerate=30/1 ! \
   tee name=t \
   t. ! queue ! mppjpegenc rc-mode=fixqp q-factor=95 ! multipartmux boundary=rkpreview ! fakesink sync=false \
   t. ! queue leaky=downstream max-size-buffers=1 ! videorate drop-only=true ! \
   video/x-raw,format=UYVY,width=640,height=480,framerate=15/1 ! \
   appsink name=sink emit-signals=false sync=false max-buffers=1 drop=true" 3 answer-allocation
```

最终完整服务复测使用的关键环境变量为：

```bash
RK_RUNTIME_MODE=system
RK_VIDEO_PIPELINE_BACKEND=inproc_gst
RK_VIDEO_ANALYSIS_CONVERT_BACKEND=rga
RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf
RK_VIDEO_RGA_OUTPUT_DMABUF=1
RK_VIDEO_GST_DMABUF_INPUT=1
RK_FALL_RKNN_INPUT_DMABUF=1
RK_VIDEO_ANALYSIS_DMA_HEAP=/dev/dma_heap/system-uncached-dma32
```

### 8.6 本次部署/验证过程中的实际约束

这次续调试还遇到一个与代码无关、但会影响复测节奏的实际问题：

- 板端根分区只剩很少空间
- 直接 `rsync` 一个新的候选 bundle 目录时，Qt plugin 拷贝阶段会报 `No space left on device`

因此本次最终采用的是：

```text
宿主机重新交叉编译 -> 仅替换板端现有 bundle 中的 health-videod 二进制 -> 原位复测
```

这不会改变结论，因为本次关键修复都集中在 `health-videod` 的：

- `InprocessGstreamerPipeline`
- `GstreamerVideoPipelineBackend`
- `RgaFrameConverter`

### 8.7 续调试时发现的两个真实问题

在用户按如下环境变量直接上板复测时：

```bash
export RK_RUNTIME_MODE=system
export RK_VIDEO_PIPELINE_BACKEND=inproc_gst
export RK_VIDEO_ANALYSIS_CONVERT_BACKEND=rga
export RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf
export RK_VIDEO_RGA_OUTPUT_DMABUF=1
export RK_VIDEO_GST_DMABUF_INPUT=1
export RK_FALL_RKNN_INPUT_DMABUF=1
export RK_VIDEO_ANALYSIS_DMA_HEAP=/dev/dma_heap/system-uncached-dma32
```

暴露出了两个之前没有彻底收口的问题：

1. `inproc_gst + test_file` 会直接失败  
   根因不是板端环境，而是代码里明确把 `test_file` 判成了 `inprocess_gstreamer_test_file_unsupported`。这会导致 UI 发出 `start_test_input` 后，后端实际上无法进入测试视频模式。

2. `RK_VIDEO_GST_DMABUF_INPUT=1` 被错误实现成了“切换主采集格式”的开关  
   原本这个开关应该只是“探测 appsink 是否拿到了 DMABUF memory”。但代码实际把它进一步联动成了：

```text
analysis input format -> UYVY
source caps -> UYVY
```

这会把整条摄像头主链直接切到实验性的 `UYVY` 路径，用户看到的“预览卡死”就是在这个错误实现下触发的。

### 8.8 本次补修的具体代码修正

本次续调试在不扩大改动边界的前提下，做了两个针对性修正：

1. `test_file` 自动回退到外部 `gst-launch` 路径  
   当用户仍然设置 `RK_VIDEO_PIPELINE_BACKEND=inproc_gst`，但当前输入模式是 `test_file` 时，`health-videod` 不再硬报不支持，而是自动回退到现有的：

```text
filesrc -> decodebin -> ... -> tcpserversink + fdsink
```

这样测试模式可以继续工作，同时不要求这轮就把 in-process `filesrc/decodebin` 全部补齐。

2. `RK_VIDEO_GST_DMABUF_INPUT=1` 收敛回“只探测，不改主链格式”  
   现在只有在额外显式设置：

```bash
export RK_VIDEO_GST_FORCE_DMABUF_IO=1
```

时，才允许进入实验性的：

```text
UYVY + io-mode=dmabuf
```

协商路径。只设置 `RK_VIDEO_GST_DMABUF_INPUT=1` 时，稳定主链仍保持默认相机格式，日志只负责报告：

```text
video_runtime event=gst_dmabuf_input unavailable reason=not_dmabuf
```

### 8.9 本次最终板端复测结果

修正后重新交叉构建、覆盖到板端，并在清理旧进程后再次使用同一组环境变量复测，结果如下。

#### 8.9.1 camera 预览模式

- `start_preview` 返回 `ok=true`
- 板端直接连接 `127.0.0.1:5602` 抽样 3 秒，实际读到：

```text
camera_bytes 5627010
camera_boundary_count 46
camera_jpeg_soi_count 46
camera_jpeg_eoi_count 45
```

- `health-videod.log` 中可见：

```text
video_runtime event=gst_dmabuf_input unavailable reason=not_dmabuf
video_runtime camera=front_cam event=preview_started mode=camera backend=inproc_gst analysis=1 analysis_backend=rga
video_perf camera=front_cam mode=camera state=previewing fps=15.0 ...
```

这说明：

- 预览链已经恢复连续出帧
- 分析链仍在稳定发布
- 输入侧 DMABUF 在默认推荐开关下没有命中，会自动回退到 bytes 输入路径

#### 8.9.2 test_file 模式

- `start_test_input` 返回 `ok=true`
- `health-videod` 会自动切到外部 `gst-launch filesrc + decodebin` 路径
- 板端直接连接 `127.0.0.1:5602` 抽样 3 秒，实际读到：

```text
test_bytes 4509955
test_boundary_count 76
test_jpeg_soi_count 75
test_jpeg_eoi_count 75
```

- `health-videod.log` 中可见：

```text
video_runtime camera=front_cam event=preview_started mode=test_file analysis=1 analysis_backend=rga
video_runtime camera=front_cam event=test_input_started file=/home/elf/Videos/video.mp4
video_perf camera=front_cam mode=test_file state=previewing fps=20.3 ...
```

这说明测试模式已经不再是“命令成功但实际切不进去”的状态，而是已经能真实输出 MJPEG 预览和分析帧。

### 8.10 本轮结束时应当如何表述结论

到本次修正完成为止，最准确的结论应该是：

- 进程内 GStreamer 预览 + 分析后端已经可用，`camera` 模式板端可稳定工作
- `test_file` 模式已经恢复可用，但当前是自动回退到外部 `gst-launch` 文件播放路径
- RGA 输出侧直写 DMA heap fd 的稳定收益路径已经打通
- 只设置 `RK_VIDEO_GST_DMABUF_INPUT=1` 时，当前板端默认仍未命中输入侧 DMABUF
- 因此现在不能宣称“sensor -> RGA -> RKNN 输入侧全链路稳定 0 拷贝已经完成”

换言之，本轮真正稳定落地的是：

```text
camera/test input
-> GStreamer 采集
-> RGA 输出 RGB DMA fd
-> fd descriptor 发给 health-falld
```

而不是“输入侧 DMABUF 0 拷贝已经稳定命中”的版本。

## 9. 0 拷贝与非 0 拷贝 A/B 对比补充（2026-04-28 夜）

在完成后续 `UYVY + io-mode=dmabuf` 路径验证后，又补做了一次直接的板端 A/B 对比，目标不是比较“老架构 vs 新架构”，而是只比较：

- 同样的 `inproc_gst + rga` 主链下
- 显式命中 0 拷贝路径
- 与显式关闭 DMA/DMABUF 路径的非 0 拷贝版本

这样可以把收益尽量收敛到 DMA/DMABUF 本身，而不是把外部 `gst-launch`、跨进程方式或其他结构性变化混在一起。

### 9.1 测试口径

两组都在同一块 RK3588 板上执行，`camera_id=front_cam`，都走 `start.sh --backend-only`，都使用 `start_preview` 拉起相机分析链路。

0 拷贝组环境变量：

```bash
RK_RUNTIME_MODE=system
QT_QPA_PLATFORM=offscreen
RK_VIDEO_PIPELINE_BACKEND=inproc_gst
RK_VIDEO_ANALYSIS_CONVERT_BACKEND=rga
RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf
RK_VIDEO_RGA_OUTPUT_DMABUF=1
RK_VIDEO_GST_DMABUF_INPUT=1
RK_VIDEO_GST_FORCE_DMABUF_IO=1
RK_FALL_RKNN_INPUT_DMABUF=1
RK_VIDEO_ANALYSIS_DMA_HEAP=/dev/dma_heap/system-uncached-dma32
```

非 0 拷贝组环境变量：

```bash
RK_RUNTIME_MODE=system
QT_QPA_PLATFORM=offscreen
RK_VIDEO_PIPELINE_BACKEND=inproc_gst
RK_VIDEO_ANALYSIS_CONVERT_BACKEND=rga
RK_VIDEO_ANALYSIS_TRANSPORT=shared_memory
```

采样方式：

- 每组运行 `18 s`
- 共跑 `2` 轮
- 第二轮刻意反向顺序执行，避免把“先跑/后跑”的缓存与预热效应误当成收益
- `health-videod` 采集 `RK_VIDEO_LATENCY_MARKER_PATH`
- `health-falld` 采集 `RK_FALL_POSE_TIMING_PATH`

### 9.2 路径命中证据

0 拷贝组两轮都稳定命中完整 DMA/DMABUF 路径：

- `health-videod.log` 出现 `video_runtime event=gst_dmabuf_input available`
- `video marker` 连续为 `"transport":"dmabuf"`
- `video marker` 中 `rga_input_dmabuf=true`
- `video marker` 中 `rga_output_dmabuf=true`
- `fall marker` 中 `input_dmabuf_path=true`
- `fall marker` 中 `io_mem_path=true`

非 0 拷贝组两轮都稳定停留在共享内存/普通输入路径：

- `video marker` 连续为 `"transport":"shared_memory"`
- 没有 `gst_dmabuf_input available`
- `fall marker` 中 `input_dmabuf_path=false`
- `fall marker` 中 `io_mem_path=false`

## 10. 统一运行时配置工作流更新（2026-04-29）

随着 `AppRuntimeConfig` 和独立 `health-config-ui` 落地，后续板端推荐流程不再是先 `export` 一长串环境变量，而是：

1. 编辑 `config/runtime_config.json`，或运行 `./scripts/config.sh`
2. 使用 `./scripts/start.sh` / `./scripts/start_all.sh`
3. 只在 A/B 对比或临时排障时追加单次环境变量 override

当前 0 拷贝板端推荐配置可以直接写成：

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
    "gst_force_dmabuf_io": true,
    "dma_heap": "/dev/dma_heap/system-uncached-dma32"
  },
  "fall_detection": {
    "rknn_input_dmabuf": true
  }
}
```

如果只是想快速验证 override 优先级，推荐像下面这样做单次覆盖，而不是把调试变量长期写进 shell：

```bash
RK_VIDEO_ANALYSIS_TRANSPORT=dmabuf ./scripts/start.sh --backend-only
```

因此这次对比不是“环境变量变了但实际还是同一路径”，而是两组确实命中了不同实现分支。

### 9.3 板端结果

两轮均值如下：

| 指标 | 0 拷贝 | 非 0 拷贝 | 差值（非 0 拷贝 - 0 拷贝） |
| --- | ---: | ---: | ---: |
| `video_marker_fps` | `14.976` | `14.975` | `+0.001` |
| `fall_avg_total_ms` | `21.971 ms` | `22.551 ms` | `+0.580 ms` |
| `fall_avg_rknn_run_ms` | `20.511 ms` | `20.998 ms` | `+0.487 ms` |
| `fall_avg_outputs_get_ms` | `0.167 ms` | `0.464 ms` | `+0.297 ms` |
| `fall_avg_inputs_set_ms` | `0.000 ms` | `0.005 ms` | `+0.005 ms` |
| `fall_input_dmabuf_ratio` | `1.000` | `0.000` | `-1.000` |
| `fall_io_mem_ratio` | `1.000` | `0.000` | `-1.000` |

按均值计算：

- `fall_avg_total_ms` 下降约 `0.58 ms`，约 `2.57%`
- `fall_avg_rknn_run_ms` 下降约 `0.49 ms`，约 `2.32%`
- `fall_avg_outputs_get_ms` 下降约 `0.30 ms`，约 `64.01%`

对应四次原始板端摘要如下：

| 轮次 | 路径 | `video_marker_fps` | `fall_avg_total_ms` | `fall_avg_rknn_run_ms` | `fall_avg_outputs_get_ms` |
| --- | --- | ---: | ---: | ---: | ---: |
| 第 1 轮 | 0 拷贝 | `14.978` | `21.914` | `20.453` | `0.169` |
| 第 1 轮 | 非 0 拷贝 | `14.975` | `22.102` | `20.852` | `0.198` |
| 第 2 轮 | 非 0 拷贝 | `14.975` | `23.000` | `21.144` | `0.729` |
| 第 2 轮 | 0 拷贝 | `14.975` | `22.029` | `20.568` | `0.165` |

### 9.4 如何解读这组结果

这次 A/B 对比说明三件事：

1. 0 拷贝路径已经不是“理论上可走”，而是板端实测能稳定命中的真实运行路径。
2. 在当前 `15 FPS` 分析节流下，吞吐几乎不变，`video_marker_fps` 基本一致，这符合预期。
3. 0 拷贝带来的收益主要体现在单帧处理链路变短，而不是直接把当前场景的 FPS 从 `15` 再抬高。

收益不算夸张也符合当前系统结构，因为：

- 分析支路本身被限制在约 `15 FPS`
- 主耗时仍然主要在 `RKNN` 推理本体，单帧约 `20~21 ms`
- 当前移除的是若干次内存搬运和输入/输出 staging，而不是替代掉整段推理

因此，这次 A/B 的最准确表述是：

- 0 拷贝路径已经打通并命中
- 当前板端能看到稳定但温和的单帧时延收益
- 在 `15 FPS` 场景下，收益更明显体现在 latency 和内存搬运缩减，而不是表观 FPS 提升

## 10. 补充后的当前结论

结合前文功能验证和本节 A/B 结果，到 `2026-04-28` 这轮开发结束时，更准确的结论应更新为：

- 默认保守配置下，系统仍可回退到稳定的非 0 拷贝路径。
- 在显式开启 `RK_VIDEO_GST_FORCE_DMABUF_IO=1` 后，这块板子上已经可以稳定命中：

```text
GstBuffer DMABUF UYVY -> RGA input fd -> RGB DMA heap fd -> fd descriptor -> health-falld/RKNN
```

- 这条完整 0 拷贝路径相对非 0 拷贝路径，在当前 `15 FPS` 场景下带来约 `0.58 ms` 的平均单帧总耗时下降。
- 因此，这轮工作不应再表述为“只有输出侧 DMA 优化已完成”，而应表述为：

```text
完整 0 拷贝路径已经在当前 RK3588 板上验证通过，
但它依赖显式的 UYVY + io-mode=dmabuf 协商条件，
默认保守配置仍应保留 fallback。
```
