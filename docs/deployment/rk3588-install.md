# RK3588 Bundle Deployment Guide

## Standard Workflow

This project now uses a fixed deployment workflow:

- cross-build on the Ubuntu host
- assemble `rk3588_bundle/`
- transfer the whole bundle directory to the RK3588 board
- start it on the board with `./scripts/start.sh`

The verified SDK path used by the current repository is:

- `/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot`

The toolchain file provided by this repository is:

- `rk_health_station/cmake/toolchains/rk3588-buildroot-aarch64.cmake`

## 1. Build the Bundle on the Host

Run:

```bash
bash rk_health_station/deploy/scripts/build_rk3588_bundle.sh
```

Default output:

- `rk_health_station/out/rk3588_bundle/`

The bundle contains:

- `bin/healthd`
- `bin/health-ui`
- `lib/`
- `plugins/`
- `scripts/start.sh`
- `scripts/stop.sh`
- `scripts/status.sh`

## 2. Verify That the Bundle Is Really ARM64

```bash
file rk_health_station/out/rk3588_bundle/bin/healthd
file rk_health_station/out/rk3588_bundle/bin/health-ui
```

Expected result:

- output contains `ARM aarch64`
- output must not be `x86-64`

## 3. Transfer the Bundle to the Board

Recommended `rsync` method:

```bash
rsync -av rk_health_station/out/rk3588_bundle/ <rk_user>@<rk_ip>:/home/<rk_user>/rk3588_bundle/
```

Equivalent `scp` method:

```bash
scp -r rk_health_station/out/rk3588_bundle <rk_user>@<rk_ip>:/home/<rk_user>/
```

## 4. Start on the Board

```bash
ssh <rk_user>@<rk_ip> '
  cd /home/<rk_user>/rk3588_bundle &&
  chmod +x scripts/*.sh &&
  ./scripts/start.sh
'
```

Backend-only start:

```bash
ssh <rk_user>@<rk_ip> '
  cd /home/<rk_user>/rk3588_bundle &&
  ./scripts/start.sh --backend-only
'
```

## 5. Status and Stop on the Board

Status:

```bash
ssh <rk_user>@<rk_ip> '
  cd /home/<rk_user>/rk3588_bundle &&
  ./scripts/status.sh
'
```

Stop:

```bash
ssh <rk_user>@<rk_ip> '
  cd /home/<rk_user>/rk3588_bundle &&
  ./scripts/stop.sh
'
```

## 6. Runtime Notes

- logs are written into `rk3588_bundle/logs/`
- pid files are written into `rk3588_bundle/run/`
- database is written into `rk3588_bundle/data/healthd.sqlite`
- local IPC socket defaults to `rk3588_bundle/run/rk_health_station.sock`
- bundle start does not depend on systemd
- if UI cannot open, first inspect `logs/health-ui.log`


## Video Monitor Runtime

- `health-videod` is the only process allowed to open `/dev/video11`
- default video storage directory: `/home/elf/videosurv/`
- UI may change the storage directory through the Video page, but the daemon validates it before applying
- bundle runtime exports `RK_VIDEO_SOCKET_NAME` for the video IPC socket
- current preview transport is local `TCP + MJPEG`
- current preview URL shape is `tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview`
- `health-ui` consumes preview frames through `QTcpSocket`; it does not open `/dev/video11`
- for architecture details, see `rk_health_station/docs/architecture/camera-video-readme.md`
