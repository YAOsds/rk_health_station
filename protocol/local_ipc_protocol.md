# Local IPC Protocol

## Transport

- `healthd` exposes a `QLocalServer`
- `health-ui` connects by `QLocalSocket`
- default socket name/path comes from `RK_HEALTH_STATION_SOCKET_NAME`
- messages are newline-delimited compact JSON objects

## Common Envelope

```json
{
  "ver": 1,
  "kind": "request",
  "action": "get_device_list",
  "req_id": "ui-1",
  "ok": true,
  "payload": {}
}
```

## Read Actions

- `get_device_list`
- `get_dashboard_snapshot`
- `get_pending_devices`
- `get_alerts_snapshot`
- `get_history_series`

### `get_history_series` request

```json
{
  "ver": 1,
  "kind": "request",
  "action": "get_history_series",
  "req_id": "ui-5",
  "ok": true,
  "payload": {
    "device_id": "watch_001",
    "from_ts": 1713000000,
    "to_ts": 1713003600
  }
}
```

## Write Actions

- `approve_device`
- `reject_device`
- `rename_device`
- `set_device_enabled`
- `reset_device_secret`

## Error Response

```json
{
  "ver": 1,
  "kind": "response",
  "action": "get_history_series",
  "req_id": "ui-5",
  "ok": false,
  "payload": {
    "error": "history_query_failed"
  }
}
```
