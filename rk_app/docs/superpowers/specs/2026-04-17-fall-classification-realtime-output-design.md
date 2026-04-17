# RK3588 Fall Classification Realtime Output Design

## 1. Goal

在保持现有 `health-videod -> analysis socket -> health-falld` 架构不变的前提下，把当前已经能在 RK3588 上运行的 `yolov8 pose -> LSTM RKNN` 分类链路升级为真正可消费的实时输出系统：

- `health-falld` 对每个有效分类窗口持续输出 `stand / fall / lie`
- 本地 `rk_fall.sock` 支持订阅实时分类结果流
- `health-falld.log` 同步输出结构化分类日志
- 原有 `get_runtime_status` 能力保留
- 原有 `fall_event` 策略层能力保留并与分类流并存

## 2. Non-Goals

本轮不做以下事情：

- 不改 `health-videod` 的 camera ownership 或 analysis socket 协议
- 不改 pose 模型输入输出契约
- 不新增 UI 展示
- 不把实时结果改成网络服务或跨机协议
- 不把 ST-GCN 重新拉回当前主线；当前主线仍然是 `LSTM -> RKNN`

## 3. Current State

当前系统已经具备以下能力：

- `health-videod` 持续向 analysis socket 输出帧
- `health-falld` 能接收 analysis frame，并在板端成功加载 pose RKNN 与 LSTM RKNN
- `health-falld` 内部已经会更新 `runtimeStatus_.latestState` / `latestConfidence`
- 外部当前只能通过 `{"action":"get_runtime_status"}` 轮询获取最近状态

当前缺口是：

- 没有面向客户端的实时分类推送流
- 没有正式的分类消息对象与协议
- 没有把每次分类结果稳定写入日志

## 4. Final Direction

采用单 socket 扩展方案，复用现有 `rk_fall.sock`，新增实时分类订阅能力，不新增独立 socket。

### 4.1 Why this direction

相对于继续走轮询或增加第二个 socket：

- 更符合“实时输出”的目标
- 不改变既有部署结构
- 后续 UI 直接复用这一输出流即可
- 分类层与事件层可通过消息类型分层，边界清晰

## 5. Output Model

系统对外将同时维护三类输出：

### 5.1 Runtime status

用于健康检查与调试：

- `camera_id`
- `input_connected`
- `pose_model_ready`
- `action_model_ready`
- `current_fps`
- `last_frame_ts`
- `last_infer_ts`
- `latest_state`
- `latest_confidence`
- `last_error`

该能力继续通过：

- `{"action":"get_runtime_status"}`

获取。

### 5.2 Classification stream

这是本次新增的正式实时输出：

- 每次 45 帧有效窗口得到分类结果时都推送一次
- 不做“仅状态变化才推送”的抑制
- 正式输出的分类值为：
  - `stand`
  - `fall`
  - `lie`
- 非有效窗口阶段仍只更新 `runtime_status=monitoring`，但不向订阅流发送伪分类结果

建议消息结构：

```json
{
  "type": "classification",
  "camera_id": "front_cam",
  "ts": 1776356876397,
  "state": "fall",
  "confidence": 0.93
}
```

### 5.3 Fall event stream

保留现有策略层输出语义：

- 分类流是“每窗口输出”
- 事件流是“满足策略后确认输出”

建议消息结构继续沿用现有 `FallEvent` 语义：

```json
{
  "type": "fall_event",
  "event_id": "",
  "camera_id": "front_cam",
  "ts_start": 0,
  "ts_confirm": 1776356876397,
  "event_type": "fall_confirmed",
  "confidence": 0.93,
  "snapshot_ref": "",
  "clip_ref": ""
}
```

## 6. Socket Protocol

### 6.1 Existing request retained

保留：

```json
{"action":"get_runtime_status"}
```

服务端返回单条 JSON status。

### 6.2 New realtime subscription request

新增：

```json
{"action":"subscribe_classification"}
```

服务端行为：

- 保持该 `QLocalSocket` 连接不断开
- 把该连接登记为订阅客户端
- 之后每次有新分类结果，就向该连接推送一条 `classification` JSON 行
- 如果同轮更新触发 `fall_event`，再额外推送一条 `fall_event` JSON 行

### 6.3 Message framing

沿用当前本地 socket 的行分隔风格：

- 每条 JSON 末尾追加 `\n`
- 客户端按行读取消息

这样可以避免引入新的 framing 机制，保持调试简单。

## 7. Logging Behavior

`health-falld.log` 需要成为实时验证分类效果的第二正式出口。

### 7.1 Classification log

每次分类窗口得到结果时写一条结构化日志：

```text
classification camera=front_cam state=fall confidence=0.93 ts=1776356876397
```

### 7.2 Event log

当策略层确认事件时再写一条：

```text
fall_event camera=front_cam type=fall_confirmed confidence=0.93 ts=1776356876397
```

### 7.3 Error log

模型加载失败、推理失败、订阅广播失败等仍走已有错误路径，不与分类日志混杂成同一种记录。

## 8. Internal Architecture

本次改动只发生在 `health-falld` 内部，不改变上游或共享输入契约。

### 8.1 `action/*`

保持职责不变：

- 输入：`QVector<PosePerson>` 时序序列
- 输出：`ActionClassification { label, confidence }`

不让分类器直接知道 socket、日志、事件推送。

### 8.2 `domain/fall_detector_service.*`

在当前 `FallDetectorResult` 基础上，明确区分：

- 当前分类结果
- 当前对外显示状态
- 可选策略事件

建议演进后的语义：

- `classificationState`
- `classificationConfidence`
- `event`

这样 `FallDaemonApp` 不必猜测“这次是否有正式分类结果”。

### 8.3 `protocol/fall_ipc.*`

新增一个独立 classification message model，用于：

- `classificationResultToJson(...)`
- `classificationResultFromJson(...)`

不把 classification 硬塞进 runtime status，也不复用 fall event 结构。

### 8.4 `ipc/fall_gateway.*`

新增职责：

- 识别 `subscribe_classification`
- 保存订阅连接列表
- 广播 classification 消息
- 广播 fall_event 消息
- 清理断开的订阅 socket

保留职责：

- `get_runtime_status` 请求响应

### 8.5 `app/fall_daemon_app.cpp`

在每次有效窗口完成分类后执行以下顺序：

1. 调用 `detectorService_.update(...)`
2. 更新 `runtimeStatus_.latestState/latestConfidence`
3. 向 `gateway_` 广播 classification
4. 写 classification 日志
5. 如果 `result.event` 存在，再广播 event 并写 event 日志

## 9. Behavioral Rules

### 9.1 Before sequence is full

- `runtime_status.latest_state = monitoring`
- 不推送 classification

### 9.2 When person is missing or invalid

- 清空 buffer
- `runtime_status.latest_state = monitoring`
- 不推送 classification

### 9.3 When model inference succeeds

- 每个有效窗口都推送 classification
- `runtime_status.latest_state/latest_confidence` 与本轮 classification 对齐

### 9.4 When inference fails

- 更新 `last_error`
- `runtime_status.latest_state = monitoring`
- 不推送 classification

## 10. Verification Plan

必须覆盖三层验证。

### 10.1 Protocol / unit tests

新增或扩展测试以覆盖：

- classification JSON 编解码
- `subscribe_classification` 请求处理
- `FallGateway` 对订阅客户端的广播行为

### 10.2 Host-side end-to-end tests

在本机测试中：

- 启动 `FallDaemonApp`
- 构造 analysis frame 输入
- 连接 `rk_fall.sock`
- 发送 `subscribe_classification`
- 断言收到持续 classification 消息
- 断言 `get_runtime_status` 仍然可用

### 10.3 Board validation

在 RK3588 板上：

- 启动 `health-videod + health-falld`
- 订阅 `rk_fall.sock`
- 观察持续输出的 `stand/fall/lie`
- 检查 `logs/health-falld.log` 中同步出现 classification 日志
- 确认原有 runtime status 查询未回退

## 11. Success Criteria

只有同时满足以下条件，才算这轮需求完成：

- 板端 `health-falld` 稳定运行
- `rk_fall.sock` 可订阅实时 classification 输出
- 每个有效分类窗口都会输出一次 `stand/fall/lie`
- `health-falld.log` 同步出现分类日志
- `get_runtime_status` 保持可用
- `fall_event` 能与 classification 并存，不互相覆盖
- 不修改 `health-videod` 与 analysis socket 协议

## 12. Future Compatibility

当前主线虽然是 `LSTM -> RKNN`，但本次输出层设计只依赖共享的 `17 keypoints x 45 frames` 时序分类边界。

因此未来如果 `ST-GCN -> RKNN` 重新打通，只需要替换 `ActionClassifier` 后端，而不需要重写：

- realtime classification protocol
- classification logging
- gateway subscription model
- board-side consumer logic

