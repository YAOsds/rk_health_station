# Multi-Person Fall Detection Design

## Goal

在现有 `health-videod -> analysis socket -> health-falld -> rk_fall.sock -> health-ui` 链路不被破坏的前提下，把当前“多人输入、单人判定”的摔倒检测系统升级为“多人输入、多人独立判定”的运行时系统：

- 对同一视频中的最多 5 个人同时进行摔倒分类
- 为每个人建立独立临时轨迹与独立 45 帧序列
- 分别输出每个人的分类结果 `state + confidence`
- 在 Qt 预览区域同时显示多个人的状态列表
- `fall` 状态明显标红
- `person_id` 仅作为后端内部临时轨迹标识，不作为产品层面的长期身份概念

## Product Boundary

### In Scope

- 单次进程运行期内的多人轨迹维护
- 最多 5 人的同时分类
- 多人分类结果的批量 IPC 输出
- Qt 端的多人状态叠加展示
- 后端日志打印当前人数与每个人状态

### Out of Scope

- 跨天、跨会话、跨重启保持人物身份一致
- 人脸识别、重识别、业务身份绑定
- 数据库中长期保存 `person_id`
- 每个人贴身框绘制或关键点可视化
- 专业级 MOT/ReID 能力

## Current System Limitation

当前 `health-falld` 的流程是：

1. pose 模型对每帧输出 `QVector<PosePerson>`
2. `TargetSelector` 仅选择一个最高分的人
3. 单个 `SequenceBuffer<PosePerson>(45)` 累积序列
4. LSTM 对这个单序列做分类
5. `fall IPC` 只发布一条 `classification`
6. UI 也只显示一条 overlay 文本

这意味着当前系统虽然能接收多人输入，但后续分类链路实际只服务一个人，且多人交叉时序列容易串人。

## Design Summary

采用“轻量多目标跟踪 + 每轨迹独立 45 帧 + 批量分类 IPC + 多行 overlay”的方案：

- 在 `health-falld` 内新增轻量轨迹管理层，替代当前单目标 `TargetSelector`
- 每条轨迹独立维护自身的 `SequenceBuffer<PosePerson>`
- 每条轨迹独立调用现有 LSTM 分类器
- 对外通过新的 `classification_batch` 消息一次性发布当前所有活动轨迹的结果
- Qt 端将单条 overlay 升级为多人状态列表，但不显示 `person_id`

该方案保留现有模型输入协议、现有 analysis socket 边界和现有 LSTM 方案，只扩展运行时编排和 UI 表达层。

## Architecture

### 1. Frame Ingest Layer

`health-videod` 与 analysis socket 协议保持不变：

- `health-videod` 继续发送分析帧
- `AnalysisStreamClient` 保持不变
- `RknnPoseEstimator` 继续返回多人 `QVector<PosePerson>`

也就是说，多人能力的切入点位于 `health-falld` 的 pose 结果消费层，而不是视频采集层。

### 2. Track Management Layer

新增 `TrackManager` / `TrackedPerson` 概念，替代“每帧只选一个主目标”的逻辑。

每条活动轨迹包含：

- `trackId`：仅运行时使用的临时整型或字符串 ID
- `lastBox`：上一帧 bbox
- `lastPose`：上一帧关键点
- `lastUpdateTs`：最近更新时间
- `missCount`：连续未匹配帧数
- `SequenceBuffer<PosePerson>`：该轨迹独立 45 帧时序
- `lastClassificationState`
- `lastClassificationConfidence`
- `hasFreshClassification`

系统维护一个最多 5 条活动轨迹的池。

### 3. Classification Layer

每条轨迹独立走同一套分类逻辑：

- 独立积累自身 45 帧关键点序列
- 序列未满时，状态视为 `monitoring`
- 序列满 45 帧后，每来一帧新匹配结果就做一次滚动分类
- 使用现有 LSTM RKNN 模型，不修改其输入形状定义

这样多人检测不会改变模型本身，只改变“谁的帧进哪个序列”。

### 4. IPC Layer

保留现有单条 `classification` 的兼容能力，同时新增 `classification_batch`：

- 旧客户端仍可继续消费单条消息（如果需要可保留向后兼容）
- 新 UI 客户端优先消费批量消息

批量消息用于表达“当前这帧/当前时刻系统已知的所有活动轨迹结果”。

### 5. UI Layer

UI 不显示内部 `trackId`，而是显示多人状态列表：

- 左上角纵向列表
- 最多 5 行
- 每行一个人的 `state + confidence`
- `fall` 红色突出
- `lie` 橙色
- `stand` 绿色
- 当前无人时显示 `no person`

UI 仍然通过独立 `FallIpcClient` 接收 fall 数据，不与视频 IPC 混合。

## Track Matching Strategy

### Why Not Full MOT

产品边界明确不需要长期人物身份，因此无需引入重型 MOT 或 ReID。

目标仅是：

- 在短时运行中尽量稳定地把连续帧关联到同一个临时轨迹
- 避免多人同时出现时状态互相串扰

### Matching Rule

采用轻量级基于几何关系的匹配：

- 候选输入：当前帧全部 `PosePerson`
- 历史对象：当前所有活动轨迹
- 匹配特征：
  - bbox IoU
  - bbox 中心点距离
- 匹配流程：
  1. 先过滤掉关键点数不足或 bbox 异常的检测结果
  2. 计算每个轨迹与每个检测的匹配分数
  3. 仅接受满足最小 IoU 或最大中心距离阈值的组合
  4. 用贪心方式从最佳匹配开始分配
  5. 未匹配检测尝试创建新轨迹
  6. 未匹配轨迹增加 `missCount`
  7. `missCount` 超阈值时删除轨迹

### Capacity and Lifecycle

建议默认阈值：

- `maxTracks = 5`
- `maxMissedFrames = 10`

行为规则：

- 轨迹池未满：可创建新轨迹
- 轨迹池已满：只保留更稳定、更近期、更高质量的轨迹
- 短暂遮挡：允许轨迹继续保留若干帧
- 长时间丢失：轨迹删除，UI 对应一行消失

## Per-Track Sequence Lifecycle

每条轨迹拥有自己独立的 45 帧序列：

1. `new`：轨迹创建，序列为空
2. `warming_up`：持续追加姿态帧，直到满 45 帧
3. `active`：序列满后，每次新帧进入触发一次分类
4. `stale`：若暂时没匹配到新帧，可短时间保留最近状态
5. `removed`：连续丢失超过阈值，轨迹删除

分类结果只属于该轨迹自身，不会与别的轨迹共享序列。

## Data Model Changes

### Internal Model

新增内部结构（名称可在实现时微调）：

- `TrackedPerson`
- `TrackUpdateResult`
- `MultiPersonClassificationSnapshot`

### Shared IPC Model

新增共享模型：

- `FallClassificationEntry`
  - `state`
  - `confidence`
  - 可选内部字段：`track_id`（仅作为兼容未来扩展，不要求 UI 展示）
- `FallClassificationBatch`
  - `cameraId`
  - `timestampMs`
  - `personCount`
  - `results`

如果实现阶段发现 UI 后续需要稳定排序，可在 batch 中保留 `track_id` 作为非展示字段，但它不构成产品定义中的人物身份。

## IPC Contract

### Batch Message

建议新增：

```json
{
  "type": "classification_batch",
  "camera_id": "front_cam",
  "ts": 1776367000000,
  "person_count": 3,
  "results": [
    {"state": "stand", "confidence": 0.91},
    {"state": "fall", "confidence": 0.96},
    {"state": "lie", "confidence": 0.88}
  ]
}
```

### Compatibility Policy

- `get_runtime_status` 继续存在
- 单条 `classification` 可以先保留，避免打断旧代码路径
- 新 UI 优先订阅并解析 `classification_batch`

## Runtime Status Semantics

`FallRuntimeStatus` 仍保留单机汇总视角，而不是变成每人一个状态：

- `latest_state`：可表达总体状态，例如 `monitoring` / `multi_person_active`
- `latest_confidence`：可表达最高风险结果或主摘要

更详细的多人状态由 `classification_batch` 承担。

## Logging

日志不打印长期身份语义，只打印当前批量结果摘要，例如：

```text
classification_batch camera=front_cam count=3 states=[stand:0.91,fall:0.96,lie:0.88] ts=1776367000000
```

原则：

- 记录当前人数
- 记录每个人当前状态和置信度
- 不向产品层暴露“这个人今天/明天是不是同一个人”的错误语义

## UI Design

### Rendering Behavior

`VideoPreviewWidget` 从“单文本 overlay”扩展为“多行 overlay 列表”：

- 最多 5 行
- 建议左上角纵向堆叠
- 行间距固定，列表背景半透明
- 排序规则优先简单稳定，例如从左到右

### Visual Severity

- `fall`：红底、高权重、最大字号
- `lie`：橙底、中高权重
- `stand`：绿底、正常字号
- `monitoring`：灰底、弱化
- `no person`：灰色弱提示

### Empty State

- 当前无任何活动轨迹，显示 `no person`
- 不再沿用“单结果过期 -> no person”的单人逻辑，而是基于 batch 是否为空判断

## Component Responsibility

### health-videod

- 不改协议
- 不做多人跟踪
- 只负责输出分析帧

### health-falld

- 负责多人轨迹维护
- 负责每轨迹独立时序缓冲
- 负责每轨迹独立分类
- 负责生成批量结果并广播

### shared protocol/model

- 扩展多人批量模型与序列化
- 保持旧协议可兼容

### health-ui

- `FallIpcClient`：接收 `classification_batch`
- `VideoMonitorPage`：将批量结果映射为 overlay 视图模型
- `VideoPreviewWidget`：只负责渲染多人列表，不处理 socket 或业务逻辑

## Testing Strategy

### Track Layer Tests

- 两人稳定出现时，生成两条独立轨迹
- 两人短时间交叉但未完全遮挡时，轨迹尽量保持稳定
- 一人离场后，轨迹在超时后删除
- 超过 5 人时，只保留前 5 条有效轨迹

### Backend Integration Tests

- 多轨迹能分别积累独立 45 帧
- 满 45 帧后能输出多条分类结果
- `classification_batch` 的编码/解码正确
- `FallGateway` 能广播 batch 消息

### UI Tests

- 收到 batch 后显示多行 overlay
- `fall` 行具备最高严重级别样式
- 空 batch 或无活动轨迹时显示 `no person`
- 不显示 `person_id`

### Board Validation

- RK3588 板端同时出现多人时，`health-falld.log` 输出批量状态
- UI 叠加区同时展示多条状态
- `fall` 在多人列表中仍明显突出

## Implementation Plan Boundary

本设计对应的实现工作建议拆为四步：

1. 后端内部改为多轨迹、多序列，但先不改 UI
2. 扩展 shared model / fall IPC 为 batch 输出
3. Qt overlay 改为多人列表展示
4. 板端联调与阈值调参

这样可以保持每一步都有明确回归验证点，并避免一次性大范围耦合修改。

## Risks and Mitigations

### Risk 1: Track Swapping

多人交叉或遮挡时，轻量匹配仍可能短时串轨。

Mitigation:
- 使用 IoU + 中心距离双重约束
- 删除与创建轨迹时保持保守阈值
- 优先先实现稳定场景，再板端调参

### Risk 2: UI Flicker

轨迹频繁出现/消失时，多行 overlay 可能跳动。

Mitigation:
- 允许轨迹短暂 stale 保留
- UI 只显示当前活动轨迹，排序规则保持稳定

### Risk 3: Coupling Expansion

多人功能可能诱导把跟踪逻辑塞进 UI 或 video daemon。

Mitigation:
- 跟踪逻辑只放在 `health-falld`
- `VideoMonitorPage` 只消费 batch 结果
- `VideoPreviewWidget` 只做渲染

## Final Recommendation

采用以下最终方案：

- 轻量多目标跟踪
- 最多跟踪 5 人
- 每轨迹独立 45 帧 LSTM 输入
- 批量 IPC 输出 `classification_batch`
- UI 多行 overlay 展示
- `person_id` 仅作内部临时变量，不作为产品层的身份概念

这是在当前产品边界、现有代码结构和 RK3588 部署约束下，复杂度与收益最平衡的实现路线。
