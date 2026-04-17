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
