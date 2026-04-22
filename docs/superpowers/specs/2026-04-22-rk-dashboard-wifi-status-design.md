# RK3588 Dashboard Wi-Fi Status Design

Date: 2026-04-22
Status: Approved for planning

## Goal

Add a host-side Wi-Fi status card to the RK3588 `Dashboard` page so the UI shows the current Wi-Fi state of the RK3588 board itself, not the ESP device. The card only covers Wi-Fi and must expose the IPv4 address that the ESP should use as its target IP during provisioning.

The feature is intentionally display-only. It does not add Wi-Fi configuration, connect/disconnect controls, or Ethernet status.

## User-Approved Scope

The user approved the following requirements during brainstorming:

- Show the status on the existing `Dashboard` page.
- Show the full field set:
  - connection status
  - SSID
  - Wi-Fi interface name
  - current IPv4
- Treat the displayed Wi-Fi IPv4 as the target IP that the ESP should fill in during provisioning.
- If the RK3588 has a Wi-Fi NIC but is not connected:
  - status shows `未连接`
  - SSID shows `--`
  - interface still shows the real interface name, such as `wlan0`
  - IPv4 shows `--`

## Context

The current UI architecture already routes dashboard data through `healthd`:

- `rk_app/src/healthd/ipc_server/ui_gateway.cpp` builds the dashboard snapshot.
- `rk_app/src/health_ui/ipc_client/ui_ipc_client.cpp` requests and receives that snapshot.
- `rk_app/src/health_ui/pages/dashboard_page.cpp` renders the snapshot.

This makes `healthd` the natural place to collect host Wi-Fi state and expose it to the UI.

The local board manual `ELF 2开发板快速使用手册.pdf` shows that the ELF 2 desktop environment already uses Wi-Fi-oriented workflows and NetworkManager-oriented networking workflows:

- Desktop Wi-Fi testing includes a successful activation example for `wlan0`.
- Desktop networking documentation uses `NetworkManager` and `nmcli`.

That aligns well with a host-side Wi-Fi status feature built around the currently active Wi-Fi interface.

## Approaches Considered

### Recommended: `healthd` collects host Wi-Fi status and includes it in dashboard snapshot

Pros:

- Matches the existing backend-to-UI architecture.
- Keeps system/network probing out of `health_ui`.
- Makes future reuse by logs, diagnostics, or other pages straightforward.

Cons:

- Requires coordinated changes in backend, IPC payload, and UI rendering.

### Alternative: `health_ui` reads local Wi-Fi state directly

Pros:

- Fewer layers to touch.

Cons:

- Breaks the current separation of responsibilities.
- Duplicates host-state logic in the UI process.
- Harder to reuse elsewhere.

### Alternative: show only a static hint for the target IP concept

Pros:

- Minimal work.

Cons:

- Does not actually show current Wi-Fi state.
- Fails the requirement to display real SSID/interface/IP information.

## Decision

Use the recommended approach:

- `healthd` becomes the single collector of RK3588 host Wi-Fi state.
- `UiGateway::buildDashboardResponse()` includes a new `host_wifi` object.
- `DashboardPage` renders a dedicated Wi-Fi status card.

## Data Model

The dashboard snapshot gains a top-level `host_wifi` object:

```json
{
  "device_count": 1,
  "current_device_id": "watch_001",
  "devices": [],
  "host_wifi": {
    "present": true,
    "connected": true,
    "interface_name": "wlan0",
    "ssid": "office_ap",
    "ipv4": "192.168.137.23"
  }
}
```

Field meanings:

- `present`: whether a Wi-Fi interface is detected on the RK3588 host
- `connected`: whether that Wi-Fi interface is currently connected
- `interface_name`: actual interface name, such as `wlan0`
- `ssid`: active SSID when connected, otherwise `--`
- `ipv4`: Wi-Fi IPv4 address when connected, otherwise `--`

For this feature, `host_wifi.ipv4` is the value the ESP user should enter as the provisioning target IP.

## Backend Design

Add a small host Wi-Fi status provider inside `rk_app/src/healthd`, for example under a new `host/` or `system/` subdirectory.

Responsibilities:

- detect whether a Wi-Fi interface exists
- determine whether it is connected
- obtain the interface name
- obtain the current SSID
- obtain the current IPv4
- return a stable value object for `UiGateway`

### Runtime Assumption

This feature targets the ELF 2 desktop environment where NetworkManager is present. That assumption is consistent with the board manual.

### Recommended Probe Strategy

Use `nmcli` as the primary source of Wi-Fi connection state, with `QNetworkInterface` as a best-effort fallback for interface and IPv4 detection.

Why:

- `QNetworkInterface` can help with interface and IPv4 enumeration, but it does not reliably provide SSID.
- The user explicitly wants SSID displayed.
- The board manual already documents NetworkManager and `nmcli` workflows on the desktop system.

Recommended semantics:

- No Wi-Fi device detected:
  - `present=false`
  - `connected=false`
  - other fields are `--`
- Wi-Fi device detected but not connected:
  - `present=true`
  - `connected=false`
  - `interface_name` is real
  - `ssid=--`
  - `ipv4=--`
- Wi-Fi device detected and connected:
  - `present=true`
  - `connected=true`
  - all fields are populated from current host state

### Failure Policy

Wi-Fi status collection is non-critical.

- Failure to invoke `nmcli`
- missing `nmcli`
- unexpected command output
- inability to resolve a usable IPv4

must not fail `healthd` startup or dashboard responses. Instead, the backend returns a safe best-effort `host_wifi` object so the rest of the product remains usable.

## UI Design

Add a dedicated Wi-Fi status section to `DashboardPage`. It remains separate from ESP device telemetry so users can distinguish:

- device-side health data
- RK3588 host network environment

Displayed rows:

- `status`
- `ssid`
- `interface`
- `ipv4 (for ESP)`

Display mapping:

- `present=false` -> `status = 无 Wi-Fi 网卡`
- `present=true && connected=false` -> `status = 未连接`
- `present=true && connected=true` -> `status = 已连接`

Field display rules:

- disconnected:
  - `ssid = --`
  - `interface = <real interface name>`
  - `ipv4 (for ESP) = --`
- no Wi-Fi NIC:
  - all fields except status show `--`

The label `ipv4 (for ESP)` makes the operational meaning explicit: this is the address the ESP should use as its target IP on the same Wi-Fi network.

## Refresh Strategy

Keep using the existing dashboard snapshot request path. Do not introduce a second Wi-Fi-specific IPC request.

Refresh behavior:

- request once when the UI starts
- add lightweight periodic refresh of the dashboard snapshot every 3 seconds

Rationale:

- fast enough to reflect connect/disconnect changes during provisioning or setup
- light enough to avoid unnecessary backend churn
- reuses the existing data flow already used by the dashboard

## Error Handling

If host Wi-Fi state cannot be collected, the dashboard request still succeeds.

Recommended degraded behavior:

- if a Wi-Fi interface can still be detected through fallback enumeration:

```json
{
  "present": true,
  "connected": false,
  "interface_name": "wlan0",
  "ssid": "--",
  "ipv4": "--"
}
```

- if no Wi-Fi interface can be detected at all:

```json
{
  "present": false,
  "connected": false,
  "interface_name": "--",
  "ssid": "--",
  "ipv4": "--"
}
```

The important rule is that dashboard rendering must remain stable and never crash because Wi-Fi probing failed. When fallback enumeration still finds a Wi-Fi interface, the UI should prefer showing `未连接` with the real interface name instead of incorrectly implying that the board has no Wi-Fi NIC.

## Testing and Validation

### Unit Tests

Add focused tests for the Wi-Fi provider or parser logic:

- Wi-Fi device present and connected
- Wi-Fi device present and disconnected
- no Wi-Fi device
- malformed or partial `nmcli` output
- command failure

If parsing is split into helper functions, those helpers should be tested directly with captured command output samples.

### Integration Tests

Add dashboard snapshot coverage so `get_dashboard_snapshot` includes `host_wifi` and preserves existing payload behavior for device data.

### Manual Validation

Validate these scenarios on the RK3588 board:

1. Wi-Fi NIC present but not connected
   - status `未连接`
   - SSID `--`
   - real interface name shown
   - IPv4 `--`
2. Wi-Fi connected
   - status `已连接`
   - SSID matches current AP
   - interface name shown
   - IPv4 matches current Wi-Fi IPv4
3. Wi-Fi disconnected after being connected
   - UI updates within about 3 seconds
4. No Wi-Fi NIC or no detectable Wi-Fi device
   - status `无 Wi-Fi 网卡`
5. ESP provisioning
   - using the displayed `ipv4 (for ESP)` allows ESP and RK3588 to communicate on the same Wi-Fi network

## Out of Scope

- Wi-Fi connect/disconnect controls
- editing SSID or password
- switching DHCP/static IP from this screen
- Ethernet or general network status
- VPN, IPv6, DNS, gateway, signal strength, or multi-interface routing views

## Implementation Notes for Planning

Likely touch points:

- `rk_app/src/healthd/CMakeLists.txt`
- new host Wi-Fi provider files under `rk_app/src/healthd/`
- `rk_app/src/healthd/ipc_server/ui_gateway.cpp`
- `rk_app/src/health_ui/pages/dashboard_page.cpp`
- `rk_app/src/health_ui/pages/dashboard_page.h`
- `rk_app/src/health_ui/app/ui_app.cpp`
- relevant unit/integration tests under `rk_app/src/tests/`

The implementation plan should explicitly decide whether the provider:

- shells out to `nmcli` directly via `QProcess`, or
- wraps command execution behind a small interface to make tests easier

The design recommendation is to keep that wrapper small and testable.
