# RK Health Station ESP32 Firmware

This directory holds the ESP32-side migration assets for the RK3588 + ESP32 product.

Task 10 migration scope:
- reuse the existing sensor, algorithm, and WiFi provisioning assets from `esp_health_watch/esp_demo`
- replace the MQTT main path with a TCP main path
- provision WiFi + RK3588 host address + host port + `device_id` + `device_name` + `device_secret` in one step
- authenticate to `healthd` with `auth_hello -> auth_challenge -> auth_proof -> auth_result`
- treat first-time devices as pending approval on the RK3588 host

Component layout:
- `components/config_store/`: shared runtime config structures
- `components/net_client/`: TCP connect, frame send, frame receive
- `components/auth_client/`: backend auth handshake and status mapping
- `components/telemetry/`: authenticated telemetry upload wrapper

Integration notes:
- `esp_health_watch/esp_demo` remains the runnable ESP-IDF project for now
- `esp_demo/CMakeLists.txt` points `EXTRA_COMPONENT_DIRS` at these reusable components
- the original MQTT component is left in-tree as a reference but is no longer the main uplink path
