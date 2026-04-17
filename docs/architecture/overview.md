# Architecture Overview

## Product Shape

- one RK3588 main station runs the whole backend and UI locally
- multiple ESP32 wearable devices connect over the same external WiFi router
- RK3588 software is split into two Qt/C++ processes:
  - `healthd`: backend daemon
  - `health-ui`: frontend application

## Data Paths

### Device to Backend

- ESP32 -> WiFi -> TCP -> `healthd`
- protocol is length-prefixed compact JSON
- devices authenticate through `auth_hello -> auth_challenge -> auth_proof -> auth_result`
- only authenticated devices can push telemetry into the main backend path

### Backend to UI

- `health-ui` never talks to devices directly
- `health-ui` only uses local IPC exposed by `healthd`
- local IPC transport is `QLocalServer` / `QLocalSocket` with newline-delimited JSON

## Backend Responsibilities

`healthd` is responsible for:
- device admission and approval workflow
- device secret verification and runtime session auth
- telemetry storage into SQLite
- minute aggregation generation
- alert evaluation
- local IPC responses for UI pages

## UI Responsibilities

`health-ui` is responsible for:
- dashboard display
- device list and detail navigation
- pending-device approval operations
- device settings operations
- alerts snapshot display
- minute history query display

## Persistence

SQLite stores:
- device metadata
- pending device requests
- audit log
- raw telemetry samples
- minute aggregation data

## Security Model

- device credentials are provisioned per ESP32
- unknown devices enter pending approval instead of becoming active automatically
- disabled or rejected devices are blocked from authenticated telemetry
- UI management operations flow through backend policy checks
