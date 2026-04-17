# ByteTrack Fall Detection Design

## Goal

在不改变现有 `health-videod -> analysis socket -> health-falld -> rk_fall.sock -> health-ui` 职责边界的前提下，将当前基于简化几何匹配的多人摔倒检测链路升级为基于 ByteTrack 的多人短时稳定跟踪链路，使同屏最多 5 人能够分别维护独立关键点序列、独立动作分类状态，并在 Qt 预览区域稳定显示多人 `state + confidence`。

## Scope

### In Scope

- 在 `health-falld` 内引入 ByteTrack 风格多目标跟踪
- 直接复用 `YOLOv8-pose.rknn` 输出的 `bbox + score + 17 keypoints`
- 为每条轨迹维护独立 45 帧关键点序列
- 为每条轨迹维护独立动作分类状态与事件确认状态
- 输出多人分类 batch 到 `health-ui`
- 板端 RK3588 运行验证与参数调优

### Out of Scope

- 外观特征模型 / 任何新增训练模型
- 跨会话、跨视频、跨天的身份保持
- UI 展示 `track_id`
- 在 `health-videod` 中嵌入 tracking 或 action 逻辑
- 使用伪关键点补帧

## Current Problem

当前工程中的多人链路已经具备：

- pose 模型输出多人 `PosePerson`
- 多人 batch IPC 输出与 UI 多行 overlay
- 基于轨迹的独立 `SequenceBuffer<PosePerson>`

但仍存在两个根本问题：

1. `TrackManager` 仍是简化版几何贪心匹配，仅使用 IoU 和中心距离，遇到多人交叉、遮挡、检测分数波动时容易串轨。
2. 动作确认上下文不是完全 per-track 隔离，存在不同人物状态互相影响的风险。

本设计的目标不是追求长期身份，而是让同一视频中的短时轨迹足够稳定，使 LSTM 输入不再被不同人物混入。

## Architecture Boundary

### health-videod

- 保持现有分析帧输出方式不变
- 不感知 tracking、LSTM、多人业务状态
- 仍只负责采集与 analysis frame 发送

### health-falld

本期唯一核心改造点。内部职责拆分为：

- `pose estimator`：调用 `YOLOv8-pose.rknn` 输出 `QVector<PosePerson>`
- `byte tracker`：完成多人跨帧关联与轨迹生命周期管理
- `track action context`：维护每条轨迹的 45 帧关键点序列、动作分类结果、事件确认状态
- `gateway`：对外发布多人状态 batch 与运行时摘要

### health-ui

- 继续只消费多人分类结果
- 不理解 tracking 细节
- 在视频预览区域叠加多人 `state + confidence`
- `fall` 标红，`no person` 在无人时显示

### Key Boundary Rules

- `track_id` 仅存在于 `health-falld` 内部
- UI 与日志不表达长期身份语义
- 本期不引入新增 tracking 模型、不改动 detector 模型、不改变 socket 边界

## End-to-End Data Flow

1. `health-videod` 发送 analysis frame。
2. `health-falld` 使用 `YOLOv8-pose.rknn` 推理，得到每帧多人 `PosePerson`。
3. `ByteTracker` 对本帧 detections 与历史轨迹执行关联。
4. 每条稳定轨迹更新自己的 `TrackActionContext`。
5. 序列满足条件的轨迹调用 `LSTM.rknn` 进行动作分类。
6. `health-falld` 汇总当前所有活动轨迹的最终状态，发布 `classification_batch`。
7. `health-ui` 按既有协议渲染多人状态 overlay。

## Tracking Design

### Tracker Input

直接复用现有 `PosePerson`：

- `bbox = person.box`
- `det_score = person.score`
- `keypoints = person.keypoints`

不增加新的 person detector，也不拆分成独立 tracking 模型。

### Why ByteTrack

ByteTrack 的核心价值是使用两阶段关联：

- 第一阶段用高分检测关联活动轨迹
- 第二阶段用低分检测补关联未匹配轨迹

这比当前的“单阶段贪心几何匹配”更能抵抗：

- 短时遮挡
- 框质量波动
- 轻度漏检
- 多人接近时的轨迹中断

### Tracker States

每条轨迹遵循三态生命周期：

- `Tracked`：当前帧成功匹配到真实检测
- `Lost`：短时未匹配，但仍在等待恢复
- `Removed`：超过超时阈值，彻底删除

### Matching Pipeline

每一帧按如下顺序处理：

1. 过滤异常检测：关键点不足、bbox 非法、面积过小的检测直接丢弃。
2. 按 `det_score` 将检测划分为 `high-score` 和 `low-score` 两组。
3. 对 `Tracked` 和 `Lost` 轨迹执行状态预测。
4. 第一轮：用 `high-score` detections 关联当前轨迹。
5. 第二轮：用 `low-score` detections 补关联第一轮未匹配轨迹。
6. 对仍未匹配的高分检测创建新轨迹。
7. 对未匹配轨迹更新 lost 状态；超时后删除。

### Matching Cost

本期保持 ByteTrack 主干纯净，不引入外观特征。匹配代价由以下组成：

- 预测框与当前框的 IoU
- 中心位移约束
- 最小框面积与最小检测质量门控

其中“中心位移过大直接拒绝”只作为轻量几何门控，而不是新的复杂 scoring 系统。

### Capacity

- `max_tracks = 5`
- 超过容量时，不创建新轨迹
- 优先保持已稳定存在的轨迹，避免画面人数短时抖动导致频繁替换

## Per-Track Action Context

每条轨迹都拥有独立的运行上下文：

- `latestPose`
- `SequenceBuffer<PosePerson>(45)`
- `lastClassificationState`
- `lastClassificationConfidence`
- `event confirmation state`
- `lastUpdateTs`
- `track lifecycle state`

### Critical Rule

- 模型实例可以共享
- 分类状态机与事件确认状态绝不能共享

也就是说：

- `LSTM` RKNN runner 可以是共享对象
- 但“某条轨迹连续多少次 fall-like”“是否触发 fall event”必须挂在该轨迹自己的上下文中

### Sequence Write Policy

只有当轨迹当前帧真实匹配到 detection 时，才允许写入序列：

- `Tracked`：压入本帧 `PosePerson`
- `Lost`：暂停写入
- `Removed`：销毁序列

禁止把 Kalman 预测框或任何伪关键点写入 LSTM 输入。

### Classification Trigger Policy

仅当以下条件同时成立时，才对该轨迹触发 LSTM 分类：

- 轨迹状态为 `Tracked`
- 当前帧关键点质量满足最小要求
- `SequenceBuffer` 已满 45 帧

否则该轨迹保持 `monitoring`。

## Runtime Output Policy

### Batch Output

`health-falld` 继续对外发布多人 `classification_batch`，每一项表示一个当前有效轨迹的最终状态：

- `state`
- `confidence`

保持 UI 只消费最终结果列表，而不接触 tracking 内部状态。

### Ordering

为了让 UI 展示稳定，batch 中的条目按 `bbox.center().x()` 从左到右排序。这样即使内部 `track_id` 不展示，用户仍能获得相对稳定的行顺序。

### No Person

当本帧没有任何活动轨迹时，输出空 batch，由 UI 按现有规则显示 `no person`。

### Logging

日志只打印当前人数与状态摘要，例如：

```text
classification_batch camera=front_cam count=3 states=[stand:0.91,fall:0.96,lie:0.88] ts=1776367000000
```

不输出长期身份语义，不承诺跨时段人物一致性。

## Module Layout

推荐在 `rk_app/src/health_falld` 中形成如下结构：

- `tracking/byte_track_detection.h`
  - 承载 `bbox + score + PosePerson`
- `tracking/byte_track_state.h`
  - 轨迹状态、计数器、时间戳
- `tracking/kalman_filter.*`
  - 轨迹预测
- `tracking/association.*`
  - IoU cost、匹配与阈值过滤
- `tracking/byte_tracker.*`
  - ByteTrack 主流程
- `tracking/track_action_context.h`
  - 每轨迹 45 帧缓存、分类状态、事件确认器

`FallDaemonApp` 只保留 orchestration 职责：

- 获取 pose detections
- 调用 `ByteTracker`
- 对活动轨迹执行动作分类
- 汇总发布 batch

不再在 `FallDaemonApp` 中直接实现匹配细节。

## Runtime Configuration

以下参数进入 runtime config，而不是散落硬编码：

- `max_tracks = 5`
- `sequence_length = 45`
- `track_high_thresh = 0.35`
- `track_low_thresh = 0.10`
- `new_track_thresh = 0.45`
- `match_thresh = 0.80`
- `lost_timeout_ms = 800`
- `min_valid_keypoints = 8`
- `min_box_area`

这些参数允许板端回放调优，但默认值应能直接支撑单人、双人和三人场景验证。

## Test Strategy

### Unit Tests

至少覆盖以下行为：

- 匹配代价与门控逻辑正确性
- `Tracked -> Lost -> Removed` 生命周期转换
- `Lost -> Tracked` 恢复路径
- `max_tracks = 5` 容量限制
- 每轨迹独立事件确认状态
- `sequence` 仅在真实匹配时写入

### Integration Tests

通过构造多帧 `PosePerson` 序列模拟：

- 单人持续出现
- 两人并行移动
- 两人交叉经过
- 一人短时遮挡后恢复
- 一人离场、一人入场
- 无人 -> 单人 -> 多人切换

验证重点：

- 轨迹数量变化是否正确
- 关键点序列是否保持归属稳定
- 分类状态是否不会串到其他人身上

### UI Verification

验证 UI 最终呈现而非 tracking 细节：

- 单人显示单条状态
- 多人显示多条状态
- `fall` 标红
- 无人显示 `no person`
- 行顺序不频繁跳变

### Board Validation

在 RK3588 上观察：

- `health-videod`、`health-falld`、`health-ui` 长时间运行稳定性
- socket 链路是否稳定
- 多人同屏时状态更新是否可接受
- 单人场景效果不退化

## Acceptance Criteria

以下条件同时满足，视为方案成功：

- 单人场景较当前版本无明显退化
- 多人同屏时不同人物状态不再明显串扰
- 两人交叉时轨迹稳定性相对当前版本有明显改善
- 每个人的 45 帧序列、分类状态、事件确认状态完全独立
- 板端可持续运行，UI 可稳定显示多人状态
- 改动集中在 `health-falld`，不破坏 `health-videod` 与 `health-ui` 的职责边界

## Risks and Non-Goals

- ByteTrack 仍然不是长期身份方案，复杂遮挡或长时间出画重入时仍可能重新编号
- 本期不追求专业级 MOT 指标，只追求“足够稳地服务摔倒序列分类”
- 若未来现场数据证明 ByteTrack 在复杂交叉场景仍不足，再单独立项评估后续增强方案；这不属于本期方案范围
