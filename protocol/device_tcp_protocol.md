# Device TCP Protocol

## Transport

- ESP32 opens a TCP connection to the RK3588 `healthd` backend
- default backend listen port: `19001`
- frame format: `4-byte big-endian length` + `compact JSON body`
- one JSON body corresponds to one logical frame

## Common Envelope

```json
{
  "ver": 1,
  "type": "telemetry_batch",
  "seq": 12,
  "ts": 1713000000,
  "device_id": "watch_001",
  "payload": {}
}
```

Fields:
- `ver`: protocol version
- `type`: message type
- `seq`: device-side sequence number
- `ts`: unix seconds
- `device_id`: unique device identity
- `payload`: type-specific object

## Auth Flow

### 1. `auth_hello`

ESP32 sends:

```json
{
  "ver": 1,
  "type": "auth_hello",
  "seq": 1,
  "ts": 1713000000,
  "device_id": "watch_001",
  "payload": {
    "device_name": "RK Watch 01",
    "firmware_version": "1.0.0-rk",
    "hardware_model": "esp32s3",
    "mac": "AA:BB:CC:DD:EE:FF"
  }
}
```

Backend responses:
- `auth_challenge`
- or `auth_result` with `registration_required`
- or `auth_result` with `rejected`

### 2. `auth_challenge`

Backend sends:

```json
{
  "ver": 1,
  "type": "auth_challenge",
  "seq": 1,
  "ts": 1713000000,
  "device_id": "watch_001",
  "payload": {
    "server_nonce": "..."
  }
}
```

### 3. `auth_proof`

ESP32 sends HMAC-SHA256 proof over:
- `device_id`
- `server_nonce`
- `client_nonce`
- `ts`

```json
{
  "ver": 1,
  "type": "auth_proof",
  "seq": 2,
  "ts": 1713000001,
  "device_id": "watch_001",
  "payload": {
    "client_nonce": "...",
    "proof": "hex hmac sha256",
    "ts": 1713000001
  }
}
```

### 4. `auth_result`

Backend sends:

```json
{
  "ver": 1,
  "type": "auth_result",
  "seq": 2,
  "ts": 1713000001,
  "device_id": "watch_001",
  "payload": {
    "result": "ok"
  }
}
```

Possible `result` values:
- `ok`
- `registration_required`
- `rejected`

## Telemetry

After `auth_result = ok`, ESP32 sends `telemetry_batch` frames:

```json
{
  "ver": 1,
  "type": "telemetry_batch",
  "seq": 12,
  "ts": 1713000010,
  "device_id": "watch_001",
  "payload": {
    "heart_rate": 72,
    "spo2": 98.4,
    "acceleration": 0.42,
    "finger_detected": 1,
    "firmware_version": "1.0.0-rk",
    "device_name": "RK Watch 01"
  }
}
```

## Retry Guidance

- if TCP connect fails: retry with backoff
- if auth result is `registration_required`: stay in pending state and retry later
- if auth result is `rejected`: stop normal telemetry and wait for reprovisioning or secret reset
- if socket breaks after successful auth: reconnect and authenticate again
