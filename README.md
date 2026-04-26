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
- 在板端进入 bundle 目录后直接运行 `./scripts/start.sh`

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
