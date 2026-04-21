# Track Icon Overlay Design

## Goal

在不破坏现有 `health-videod -> health-falld -> health-ui` 架构边界的前提下，把当前多人追踪系统内部已经存在的：

- `trackId`
- 每轨迹独立 `SequenceBuffer`
- 每轨迹独立 LSTM 分类结果

直接可视化成用户可理解的界面表达。

目标效果：

- 视频预览中，每个被追踪到的人头顶显示一个小图标
- 状态栏中的每个人状态项前显示同一个小图标
- 图标与当前运行时轨迹一一对应
- 用户能直观看出“画面中的这个人”对应“状态栏中的这一项”

本设计不引入 ReID，不引入跨重启人物身份，不新增业务级身份系统，只把当前运行时轨迹关系显式展示出来。

## Product Boundary

### In Scope

- 将当前活动轨迹的显示编号暴露给 UI
- 为每个活动轨迹输出一个可绘制锚点
- UI 在视频预览上叠加小图标
- UI 在状态栏列表中显示同一个小图标
- 让图标和当前 `state + confidence` 同步变化

### Out of Scope

- 跨会话保持同一人物长期使用同一个图标
- 人脸识别、ReID、业务身份绑定
- 在视频底层编码流中烧录图标
- 对外部协议新增“真实人物ID”概念

## Current Context

当前项目已经具备以下前提：

1. `health-falld` 内部存在多人追踪能力
2. 每个 `TrackedPerson` 都有独立 `trackId`
3. 每个 `TrackedPerson` 都有独立 45 帧时序缓存
4. LSTM 分类是按轨迹独立执行的
5. `health-ui` 已经能通过 `rk_fall.sock` 消费多人状态
6. `health-ui` 已经有视频预览区域和状态叠加区域

因此本次功能不是“再设计一层身份系统”，而是把现有运行时轨迹绑定关系以可视化方式显示出来。

## Design Principles

1. **不改变摄像头所有权**
   - `/dev/video11` 仍然只属于 `health-videod`

2. **不把显示逻辑耦合进视频后端**
   - 图标不画进视频流
   - 图标由 `health-ui` 作为 overlay 叠加

3. **不创造新的身份语义**
   - `trackId` 仍然只是运行时轨迹 ID
   - UI 图标只表达当前运行时对应关系

4. **优先忠实反映后端真实状态**
   - 如果后端轨迹切换，UI 对应关系也应跟随后端真实变化
   - 不在 UI 私自做“猜测性续接”

5. **可扩展但不过度设计**
   - 当前先支持小图标 + 状态栏对应
   - 为未来扩展成框、箭头、关键点显示预留数据字段，但这次不实现

## Recommended Architecture

推荐方案是：

- `health-falld` 负责输出每个活动轨迹的显示信息
- `health-ui` 负责实际绘制

整体链路如下：

```text
health-videod
  -> TCP MJPEG preview
  -> health-ui

health-falld
  -> classification_batch + overlay metadata
  -> rk_fall.sock
  -> health-ui

health-ui
  -> 预览底图来自 videod
  -> overlay 数据来自 falld
  -> 在画面和状态栏里同步绘制同一个图标
```

### Why This Is Preferred

该方案优于“把图标直接烧进视频流”，因为：

- 不改动 `health-videod` 现有预览稳定性
- UI 样式后续更容易迭代
- 可以按页面需要开关显示
- 继续维持“视频数据面”和“AI/UI 表达面”分离

## Data Model Design

### Internal Model

`health-falld` 内部继续以 `TrackedPerson` 为核心，不改变其作为追踪和分类载体的角色。

新增一个面向 UI 输出的显示结构，统一命名为：

```text
FallOverlayEntry
- trackId
- iconId
- state
- confidence
- anchorX
- anchorY
- bbox
```

其中：

- `trackId`：后端真实轨迹 ID
- `iconId`：UI 显示编号
- `anchorX / anchorY`：头顶图标锚点
- `bbox`：备用绘制和调试字段

### Shared IPC Model

扩展现有 `FallClassificationEntry`，增加这些字段：

- `trackId`
- `iconId`
- `anchorX`
- `anchorY`
- `bboxX`
- `bboxY`
- `bboxW`
- `bboxH`

保持 `state` 和 `confidence` 不变。

这样不需要引入一套全新消息类型，仍然沿用当前 `classification_batch`，只是在每条结果上扩展 overlay 元数据。

## iconId Strategy

### Why Not Show Raw trackId

内部 `trackId` 可能是 `3`、`7`、`14` 这类增长值，直接显示给用户不够友好，也不利于状态栏快速扫描。

### Recommended Rule

`health-falld` 输出两套概念：

- `trackId`：内部真实轨迹 ID
- `iconId`：用户可见显示编号

`iconId` 规则：

1. 对当前活动轨迹池维护一个显示编号池，范围 `1..maxTracks`
2. 新轨迹出现时分配一个空闲 `iconId`
3. 轨迹持续存在时，尽量保持原有 `iconId`
4. 轨迹真正删除后，释放该 `iconId`

这样可以保证：

- 用户看到的是紧凑小编号
- 同一轨迹在其存活期间图标稳定
- 同时不影响后端内部真实 `trackId`

## Anchor Calculation

### Preferred Method

优先使用头部关键点推导锚点：

- nose
- left eye / right eye
- left ear / right ear

如果这些关键点中存在足够可信的点，则取其中心作为头部锚点。

### Fallback Method

若头部关键点不足或不稳定，则退化为：

- `bbox.topCenter`

### Drawing Offset

UI 实际绘制时，不直接把图标压在锚点上，而是：

- `drawY = anchorY - verticalOffset`

避免遮住脸部。

## UI Rendering Design

### Preview Overlay

在 `VideoPreviewWidget` 叠加一个轻量绘制层：

- 对当前 batch 中每条轨迹画一个小圆牌或小胶囊
- 文字为 `iconId`
- 颜色按 `iconId` 固定映射
- 若状态是 `fall`，可给图标加一圈红色强调边框

### Status List

状态栏中每一项显示：

```text
[icon] state confidence
```

例如：

```text
[1] stand 0.98
[2] fall  0.94
[3] lie   0.89
```

状态颜色规则延续当前语义：

- `stand`：绿色
- `lie`：橙色
- `fall`：红色
- `monitoring`：灰色

### Ordering

状态栏顺序固定按当前活动轨迹的左到右顺序排列。

这样视频画面与状态栏更容易建立直觉对应关系。

## Runtime Behavior

### New Track

- 新轨迹出现后立即显示图标
- 若其序列未满 45 帧，则状态显示 `monitoring`

### Mature Track

- 轨迹序列满后，继续滚动输出 `state + confidence`
- 图标和状态栏同步更新

### Temporarily Lost Track

- 若轨迹仍在后端活动池中，则继续保留显示
- UI 对暂时丢失轨迹使用降低透明度的样式

### Removed Track

- 当后端轨迹被删除，对应图标和状态栏项一并移除

## Error Handling

1. **缺少 anchor**
   - 如果 batch 中没有有效 `anchor`，UI 可回退到 `bbox.topCenter`
   - 若连 `bbox` 都没有，则只显示状态栏，不显示头顶图标

2. **UI 收到空 batch**
   - 清除当前 overlay
   - 状态栏显示无人或空状态

3. **部分字段缺失的向后兼容**
   - UI 在解码时应允许旧消息缺失新字段
   - 缺失时只退化显示，不让页面崩溃

4. **trackId / iconId 不一致**
   - UI 不做二次纠正
   - 直接按后端输出为准，避免前后端状态分叉

## Testing Strategy

### Protocol Tests

- 验证 `FallClassificationEntry` 新字段 encode/decode 正确
- 验证旧消息缺失新字段时仍可被 UI 正常解析

### Fall Daemon Tests

- 验证多轨迹 batch 输出包含：
  - `trackId`
  - `iconId`
  - `anchor`
  - `bbox`

- 验证同一轨迹跨帧保持稳定 `iconId`

### UI Tests

- 验证状态栏中图标与状态项一一对应
- 验证收到 batch 后 overlay 数据正确更新
- 验证缺失 anchor 时能回退到 bbox 绘制

### Board Validation

使用多人视频验证：

- 头顶图标与状态栏图标一致
- 轨迹稳定时图标稳定
- 新人进入、旧人离开时图标变化符合预期
- `fall` 状态的人在画面和状态栏中都能快速定位

## Implementation Scope

本次实现分三步：

1. 扩展共享模型和协议，支持 `trackId / iconId / anchor / bbox`
2. 让 `health-falld` 生成并输出这些字段
3. 让 `health-ui` 在状态栏和视频预览上同步绘制

这样能先打通数据，再逐步完善视觉效果，风险最低。

## Affected Files

预计主要改动位置：

- `rk_app/src/shared/models/fall_models.h`
- `rk_app/src/shared/protocol/fall_ipc.h`
- `rk_app/src/shared/protocol/fall_ipc.cpp`
- `rk_app/src/health_falld/app/fall_daemon_app.cpp`
- `rk_app/src/health_ui/ipc_client/fall_ipc_client.cpp`
- `rk_app/src/health_ui/pages/video_monitor_page.cpp`
- `rk_app/src/health_ui/widgets/video_preview_widget.cpp`

本次实现新增一个小型显示编号管理 helper，放在 `health_falld` 内部，用于在轨迹生命周期内稳定分配 `iconId`。
