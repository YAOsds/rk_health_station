# Deployment Assets

This directory contains deployment assets for the RK3588 station build.

Contents:
- `bundle/`: direct-run bundle scripts copied into `rk3588_bundle/`
- `config/runtime_config.json`: bundle 默认运行时配置
- `bundle/config.sh`: 独立配置 UI 启动入口
- `scripts/build_rk3588_bundle.sh`: one-click host-side cross-build and bundle pack script
- `scripts/install.sh`: optional systemd install flow
- `scripts/start.sh`: optional systemd start flow
- `scripts/stop.sh`: optional systemd stop flow
- `scripts/collect_logs.sh`: collect recent logs and runtime state
- `systemd/healthd.service`: backend daemon service
- `systemd/health-ui.service`: UI service
