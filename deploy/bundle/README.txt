RK3588 bundle runtime

Contents:
- bin/healthd
- bin/health-ui
- bin/health-videod
- lib/
- plugins/
- scripts/start.sh
- scripts/start_all.sh
- scripts/stop.sh
- scripts/stop_all.sh
- scripts/status.sh

Board-side quick start:
1. copy the whole rk3588_bundle directory to the board
2. enter the bundle directory
3. run: ./scripts/start.sh
4. check: ./scripts/status.sh
5. stop: ./scripts/stop.sh

Board-side one-click start/stop:
- `./scripts/start_all.sh`
- `./scripts/stop_all.sh`
- `start_all.sh` will try to infer `DISPLAY` from `~/.Xauthority`, set
  `XAUTHORITY=$HOME/.Xauthority`, and then verify all three processes are alive

Optional backend-only start:
- ./scripts/start.sh --backend-only

If the UI cannot open, check:
- DISPLAY / WAYLAND_DISPLAY / QT_QPA_PLATFORM
- logs/health-ui.log
- board-side Qt runtime compatibility
- on Ubuntu boards using system Qt runtime, install:
  `libqt5multimedia5 libqt5multimedia5-plugins libqt5multimediagsttools5 libqt5multimediawidgets5`

Video monitor notes:
- `health-videod` is the only process that opens `/dev/video11`
- default media directory on board: `/home/elf/videosurv/`
- bundle runtime exports `RK_VIDEO_SOCKET_NAME` for the video control socket
- current preview transport is local `TCP + MJPEG`
- current preview URL shape is `tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview`
- `health-ui` consumes preview frames through `QTcpSocket`; it does not open `/dev/video11`
