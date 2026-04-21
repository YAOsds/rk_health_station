# ESP Firmware Provisioning UX and Heartbeat Design

## Goal

Improve `esp_fw` provisioning so the ESP32-S3 provisioning page automatically refreshes nearby Wi-Fi networks for user selection, and provisioning mode emits continuous heartbeat logs every 3 seconds for reliable serial debugging.

## Current State

The current firmware enters provisioning mode when device config is missing or incomplete. In that path:

- `esp_fw/main/app_controller.c` logs once that config is missing and starts the provisioning portal.
- `esp_fw/components/provisioning_portal/provisioning_html.h` renders a static HTML form.
- `wifi_ssid` is a plain text input and must be typed manually.
- There is no `/scan` endpoint for listing nearby APs.
- There is no recurring provisioning heartbeat log after portal startup.

This makes initial configuration less user-friendly and leaves the serial console too quiet during provisioning.

## Requirements

### User-facing

- The provisioning page must automatically refresh nearby Wi-Fi networks on page load and then every 3 seconds.
- The user must select SSID from a scanned list instead of typing it manually.
- Hidden SSID/manual SSID entry is not required.
- Other provisioning fields remain editable: Wi-Fi password, RK3588 host, RK3588 port, device ID, device name, and device secret.
- The page must avoid full-page refreshes while the user is filling the form.

### Debugging

- Provisioning mode must print a heartbeat log every 3 seconds.
- Heartbeat logs must continue until configuration is saved and reboot is triggered.
- Logs must summarize provisioning state without flooding the console.

### Compatibility

- The design should follow the proven pattern in `QTtest/Qt例程源码/esp_health_watch/esp_demo`, especially its split between a provisioning web page and a JSON Wi-Fi scan endpoint.
- The rest of the normal runtime flow after configuration save should remain unchanged.

## Chosen Approach

Adopt the `esp_demo` pattern in a simplified form:

1. Switch provisioning Wi-Fi mode from `WIFI_MODE_AP` to `WIFI_MODE_APSTA`.
2. Add a `GET /scan` endpoint in the provisioning portal that runs a scan and returns JSON results.
3. Replace the `wifi_ssid` text input in the provisioning HTML with a `<select>` populated by JavaScript.
4. Add client-side polling that requests `/scan` immediately on page load and every 3 seconds afterward.
5. Add a provisioning heartbeat task that logs summary state every 3 seconds while provisioning is active.

This is preferred over full-page auto-refresh because it preserves partially entered form data and improves usability. It is preferred over WebSocket/SSE because the current firmware does not need a persistent push channel for this scope.

## Architecture

### Provisioning Mode Lifecycle

When `app_controller_run()` detects that config is missing or incomplete, it will continue to enter provisioning mode. The provisioning portal component becomes responsible for:

- starting SoftAP in APSTA mode
- serving the HTML form
- serving Wi-Fi scan results through `/scan`
- tracking provisioning runtime state for heartbeat logs
- logging provisioning events and periodic heartbeat summaries
- stopping the heartbeat task when provisioning is ending due to reboot

The `app_controller` module remains the entry point but should not absorb scan or heartbeat details. Those details belong inside `provisioning_portal` so the provisioning mode remains cohesive.

### Frontend Behavior

The provisioning page will:

- render an SSID dropdown instead of a free-text SSID input
- issue `fetch('/scan')` once at page load
- re-issue `fetch('/scan')` every 3 seconds
- rebuild the SSID option list from the response
- preserve the currently selected SSID if it still exists in the refreshed scan results
- show a lightweight status hint if scan results are empty or the scan request fails

The page will not use a full reload, server-side templating loops, or browser push channels.

### Scan Endpoint Behavior

`GET /scan` will:

- trigger a synchronous scan while the device remains in APSTA mode
- collect nearby AP records
- discard records with empty SSID
- deduplicate entries by SSID
- keep the strongest RSSI for duplicate SSIDs
- sort results from strongest to weakest RSSI
- return compact JSON objects with `ssid` and `rssi`

This matches the practical intent of `esp_demo`: give the user a clean list of selectable nearby networks without making the API more complex than needed.

## Detailed Design

### 1. Provisioning Wi-Fi Mode

Current provisioning startup uses `WIFI_MODE_AP`. That blocks active station-side scanning patterns used in `esp_demo`.

Change provisioning startup to `WIFI_MODE_APSTA` so the device can:

- keep the `RKHealth-XXXX` provisioning AP available for the phone
- run on-demand scans for nearby infrastructure APs

The AP SSID format remains `RKHealth-%02X%02X`, password remains `rkhealth01`, and portal URL remains `http://192.168.4.1/`.

### 2. Provisioning HTML

The static page in `esp_fw/components/provisioning_portal/provisioning_html.h` will be updated to:

- replace the `Wi-Fi SSID` text input with a `<select name="wifi_ssid">`
- add a small status element for scan state, such as "scanning", "updated", or "no networks found"
- add inline JavaScript that:
  - performs initial fetch of `/scan`
  - schedules refresh every 3 seconds
  - preserves current selection if possible
  - renders each AP as `SSID (RSSI dBm)`

There is no manual fallback SSID input.

### 3. `/scan` HTTP API

The provisioning portal module will register a new URI handler:

- `GET /scan`

Response format:

```json
[
  { "ssid": "RK-Lab", "rssi": -48 },
  { "ssid": "Office-2.4G", "rssi": -67 }
]
```

Behavioral rules:

- Return `[]` instead of an HTTP error for "no APs found".
- Return `[]` on scan collection failure after logging the failure on serial.
- Keep JSON small and ASCII-safe.
- Exclude empty SSIDs.
- Deduplicate by SSID with best RSSI retained.

### 4. Form Submission

The existing `POST /save` flow remains in place.

Validation remains largely unchanged:

- `wifi_ssid` must exist and be non-empty
- `wifi_password` must exist and be non-empty
- `server_ip` must exist and be non-empty
- `server_port` must parse to `1..65535`
- `device_id`, `device_name`, and `device_secret` must exist and be non-empty

When save succeeds:

- config is written to NVS
- success is logged on serial
- heartbeat task is told provisioning is ending
- device replies to HTTP with success text
- reboot task runs as it does today

### 5. Provisioning Heartbeat Logging

Add a dedicated provisioning heartbeat task inside `provisioning_portal`.

Task properties:

- starts when provisioning portal starts successfully
- emits one heartbeat line every 3 seconds
- stops when provisioning is ending or the component is stopped

Heartbeat contents should be single-line and stable. Proposed format:

```text
[prov] alive ap=RKHealth-02A5 portal=http://192.168.4.1/ clients=1 scan_aps=6 last_scan_age_ms=842 uptime_s=27
```

Heartbeat fields:

- `ap`: current provisioning AP SSID
- `portal`: fixed provisioning URL
- `clients`: currently connected client count on the provisioning AP
- `scan_aps`: count of valid SSIDs from the most recent scan
- `last_scan_age_ms`: age of most recent completed scan
- `uptime_s`: seconds since provisioning portal start

### 6. Event Logs Around Provisioning

In addition to periodic heartbeat, emit event-driven logs for important state changes:

- provisioning AP started: include SSID, password, portal URL
- HTTP portal started
- client connected to provisioning AP
- client disconnected from provisioning AP
- scan finished: include AP count and strongest AP summary when available
- invalid provisioning form submitted
- provisioning config saved: include SSID, host, port, device ID, device name
- reboot scheduled

These logs give visibility into both steady-state and transitions without turning the heartbeat into a dump of all details.

## Module and File Boundaries

### Files to modify

- `esp_fw/components/provisioning_portal/provisioning_portal.c`
  - add APSTA startup behavior
  - add `/scan` handler
  - add scan result tracking
  - add provisioning event hooks
  - add heartbeat task lifecycle
- `esp_fw/components/provisioning_portal/include/provisioning_portal.h`
  - only if helper declarations are needed for new internal-facing APIs
- `esp_fw/components/provisioning_portal/provisioning_html.h`
  - replace SSID text input with dynamic select and polling JavaScript
- `esp_fw/components/provisioning_portal/test/test_provisioning_parser.c`
  - extend coverage for updated form assumptions if needed
- `esp_fw/main/app_controller.c`
  - keep entry logic mostly unchanged; only minor adjustments if needed for clearer provisioning startup logs

### Optional new files if clarity improves

If `provisioning_portal.c` becomes too large, split scan/heartbeat helpers into a focused companion file such as:

- `esp_fw/components/provisioning_portal/provisioning_status.c`
- `esp_fw/components/provisioning_portal/include/provisioning_status.h`

This split is optional. If the implementation remains small enough, keeping the logic together in `provisioning_portal.c` is acceptable.

## Data Model for Provisioning Runtime State

Track a small runtime state struct inside provisioning code containing:

- AP SSID string
- current connected client count
- last valid scan AP count
- timestamp of last scan completion
- strongest scanned SSID and RSSI for event log summaries
- task handle for heartbeat task
- boolean flag indicating portal active

This state supports both `/scan` summaries and periodic heartbeat logs.

## Error Handling

### Scan failures

If a scan cannot be started or AP records cannot be collected:

- serial log should record the failure reason
- `/scan` should return `[]`
- frontend should update its status hint but continue polling every 3 seconds
- heartbeat should continue and show stale `last_scan_age_ms` increasing

### Empty scan results

If no valid SSIDs are found:

- `/scan` returns `[]`
- page shows a non-blocking hint such as "未扫描到可用 Wi-Fi"
- user can wait for next refresh

### Save failures

If saving fails:

- keep existing HTTP error behavior
- serial log should include the failure point
- provisioning portal remains active
- heartbeat continues

## Testing Strategy

### Unit / component tests

Extend provisioning tests to cover at least:

- existing form parsing still succeeds for valid body with `wifi_ssid`
- invalid form still fails when required fields are missing
- helper logic used for deduplicating and sorting scanned APs, if factored into pure functions

Because direct Wi-Fi scanning depends on ESP-IDF runtime APIs, the scan handler itself may be hard to unit test end-to-end. Prefer extracting the JSON formatting and record normalization logic into helper functions that can be tested deterministically.

### Manual verification

1. Build firmware with existing host build process for `esp_fw`.
2. Flash device and erase NVS if needed.
3. Confirm serial logs show:
   - provisioning AP startup log
   - recurring heartbeat every 3 seconds
4. Connect phone to `RKHealth-XXXX`.
5. Open `http://192.168.4.1/`.
6. Confirm SSID dropdown is populated automatically.
7. Wait at least 6 to 9 seconds and confirm list refreshes without page reload.
8. Confirm other form fields keep their typed values during SSID refresh.
9. Submit valid config and confirm save log plus reboot.
10. After reboot, confirm device leaves provisioning path and enters normal Wi-Fi/runtime path.

## Out of Scope

The following are intentionally excluded from this design:

- hidden SSID/manual SSID entry
- WebSocket or Server-Sent Events
- redesign of non-provisioning UI
- changes to runtime auth, telemetry, or sensor logic
- 5 GHz network support changes
- major migration to ESP-IDF provisioning manager APIs

## Risks and Mitigations

### Risk: frequent scans interfere with AP usability

Mitigation:

- use APSTA like `esp_demo`
- keep scan payload compact
- keep polling interval at 3 seconds only during provisioning mode

### Risk: provisioning file grows too large

Mitigation:

- allow a small helper-file split if scan/heartbeat code reduces readability

### Risk: scan results flicker and interrupt UX

Mitigation:

- refresh only the dropdown
- preserve current selection when SSID still exists
- do not full-page reload

## Success Criteria

The change is successful when all of the following are true:

- provisioning page no longer requires manual SSID entry
- nearby Wi-Fi list is visible automatically and refreshes every 3 seconds
- provisioning serial logs show continuous heartbeat every 3 seconds
- heartbeat continues while waiting for user action
- saving config still writes NVS and reboots successfully
- post-provision normal runtime path is unchanged
