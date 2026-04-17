# Long Run Checklist

## Connectivity

- multiple ESP32 devices can stay connected to one RK3588 at the same time
- approved devices reconnect automatically after WiFi interruption
- pending devices remain pending until manually approved
- rejected or disabled devices cannot push authenticated telemetry

## Process Stability

- `healthd` auto-restarts after forced kill
- `health-ui` auto-restarts after forced kill
- restarting `health-ui` does not break `healthd`
- restarting `healthd` lets `health-ui` recover cleanly after backend returns

## Storage

- `/var/lib/rk_health_station/healthd.sqlite` is created and keeps device metadata
- telemetry samples continue to append during long run
- minute aggregation rows continue to update over time
- device approval / rename / enable-disable / secret reset leave audit records

## UI Behavior

- dashboard shows current primary device data
- alerts page updates with low HR / high HR / low SpO2 / low battery conditions
- history page can query minute series for at least one device
- device approval and settings pages still work after long run

## Video Monitor Checks

- run `rk_health_station/deploy/tests/video_preview_probe.sh` on the target board
- confirm the selected preview transport still passes on the current image
- verify preview appears in `health-ui`
- verify snapshot and recording still work after preview starts

## Resource / Service Health

- 24h run without obvious memory growth in `healthd`
- 24h run without obvious memory growth in `health-ui`
- TCP port `19001` remains bound by `healthd`
- local IPC socket remains reachable by `health-ui`
- journald logs remain readable and rotate normally
