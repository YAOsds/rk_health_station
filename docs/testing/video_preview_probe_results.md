# Video Preview Probe Results

> Historical note: this file records an earlier preview transport probe and is no longer the live UI preview design.
> The current production path is `TCP + MJPEG`; see `rk_health_station/docs/architecture/camera-video-readme.md`.

- Board: `elf@192.168.137.179`
- Camera: `front_cam -> /dev/video11`
- Preferred transport: `udp_mpegts_h264`
- Validation date: `2026-04-15`
- Validation command:
  - producer: `gst-launch-1.0 -e v4l2src device=/dev/video11 ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! mpph264enc ! h264parse ! mpegtsmux ! udpsink host=127.0.0.1 port=5602 sync=false async=false`
  - consumer: `gst-launch-1.0 -v udpsrc port=5602 caps='video/mpegts,systemstream=(boolean)true,packetsize=(int)188' ! tsdemux ! h264parse ! mppvideodec ! fakesink sync=false`
- Why chosen:
  - producer and consumer both validate successfully on the RK3588 board
  - the stream can be consumed through a simple local UDP URL without opening `/dev/video11` in the UI process
  - this is closer to what `QMediaPlayer` will need than the raw RTP-only probe

## Supplemental Notes

- `udp_h264` producer-only probe also starts successfully
- `tcp_mjpeg` producer-only probe also starts successfully
- those two probes alone do not prove easy consumer compatibility, so they were not selected as the default preview transport
