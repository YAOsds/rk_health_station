# Camera / Video README

## Overview

当前摄像相关能力由两个独立进程协作完成：

- `health-videod`：摄像后端守护进程，唯一允许直接打开 `/dev/video11`
- `health-ui`：Qt 前端，只通过本地 IPC 获取状态和控制摄像，不直接接触 V4L2 设备

这套设计满足两个核心约束：

- UI 可以集成到现有 Qt 主界面
- 摄像后端必须与 UI 分进程，避免把采集、编码、落盘、设备访问逻辑混入界面层

当前默认板端摄像头配置：

- camera id: `front_cam`
- device path: `/dev/video11`
- default storage dir: `/home/elf/videosurv/`

## Process Boundary

### `health-videod`

职责：

- 管理摄像通道状态
- 启动和维护预览 pipeline
- 执行拍照
- 执行录像开始 / 停止
- 校验并应用媒体保存目录
- 通过本地 socket 对 UI 提供控制接口

关键入口文件：

- `rk_health_station/rk_app/src/health_videod/app/video_daemon_app.cpp`
- `rk_health_station/rk_app/src/health_videod/core/video_service.cpp`
- `rk_health_station/rk_app/src/health_videod/ipc/video_gateway.cpp`
- `rk_health_station/rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`

### `health-ui`

职责：

- 展示 Video 页面
- 显示当前预览
- 触发拍照、开始录像、结束录像、刷新状态、修改存储目录
- 不直接访问摄像头设备文件

关键入口文件：

- `rk_health_station/rk_app/src/health_ui/pages/video_monitor_page.cpp`
- `rk_health_station/rk_app/src/health_ui/ipc_client/video_ipc_client.cpp`
- `rk_health_station/rk_app/src/health_ui/widgets/video_preview_widget.cpp`
- `rk_health_station/rk_app/src/health_ui/widgets/video_preview_consumer.cpp`

## Control Plane

### Local IPC

UI 和视频后端之间的控制面使用 `QLocalServer` / `QLocalSocket`：

- socket name env: `RK_VIDEO_SOCKET_NAME`
- default runtime socket: `run/rk_video.sock`

协议形态：

- newline-delimited JSON
- UI 发命令
- `health-videod` 返回 `VideoCommandResult`
- `get_status` / `start_preview` 返回 `VideoChannelStatus`

当前支持的动作：

- `get_status`
- `start_preview`
- `take_snapshot`
- `start_recording`
- `stop_recording`
- `set_storage_dir`

对应实现入口：

- `rk_health_station/rk_app/src/health_videod/ipc/video_gateway.cpp`
- `rk_health_station/rk_app/src/shared/protocol/video_ipc.cpp`

## Data Plane

### Why the preview path is `TCP + MJPEG`

当前实时预览方案不是 UI 自己解码 H.264，也不是 UI 直接打开 `/dev/video11`，而是：

- `health-videod` 在本地启动 GStreamer preview pipeline
- preview pipeline 输出 `TCP + multipart MJPEG`
- `health-ui` 使用 `QTcpSocket` 连接本地 TCP 端口
- UI 解析 multipart 边界和 JPEG 帧，并用 `QImage` / `QPixmap` 进行软件渲染

当前之所以选择这条链路，而不是更早的本地 `UDP/H264` 方案，原因是：

- UI 进程自己拉起本地 H.264 消费链在 RK3588 当前运行环境下不稳定
- `TCP + MJPEG` 更容易被 Qt 侧稳定消费
- UI 不需要承担编解码 pipeline 管理职责
- 继续保持后端独立进程边界

### Current preview URL format

当前 `health-videod` 向 UI 返回的预览地址格式：

```text
tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview
```

其中：

- host: `127.0.0.1`
- port: `5602`（`front_cam` 当前固定）
- transport: `tcp_mjpeg`
- boundary: `rkpreview`

### Preview pipeline

当前预览 pipeline 由 `GstreamerVideoPipelineBackend::buildPreviewCommand()` 生成，等价逻辑如下：

```bash
gst-launch-1.0 -e \
  v4l2src device=/dev/video11 ! \
  video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
  jpegenc ! \
  multipartmux boundary=rkpreview ! \
  tcpserversink host=127.0.0.1 port=5602
```

含义：

- 输入设备为板端摄像头 `/dev/video11`
- 预览分辨率固定为 `640x480`
- 编码为 JPEG
- 通过 multipart 边界封装
- 由本地 TCP server 输出给 UI

### UI preview consumer

UI 侧预览消费者做的事情：

1. 解析 `preview_url`
2. 校验 `transport=tcp_mjpeg`
3. 连接本地 TCP 端口
4. 从流里按 boundary 拆分 multipart body
5. 抽取 `image/jpeg`
6. 解码成 `QImage`
7. 在 `VideoPreviewWidget` 里显示

实现文件：

- `rk_health_station/rk_app/src/health_ui/widgets/video_preview_consumer.cpp`

## Source-Level Implementation Walkthrough

这一节不再只讲“有什么模块”，而是贴着当前源码说明：

- 一个 Qt 按钮是如何触发后端动作的
- 摄像头原始帧如何在 `health-videod` 里被编码和分发
- Qt 端如何接流、解码、渲染
- 拍照和录像为什么不是和预览完全共用一条 pipeline

### 1. 后端启动时如何把预览准备好

`VideoDaemonApp::start()` 是 `health-videod` 的应用入口之一，启动顺序非常直接：

1. `gateway_->start()` 先启动本地 `QLocalServer`
2. `service_->startPreview("front_cam")` 立即为默认摄像头拉起预览

对应文件：

- `rk_health_station/rk_app/src/health_videod/app/video_daemon_app.cpp`

这意味着 UI 不是第一个“主动拉流”的角色。  
当前实现里，后端启动后就尽量把 `front_cam` 的预览 pipeline 先跑起来，UI 后续只要查询状态，就能拿到现成的 `preview_url`。

### 2. 控制面是怎样从 Qt 页面走到后端服务的

按钮触发链路从 `VideoMonitorPage` 开始。

在 `VideoMonitorPage` 构造函数里，几个核心按钮都直接连接到 `AbstractVideoClient`：

- `Take Snapshot` -> `client_->takeSnapshot(currentCameraId_)`
- `Start Recording` -> `client_->startRecording(currentCameraId_)`
- `Stop Recording` -> `client_->stopRecording(currentCameraId_)`
- `Apply Directory` -> `client_->setStorageDir(...)`
- `Refresh Status` -> `client_->requestStatus(currentCameraId_)`

对应文件：

- `rk_health_station/rk_app/src/health_ui/pages/video_monitor_page.cpp`

`health-ui` 当前默认客户端实现是 `VideoIpcClient`。它负责把这些高层动作编码成一行一条的 JSON 命令，并通过 `QLocalSocket` 发给 `health-videod`：

1. `sendCommand()` 组装 `VideoCommand`
2. 用 `QJsonDocument(...).toJson(QJsonDocument::Compact)` 编码
3. 末尾补一个换行符，形成 newline-delimited JSON
4. 写入本地 socket `rk_video.sock`

对应文件：

- `rk_health_station/rk_app/src/health_ui/ipc_client/video_ipc_client.cpp`

后端侧由 `VideoGateway` 接收：

1. `QLocalServer::newConnection` 接入本地连接
2. `onSocketReadyRead()` 按换行切帧
3. `decodeCommand()` 反序列化成 `VideoCommand`
4. `route()` 按 action 分发到 `VideoService`

这里要注意一个很重要的架构点：

- `VideoGateway` 只负责协议接入和分发
- `VideoService` 才负责业务状态和 pipeline 生命周期
- 真正触碰 GStreamer 命令拼接与进程管理的是 `GstreamerVideoPipelineBackend`

这三层边界让后续扩展“多摄像头 / 多协议 / 多前端”时不必把逻辑全塞进 UI。

对应文件：

- `rk_health_station/rk_app/src/health_videod/ipc/video_gateway.cpp`
- `rk_health_station/rk_app/src/health_videod/core/video_service.cpp`
- `rk_health_station/rk_app/src/health_videod/pipeline/gstreamer_video_pipeline_backend.cpp`

### 3. 摄像头视频如何变成 Qt 里的实时预览

这条链路可以分成 4 个阶段。

#### 阶段 A：从 `/dev/video11` 采集原始帧

预览 pipeline 由 `GstreamerVideoPipelineBackend::buildPreviewCommand()` 生成。  
源码里拼出来的命令本质上就是：

```bash
gst-launch-1.0 -e \
  v4l2src device=/dev/video11 ! \
  video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! \
  jpegenc ! \
  multipartmux boundary=rkpreview ! \
  tcpserversink host=127.0.0.1 port=5602
```

这一步里：

- `v4l2src` 直接从内核 V4L2 设备读帧
- `video/x-raw,format=NV12,...` 约束输入 profile
- 当前预览 profile 来自 `VideoService::initializeDefaultChannels()`

也就是说，摄像头出来的是原始视频帧，至少在当前链路里，还没有“直接给 Qt 看”的能力，必须先经过后端处理。

#### 阶段 B：在后端做预览编码和封装

采集到的 `NV12` 原始帧先经过 `jpegenc` 变成 JPEG 帧，再由 `multipartmux` 封装成 multipart 流。

这里的“推流”并不是 RTSP/RTMP 那种媒体服务器模型，而是更轻量的本地 TCP 推送：

- `tcpserversink` 在 `127.0.0.1:5602` 监听
- 每个连接上来的本地消费者都会收到连续的 multipart JPEG 数据
- URL 里通过 `boundary=rkpreview` 告诉 UI 如何分包

所以当前系统里的“预览推流”本质上是：

- 后端采集原始帧
- 后端编码成 JPEG
- 后端通过 TCP 连续输出 multipart MJPEG 字节流

#### 阶段 C：Qt 端接流和拆包

UI 收到的不是单独一张图片，也不是 H.264 裸流，而是一条连续的 TCP 字节流。  
`VideoPreviewConsumer` 的职责就是把这条流重新拆回一帧一帧的 JPEG：

1. `configureSource()` 解析 `tcp://...` URL
2. 校验 `scheme == tcp`
3. 校验 `transport == tcp_mjpeg`
4. 记录 host / port / boundary
5. `QTcpSocket::connectToHost()` 建立本地连接
6. `readyRead` 到来后不断把字节追加到 `streamBuffer_`
7. 通过 `boundaryMarker_` 找 multipart 边界
8. 解析头部中的 `Content-Type` / `Content-Length`
9. 抽出一帧 JPEG 字节

这里的实现是一个纯 Qt 的 multipart parser，没有额外起 GStreamer 消费进程，也没有在 UI 里再跑一条播放器 pipeline。

#### 阶段 D：Qt 端 JPEG 解码和渲染

真正的“视频解码”在当前预览方案里发生在 UI 侧这一步：

- `VideoPreviewConsumer::emitJpegFrame()` 调用 `QImage::fromData(jpegBytes, "JPEG")`
- 如果解码成功，发出 `frameReady(QImage)`
- `VideoPreviewWidget` 收到后把 `QImage` 保存到 `currentFrame_`
- 再通过 `QPixmap::fromImage(...scaled(...))` 渲染到 `QLabel`

对应文件：

- `rk_health_station/rk_app/src/health_ui/widgets/video_preview_consumer.cpp`
- `rk_health_station/rk_app/src/health_ui/widgets/video_preview_widget.cpp`

因此当前“Qt 预览解码”不是硬解 H.264，而是：

- 后端把原始帧编码成 JPEG
- UI 用 `QImage` 做 JPEG 软件解码
- Widget 再做一次缩放显示

这也是当前方案稳定的关键原因之一：Qt 端只承担相对简单的 JPEG 解码和绘制，不承担 H.264 消费 pipeline 的生命周期管理。

### 4. UI 是如何拿到并刷新预览地址的

预览并不是 `VideoPreviewWidget` 自己决定从哪拉流。  
真正的预览源来自后端返回的 `VideoChannelStatus.previewUrl`。

链路如下：

1. UI 请求 `get_status`
2. `VideoGateway::buildStatusResult()` 把 `VideoChannelStatus` 序列化返回
3. `VideoIpcClient::onReadyRead()` 解码响应
4. 对 `get_status` / `start_preview` 响应额外解析出 `VideoChannelStatus`
5. 发出 `statusReceived(status)`
6. `VideoMonitorPage::onStatusReceived()` 调用 `previewWidget_->setPreviewSource(...)`
7. `VideoPreviewWidget` 再驱动 `VideoPreviewConsumer::start(source)`

这使得 UI 不需要猜测端口，也不需要自己拼接 preview URL。  
所有“流从哪里来”的决定都由后端统一下发。

### 5. 拍照是如何触发的，为什么不是直接从预览流截帧

#### 控制触发

按钮点击后，调用链是：

`VideoMonitorPage` -> `VideoIpcClient::takeSnapshot()` -> `VideoGateway::route()` -> `VideoService::takeSnapshot()`

#### 后端处理逻辑

`VideoService::takeSnapshot()` 当前做了几件关键事情：

1. 检查 camera 是否存在
2. 若当前正在录像，直接拒绝，返回 `busy_recording`
3. 校验存储目录是否可写
4. 如果预览正在运行，先 `stopPreview()`
5. 生成新的快照文件名
6. 调用 `pipelineBackend_->captureSnapshot(...)`
7. 更新 `lastSnapshotPath`
8. 如果之前有预览，再重新 `ensurePreview()`

这里体现出当前实现的重要取舍：  
拍照不是从 MJPEG 预览流里“抓一帧”，而是单独跑一条 one-shot pipeline。

#### 为什么单独跑 one-shot pipeline

`buildSnapshotCommand()` 生成的是：

```bash
gst-launch-1.0 -e \
  v4l2src device=/dev/video11 num-buffers=1 ! \
  video/x-raw,format=NV12,width=1920,height=1080 ! \
  mppjpegenc ! \
  filesink location=<snapshot_path>
```

这样做有几个直接好处：

- 快照规格可以独立于预览规格，当前就是 `1920x1080`
- 不依赖 UI 当前是否已经连上预览流
- 不需要从低分辨率预览帧回收图片
- 直接得到板端落盘文件，路径由后端统一管理

这里的编码器使用 `mppjpegenc`，说明拍照产物是在后端直接编码成 JPEG 文件后写盘，而不是先把原始帧送给 UI 再存。

### 6. 录像是如何触发的，为什么能一边录一边继续预览

#### 控制触发

按钮点击后，链路是：

`VideoMonitorPage` -> `VideoIpcClient::startRecording()` -> `VideoGateway::route()` -> `VideoService::startRecording()`

#### `VideoService::startRecording()` 的状态切换

当前实现不是在已有预览 pipeline 上“热插拔”录像分支，而是显式切换：

1. 校验 camera 状态
2. 校验存储目录
3. 先调用 `ensurePreview()`，确保当前 camera 已可正常预览
4. 再调用 `stopPreview()`，停掉纯预览 pipeline
5. 生成新的录像文件路径
6. 启动 recording pipeline
7. 将 `channel.recording = true`
8. 把状态切到 `Recording`

这里先确保预览可用，再切换到录像 pipeline，是为了避免“摄像头本身不可用时直接切录像”造成状态判断不清。

#### 录像 pipeline 的编码和分流

`buildRecordingCommand()` 生成的逻辑如下：

```bash
gst-launch-1.0 -e \
  v4l2src device=/dev/video11 ! \
  video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 ! \
  tee name=t \
  t. ! queue ! videoscale ! video/x-raw,width=640,height=480 ! \
  jpegenc ! multipartmux boundary=rkpreview ! \
  tcpserversink host=127.0.0.1 port=5602 \
  t. ! queue ! mpph264enc ! h264parse ! qtmux ! filesink location=<record_path>
```

可以把它理解成“同一个摄像头输入，被 tee 成两路”：

- 一路用于预览：
  - `videoscale` 把录像输入缩到预览分辨率
  - `jpegenc` 编成 JPEG
  - `multipartmux + tcpserversink` 继续给 UI 输出 MJPEG
- 一路用于录像：
  - `mpph264enc` 做 H.264 编码
  - `h264parse` 整理码流
  - `qtmux` 封装为 MP4
  - `filesink` 落盘

所以当前录像时，UI 看到的其实不是 MP4 文件的回放，也不是 H.264 解码画面，而是录像 pipeline 里 tee 出来的“预览支路”。

#### 停止录像时为什么还要重新拉一次预览

`VideoService::stopRecording()` 会：

1. 调 `pipelineBackend_->stopRecording()`
2. 清理 recording 状态
3. 把 `previewUrl` 先清空
4. 把状态回到 `Idle`
5. 再调用 `ensurePreview()` 重建纯预览 pipeline

原因很简单：  
录像停止后，录像专用 pipeline 被整体结束了，其中包含的预览支路也一起消失。  
因此必须重新启动一条纯预览 pipeline，UI 才能继续显示实时画面。

### 7. 当前方案里的“解码、编码、推流”分别发生在哪里

如果从媒体技术角度拆分，当前实现大致是这样：

- 采集：
  - `v4l2src` 从 `/dev/video11` 读取原始视频帧
- 预览编码：
  - `jpegenc` 把原始帧编码成 JPEG
- 预览推流：
  - `multipartmux + tcpserversink` 把 JPEG 帧连续输出为本地 TCP MJPEG 流
- UI 预览解码：
  - `QImage::fromData(..., "JPEG")` 把 JPEG 字节解码为 `QImage`
- 录像编码：
  - `mpph264enc` 负责编码成 H.264
- 录像封装：
  - `qtmux` 把 H.264 封装为 MP4
- 拍照编码：
  - `mppjpegenc` 直接把单帧编码成 JPEG 文件

也就是说：

- 当前预览路径没有 H.264 解码
- 当前 UI 不负责 GStreamer 解码 pipeline
- 当前“推流”是板端本地 TCP MJPEG 输出，不是面向公网或跨设备的流媒体协议

### 8. 为什么当前架构对后续扩展更友好

贴着源码看，当前实现已经为后续扩展埋好了几个关键点：

- `VideoService` 用 `channels_` 管理 `cameraId -> VideoChannelStatus`
- `previewPortForCamera()` / `previewUrlForCamera()` 已经把“预览地址生成”收敛到后端
- UI 只认 `previewUrl` 和状态，不认具体设备节点
- `VideoGateway` 只处理命令协议，后续可以并行增加新的控制入口
- `VideoPreviewConsumer` 只是一个消费者，后续完全可以再增加别的本地消费者

因此将来如果要支持：

- 多摄像头
- UI 之外的第二个预览需求方
- 不同 camera 使用不同 preview transport

优先扩展的都应该是后端描述和分发能力，而不是让 UI 重新接管 `/dev/video*`。

## Snapshot / Record Strategy

### Snapshot

拍照不是从预览流截帧，而是由 `health-videod` 单独执行 one-shot pipeline：

```bash
gst-launch-1.0 -e \
  v4l2src device=/dev/video11 num-buffers=1 ! \
  video/x-raw,format=NV12,width=1920,height=1080 ! \
  mppjpegenc ! \
  filesink location=<snapshot_path>
```

当前拍照规格：

- 1920x1080
- JPEG

### Recording

录像时后端使用 `tee` 一路录文件、一路继续提供预览：

- 录像主规格：`1280x720`
- 文件格式：`MP4`
- 预览支路：缩放为 `640x480`，编码为 `MJPEG`，仍通过本地 TCP 提供给 UI

当前录像 pipeline 由 `GstreamerVideoPipelineBackend::buildRecordingCommand()` 生成。

## Runtime Profiles

当前默认规格由 `VideoService::initializeDefaultChannels()` 初始化：

- preview: `640x480 @ 30fps`, pixel format `NV12`
- snapshot: `1920x1080`, pixel format `NV12`
- record: `1280x720 @ 30fps`, pixel format `NV12`, codec `H264`, container `MP4`

对应文件：

- `rk_health_station/rk_app/src/health_videod/core/video_service.cpp`

## Storage Layout

默认媒体目录：

- `/home/elf/videosurv/`

文件命名规则：

- snapshot: `snapshot_yyyyMMdd_HHmmss.jpg`
- record: `record_yyyyMMdd_HHmmss.mp4`

存储目录可以在 UI 侧配置，但真正生效前会由 `health-videod` 做目录合法性和可写性校验。

相关文件：

- `rk_health_station/rk_app/src/health_videod/core/video_service.cpp`
- `rk_health_station/rk_app/src/health_videod/core/video_storage_service.*`

## Startup Behavior

`health-videod` 启动后会自动对 `front_cam` 调一次 `startPreview()`，这样 UI 进入 Video 页面时通常能立刻拿到预览地址和状态。

对应代码：

- `rk_health_station/rk_app/src/health_videod/app/video_daemon_app.cpp`

Bundle 运行时相关脚本：

- `rk_health_station/deploy/bundle/start.sh`
- `rk_health_station/deploy/bundle/start_all.sh`
- `rk_health_station/deploy/bundle/stop.sh`
- `rk_health_station/deploy/bundle/stop_all.sh`
- `rk_health_station/deploy/bundle/status.sh`

## Testing and Validation

当前和摄像链路直接相关的测试包括：

- `video_protocol_test`
- `video_service_test`
- `gstreamer_video_pipeline_backend_test`
- `video_monitor_page_test`
- `video_preview_consumer_test`

板端真实验证已经覆盖：

- `health-videod` 出预览流
- `health-ui` 连接本地 `tcp_mjpeg` 预览并持续渲染帧
- `start_all.sh` / `stop_all.sh` 一键拉起与关闭三进程

## Historical Note

`rk_health_station/docs/testing/video_preview_probe_results.md` 记录的是更早期的 `UDP + MPEGTS + H264` 预览探测结果。  
该文档仍可作为“板端底层 GStreamer 能力探针”参考，但**已经不是当前 UI 实际使用的预览方案**。

当前实际在线方案以本 README 和代码实现为准：

- 后端预览输出：`TCP + MJPEG`
- UI 预览消费：`QTcpSocket + multipart JPEG parser`

## Extension Notes

当前实现已经为后续扩展保留了比较清晰的边界：

- `VideoService` 以 `cameraId -> VideoChannelStatus` 管理通道，未来可扩成多摄像头
- `previewUrlForCamera()` / `previewPortForCamera()` 已经按 camera id 分派
- 预览作为独立数据面存在，未来可以增加第二个消费者而不改 UI 控制协议

如果后续要扩展成多摄像头 / 多需求方，建议沿着下面方向继续：

1. 把 `front_cam` 的固定配置提升为配置表
2. 把 preview transport、port、boundary 做成 camera-level 配置
3. 明确“UI 预览消费者”和“第三方预览消费者”的协议兼容约束
4. 继续保持“设备访问只在 `health-videod`”这一条架构边界
