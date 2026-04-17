# RK3588 后端 Bundle 使用说明

## 1. 文档目标

本文档只针对当前仓库 `rk_health_station`、当前 RK3588 开发板，以及当前目录下文档规定的标准作业方式。

本文档的主流程已经统一为：

- 宿主机 `Ubuntu 22.04` 上交叉编译
- 生成可直接交付的 `rk3588_bundle/`
- 通过 `scp` / `rsync` / `ssh` 把 bundle 传到 RK3588
- 在 RK3588 上进入 bundle 目录后执行 `./scripts/start.sh`

这套流程不依赖板端原生编译，也不要求先走 systemd 安装。

---

## 2. 标准来源

本次流程以以下两份本地文档为准：

- `QT编程.txt`
- `RK3588_Qt_AI标准作业手册.md`

直接相关的强制结论是：

1. 正确模式是“宿主机开发 + PC 端交叉编译 + 开发板运行验证”
2. 交叉编译必须使用 RK3588 对应 SDK / 工具链
3. 不能只看构建是否成功，必须再用 `file` 检查产物架构
4. `ssh` / `scp` / `rsync` 的主要用途是传输产物与板端验证

---

## 3. 当前项目的运行结构

当前 RK3588 主站采用双进程 Qt/C++ 架构：

- `healthd`
  - 监听设备 TCP 连接
  - 执行设备认证握手
  - 写入 SQLite
  - 提供本地 IPC
  - 生成告警快照与历史查询数据
- `health-ui`
  - 只连接本地 IPC
  - 不直接连接 ESP32
  - 展示设备、审批、告警、历史等界面

核心代码入口：

- `rk_health_station/rk_app/src/healthd/main.cpp`
- `rk_health_station/rk_app/src/healthd/app/daemon_app.cpp`
- `rk_health_station/rk_app/src/shared/storage/database.cpp`
- `rk_health_station/rk_app/src/healthd/ipc_server/ui_gateway.cpp`

---

## 4. 当前交叉环境基线

当前仓库使用如下交叉环境基线：

- SDK 根目录：`/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot`
- 交叉 C 编译器：`/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/aarch64-buildroot-linux-gnu-gcc`
- 交叉 C++ 编译器：`/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/aarch64-buildroot-linux-gnu-g++`
- SDK 自带 `qmake`：`/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake`
- sysroot：`/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot`
- 工具链文件：`rk_health_station/cmake/toolchains/rk3588-buildroot-aarch64.cmake`

虽然 `rk_health_station` 当前是 CMake 工程，但这套 SDK 中已经包含 Qt5 的 CMake package config，因此可以直接用于当前项目的交叉编译。

---

## 5. 一键交叉编译脚本

当前仓库已提供宿主机一键脚本：

- `rk_health_station/deploy/scripts/build_rk3588_bundle.sh`

执行：

```bash
bash rk_health_station/deploy/scripts/build_rk3588_bundle.sh
```

默认输出：

- `rk_health_station/out/rk3588_bundle/`

该脚本完成的动作：

1. 调用当前仓库工具链文件进行 CMake 交叉配置
2. 交叉编译 `healthd` 和 `health-ui`
3. 用 `file` 验证二进制必须为 `ARM aarch64`
4. 组装 bundle 目录
5. 复制 bundle 运行脚本
6. 复制 Qt 运行库和插件到 bundle 内部

---

## 6. Bundle 目录结构

默认生成结果如下：

- `rk_health_station/out/rk3588_bundle/bin/healthd`
- `rk_health_station/out/rk3588_bundle/bin/health-ui`
- `rk_health_station/out/rk3588_bundle/lib/`
- `rk_health_station/out/rk3588_bundle/plugins/`
- `rk_health_station/out/rk3588_bundle/scripts/start.sh`
- `rk_health_station/out/rk3588_bundle/scripts/stop.sh`
- `rk_health_station/out/rk3588_bundle/scripts/status.sh`
- `rk_health_station/out/rk3588_bundle/logs/`
- `rk_health_station/out/rk3588_bundle/run/`
- `rk_health_station/out/rk3588_bundle/data/`

其中：

- `logs/` 用于保存运行日志
- `run/` 用于保存 pid 和本地 IPC socket
- `data/` 用于保存 SQLite 数据库

---

## 7. 如何验证交叉编译产物正确

执行：

```bash
file rk_health_station/out/rk3588_bundle/bin/healthd
file rk_health_station/out/rk3588_bundle/bin/health-ui
```

只有当输出包含 `ARM aarch64` 时，才允许说“RK3588 交叉编译成功”。

如果输出是 `x86-64`，说明误用了宿主机编译链，不是正确产物。

---

## 8. 如何通过 SSH 传输到板端

### 8.1 推荐用 rsync

```bash
rsync -av rk_health_station/out/rk3588_bundle/ <rk_user>@<rk_ip>:/home/<rk_user>/rk3588_bundle/
```

这条命令的特点是：

- 传的是整个 bundle 目录
- 多次迭代更新时速度更快
- 板端目录结构会和宿主机保持一致

### 8.2 也可以用 scp

```bash
scp -r rk_health_station/out/rk3588_bundle <rk_user>@<rk_ip>:/home/<rk_user>/
```

如果使用这条命令，板端目录通常会是：

- `/home/<rk_user>/rk3588_bundle`

### 8.3 传输后建议检查

```bash
ssh <rk_user>@<rk_ip> '
  ls -l /home/<rk_user>/rk3588_bundle/bin &&
  ls -l /home/<rk_user>/rk3588_bundle/scripts
'
```

---

## 9. 板端如何一键开始运行

进入板端 bundle 目录后执行：

```bash
ssh <rk_user>@<rk_ip> '
  cd /home/<rk_user>/rk3588_bundle &&
  chmod +x scripts/*.sh &&
  ./scripts/start.sh
'
```

如果你只想先启动后端，不启动 UI：

```bash
ssh <rk_user>@<rk_ip> '
  cd /home/<rk_user>/rk3588_bundle &&
  ./scripts/start.sh --backend-only
'
```

### 9.1 `start.sh` 的默认行为

`start.sh` 会：

1. 创建 `logs/`、`run/`、`data/`
2. 设置 `HEALTHD_DB_PATH`
3. 设置 `RK_HEALTH_STATION_SOCKET_NAME`
4. 设置 `LD_LIBRARY_PATH`
5. 设置 `QT_PLUGIN_PATH`
6. 启动 `healthd`
7. 等待本地 socket 就绪
8. 再启动 `health-ui`

### 9.2 日志位置

- 后端日志：`logs/healthd.log`
- UI 日志：`logs/health-ui.log`

### 9.3 运行状态查看

```bash
ssh <rk_user>@<rk_ip> '
  cd /home/<rk_user>/rk3588_bundle &&
  ./scripts/status.sh
'
```

### 9.4 停止运行

```bash
ssh <rk_user>@<rk_ip> '
  cd /home/<rk_user>/rk3588_bundle &&
  ./scripts/stop.sh
'
```

---

## 10. 运行时目录说明

bundle 运行时默认使用以下路径：

- 数据库：`data/healthd.sqlite`
- 本地 IPC socket：`run/rk_health_station.sock`
- 后端 pid：`run/healthd.pid`
- UI pid：`run/health-ui.pid`
- 后端日志：`logs/healthd.log`
- UI 日志：`logs/health-ui.log`

这意味着整个 bundle 目录天然自包含，便于移动、打包和替换。

---

## 11. 如果 UI 无法正常打开

优先检查：

1. `logs/health-ui.log`
2. 板端当前图形会话是否可用
3. `DISPLAY` / `WAYLAND_DISPLAY` / `QT_QPA_PLATFORM`
4. 板端对 bundle 中 Qt 运行库的兼容情况

如果只是先验证后端功能，可以先用：

```bash
./scripts/start.sh --backend-only
```

---

## 12. 与 systemd 安装流的关系

当前仓库仍然保留：

- `deploy/scripts/install.sh`
- `deploy/scripts/start.sh`
- `deploy/scripts/stop.sh`
- `deploy/systemd/*.service`

但这些现在属于可选的 systemd 安装流。

你当前要求的主流程，已经改成：

- 一键交叉编译 bundle
- 传输 bundle 到板端
- 直接执行 `./scripts/start.sh`

---

## 13. 当前仍需注意的后端已知问题

这次新增的是 bundle 部署能力，不改变之前后端代码审查结论。当前仍需注意：

- `health-ui.service` 目前按 root 系统服务部署，Ubuntu 图形会话下可能导致 UI 不显示
- 历史分钟聚合当前未覆盖 `acceleration`
- 告警接口当前更接近实时快照，不是完整持久化告警历史
- `device_pending_requests` 建表逻辑有重复


## Video Monitor Runtime

- `health-videod` is the only process allowed to open `/dev/video11`
- default video storage directory: `/home/elf/videosurv/`
- UI may change the storage directory through the Video page, but the daemon validates it before applying
- bundle runtime exports `RK_VIDEO_SOCKET_NAME` for the video IPC socket
- current preview transport is local `TCP + MJPEG`
- current preview URL shape is `tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview`
- `health-ui` consumes preview frames through `QTcpSocket`; it does not open `/dev/video11`
- for architecture details, see `rk_health_station/docs/architecture/camera-video-readme.md`
