# 2026-04-24 RK3588 Analysis SHM 性能结果

## 背景

本文记录一次新的 RK3588 板端对比测试，对比对象为：

- `main` 基线 bundle：`/home/elf/rk3588_bundle_main_compare`
- 当前共享内存候选 bundle：`/home/elf/rk3588_bundle_candidate`

本次对比关注的是 test video input 模式下，从视频输入到跌倒分析这一条传输链路的性能表现。

## 测试环境

- 日期：`2026-04-24`
- 板子：`elf@192.168.137.179`（`elf2-desktop`）
- 输入视频：`/home/elf/Videos/video.mp4`
- 基线来源：主仓库 `main`，commit `45cd021`
- 候选来源：worktree `feature-analysis-shm-transport`，`HEAD=1167ec2`
- 候选说明：板子上部署的 candidate bundle 来自当前 worktree 状态，包含 SHM transport 后续修复以及后端运行时/性能日志相关的本地未提交改动

正式测基线前，我先发现板子上的 `/home/elf/rk3588_bundle_main_compare` 目录并不完整，缺少 `bin/healthd`。因此在测量前，我先把本地完整的 main bundle 重新同步到了该目录。

## 测量方法

本次复用了已有的板端测量脚本：

```bash
python3 deploy/tests/measure_rk3588_test_mode_latency.py \
  --host elf@192.168.137.179 \
  --password elf \
  --bundle-dir /home/elf/rk3588_bundle_main_compare \
  --video-file /home/elf/Videos/video.mp4

python3 deploy/tests/measure_rk3588_test_mode_latency.py \
  --host elf@192.168.137.179 \
  --password elf \
  --bundle-dir /home/elf/rk3588_bundle_candidate \
  --video-file /home/elf/Videos/video.mp4
```

脚本采集的核心指标包括：

- `startup_classification_latency_ms`：从 playback start 到 first classification 的时延
- `classification_stage_latency_ms`：从 first analysis frame 到 first classification 的时延
- `producer_cpu_pct`：`health-videod` 的 CPU 占用
- `consumer_cpu_pct`：`health-falld` 的 CPU 占用
- `producer_dropped_frames`

在同一块板子、同一份视频文件条件下，`main` 和 `candidate` 各测量 `3` 次。

## 原始结果

### Main 基线

| Run | startup_classification_latency_ms | classification_stage_latency_ms | producer_cpu_pct | consumer_cpu_pct | producer_dropped_frames |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 1318 | 1386 | 7.5 | 26.7 | 0 |
| 2 | 1262 | 1333 | 7.2 | 31.7 | 0 |
| 3 | 1342 | 1393 | 7.2 | 33.0 | 0 |

### SHM 候选版本

| Run | startup_classification_latency_ms | classification_stage_latency_ms | producer_cpu_pct | consumer_cpu_pct | producer_dropped_frames | transport_latency_ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1282 | 1377 | 7.7 | 31.7 | 0 | 874 |
| 2 | 1286 | 1372 | 6.0 | 24.0 | 0 | 925 |
| 3 | 1302 | 1381 | 6.7 | 30.0 | 0 | 849 |

## 平均结果

| 指标 | Main 平均值 | Candidate 平均值 | 差值 |
| --- | ---: | ---: | ---: |
| startup_classification_latency_ms | 1307.33 | 1290.00 | -17.33 ms |
| classification_stage_latency_ms | 1370.67 | 1376.67 | +6.00 ms |
| producer_cpu_pct | 7.30 | 6.80 | -0.50 |
| consumer_cpu_pct | 30.47 | 28.57 | -1.90 |
| producer_dropped_frames | 0.00 | 0.00 | 0.00 |

结果解释：

- 对于时延和 CPU 指标，负数差值代表 candidate 更好
- 正数差值代表 candidate 更差
- `transport_latency_ms` 只在 SHM candidate 上可见，因为 `main` 仍然使用旧的全 payload socket 传输路径

## Candidate 运行时日志样本

为了确认新增的稀疏后端日志在板端验证时确实可用，我还在 backend-only 模式下用同一份视频运行了 candidate，并采集了运行时汇总日志：

```text
video_perf camera=front_cam mode=test_file state=previewing fps=20.0 published=101 dropped_total=0 dropped_delta=0 consumers=1
video_perf camera=front_cam mode=test_file state=previewing fps=15.0 published=75 dropped_total=0 dropped_delta=0 consumers=1
fall_perf camera=front_cam ingest_fps=17.9 infer_fps=17.9 avg_infer_ms=30.6 people_frames=90 empty_frames=0 nonempty_batches=46 state=stand conf=1.00 error=
```

从这组日志可以看出两个有价值的信息：

- 当前场景下，producer 侧持续发布，没有出现 dropped frames
- 跌倒分析侧能够持续处理测试流，并输出稳定的聚合性能摘要

## 结论

基于这次新的板端实测，当前 SHM candidate 相比 `main` 基线是有优化收益的，但收益幅度属于“有改善，但不算特别大”。

本次明确改善的点：

- `startup_classification_latency_ms` 平均降低约 `17 ms`
- `health-videod` CPU 平均下降约 `0.5` 个百分点
- `health-falld` CPU 平均下降约 `1.9` 个百分点
- producer 侧 dropped frames 仍然保持为 `0`

本次没有明显改善的点：

- `classification_stage_latency_ms` 没有变快，反而平均高了约 `6 ms`；这个量级目前更像是波动范围内的噪声，除非后续重复测量仍持续朝这个方向偏移

我当前的判断是：

1. SHM transport 已经成功减少了一部分传输开销，并降低了 CPU 成本
2. 当前剩余的端到端时延，更多已经受下游分析/推理阶段影响，而不是单纯受 transport 层影响
3. 如果后续目标是获得更明显的时延收益，那么下一阶段更值得继续优化的点，应该放在 `health-falld` 的分析处理路径，而不只是 socket / shm 的传输切换本身

