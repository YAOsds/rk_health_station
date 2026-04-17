# Video Preview Fall Overlay Design

## 1. Goal

在现有 `health-ui` 视频预览页面中，直接在视频画面上叠加实时分类结果，而不是新增独立页面或单独状态区。

目标效果：

- 视频预览区域左上角实时显示分类结果
- 展示内容包括状态与置信度，例如：
  - `stand 0.97`
  - `fall 0.93`
  - `lie 0.88`
- 当当前画面没有可用人体时，显示：
  - `no person`
- `fall` 的视觉强调显著强于 `stand` / `lie`

## 2. Non-Goals

本轮不做：

- 不新增独立 fall 页面
- 不改 `health-videod` 视频预览协议
- 不改 `health-falld` 分类协议语义
- 不把分类结果并入 video IPC
- 不实现复杂动画、轨迹绘制、骨架绘制或 bbox 绘制
- 不做 UI 全局视觉重构

## 3. Current State

当前 UI 已经具备以下基础：

- `VideoMonitorPage` 负责视频页逻辑
- `VideoIpcClient` 负责视频后端状态与预览地址
- `VideoPreviewConsumer` 负责拉取 TCP MJPEG 预览流
- `VideoPreviewWidget` 已经具备 overlay label，可在视频上方叠加一层文本

当前缺口：

- UI 还没有订阅 `rk_fall.sock`
- UI 还不会解析 realtime `classification` 消息
- 视频 overlay 目前只用于 preview 自身状态，不会显示 `stand / fall / lie`
- 没有人体缺失时的 `no person` 展示语义

## 4. Final Direction

采用“双客户端 + 单展示组件”的方案：

- `VideoIpcClient` 继续负责视频链路
- 新增 `FallIpcClient` 负责摔倒分类链路
- `VideoMonitorPage` 统一协调两路数据
- `VideoPreviewWidget` 只负责渲染 overlay，不负责 socket 协议

这是当前最合适的低耦合方案，因为：

- 不把 `health-falld` 强行并入 video backend
- 不让 `VideoPreviewWidget` 直接理解 fall socket
- 不破坏已经打通的 `health-falld realtime classification` 架构

## 5. Data Flow

最终 UI 数据流如下：

```text
health-videod -> rk_video.sock -> VideoIpcClient -> VideoMonitorPage -> VideoPreviewWidget
health-falld -> rk_fall.sock -> FallIpcClient  -> VideoMonitorPage -> VideoPreviewWidget
```

职责分工：

- 视频数据决定“预览画面能否播放”
- 摔倒数据决定“视频上叠加什么状态”
- 页面负责语义映射
- 组件负责视觉呈现

## 6. New UI Client Layer

### 6.1 `FallIpcClient`

新增独立本地 IPC 客户端，职责仅限于：

- 连接 `rk_fall.sock`
- 发送 `{"action":"subscribe_classification"}`
- 读取行分隔 JSON
- 解析 `classification` 消息
- 必要时查询或读取 runtime status

它不负责：

- 颜色和样式
- `no person` 映射规则
- 页面切换逻辑

### 6.2 Suggested signals

`FallIpcClient` 对外建议提供：

- `classificationUpdated(const FallClassificationResult &result)`
- `runtimeStatusUpdated(const FallRuntimeStatus &status)`
- `connectionChanged(bool connected)`
- `errorOccurred(const QString &errorText)`

这样后续别的页面若也要消费 fall 状态，可以复用该 client。

## 7. Page-Level Mapping

### 7.1 `VideoMonitorPage` responsibilities

`VideoMonitorPage` 是协调层，负责：

- 打开视频页时连接 video backend
- 打开视频页时连接 fall backend
- 把 realtime classification 映射成 UI overlay view model
- 把 preview 状态与 fall 状态的显示优先级协调好

### 7.2 Mapping rules

页面层把后端结果映射为四种 UI 展示态：

- `stand`
- `lie`
- `fall`
- `no person`

#### a. when fresh classification exists

如果收到了新的 realtime `classification`：

- `state=stand` -> 显示 `stand <confidence>`
- `state=lie` -> 显示 `lie <confidence>`
- `state=fall` -> 显示 `fall <confidence>`

#### b. when no usable person exists

UI 不要求后端额外发一个 `no_person` 分类标签。

而是由页面端根据现有信号做保守映射：

- 预览正常
- fall client 已连接
- 一段时间内没有新的 classification，或 runtime status 长期停在 `monitoring`

则显示：

- `no person`

这样做的原因：

- 不污染模型标签集合
- 不要求 `health-falld` 专门输出 `no_person`
- UI 语义更灵活

#### c. when fall backend unavailable

如果 `FallIpcClient` 未连接或出错：

- 不显示伪造分类结果
- 可显示轻量状态，如 `status unavailable`
- 但不应覆盖 preview 自身主错误提示

## 8. Widget Rendering

### 8.1 `VideoPreviewWidget`

`VideoPreviewWidget` 保持“只渲染，不连 socket”的原则。

它新增一个正式的分类 overlay 展示接口，例如：

- `setClassificationOverlay(const QString &text, Severity severity)`
- 或者接收一个更完整的 overlay state struct

该组件内部维护独立于 preview 文案的分类 overlay 层。

### 8.2 Visual rules

建议使用现有风格基础上增强，而不是重做页面：

- `fall`
  - 红色强调
  - 更大字体
  - 半透明深色底
  - 最醒目
- `lie`
  - 橙色或黄色强调
  - 次于 `fall`
- `stand`
  - 绿色或较轻提示
  - 视觉更克制
- `no person`
  - 灰色标签
  - 明确但不过度告警

建议叠加文本格式：

- `fall 0.93`
- `lie 0.88`
- `stand 0.97`
- `no person`

### 8.3 Priority with preview state

如果视频预览本身不可用：

- preview 自身错误提示优先
- classification overlay 可隐藏

如果视频预览正常：

- classification overlay 持续显示

## 9. Verification Plan

### 9.1 Host-side tests

至少补三类测试：

1. `FallIpcClient` 协议测试
   - 模拟 `QLocalServer`
   - 推送 `classification` JSON
   - 断言 client 成功解码并发信号

2. `VideoPreviewWidget` 展示测试
   - 注入不同 overlay state
   - 断言文本正确：
     - `stand 0.97`
     - `fall 0.93`
     - `no person`

3. `VideoMonitorPage` 页面联动测试
   - 模拟收到 classification
   - 断言页面将结果传递给 preview overlay

### 9.2 Board-side validation

在 RK3588 板上验证：

- 打开 `health-ui` 视频页后仍能正常看到视频预览
- 分类结果推送时，视频左上角实时显示 `state + confidence`
- 当没有可用人体时，显示 `no person`
- 预览链路、原有页面行为不回退

### 9.3 Controlled validation

为避免现场“刚好没人”导致无法闭环，需要保留受控验证路径：

- 真实板端 `health-ui`
- 真实板端 `health-falld`
- 受控 classification 或受控 analysis 输入
- 验证 overlay 跟随变化

## 10. Success Criteria

本轮 UI 需求只有满足以下条件才算完成：

- `health-ui` 视频预览仍正常工作
- 视频画面可叠加显示：
  - `stand + confidence`
  - `fall + confidence`
  - `lie + confidence`
  - `no person`
- `fall` 的视觉强调显著强于其他状态
- overlay 跟随 realtime `classification` 更新
- `health-videod`、`health-falld`、video IPC、fall IPC 边界均不被破坏

## 11. Future Compatibility

该设计只依赖 realtime `classification` 协议，不依赖当前 LSTM 的内部实现。

因此未来若分类模型从 `LSTM -> RKNN` 切换到其他同输出后端，只要 `health-falld` 继续输出：

- `type=classification`
- `state`
- `confidence`

则 UI overlay 无需重写。

