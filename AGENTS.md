# Repository Guidelines

## Project Structure & Module Organization

`rk_app/` is the main RK3588 Qt/C++ application tree. Core services live under `rk_app/src/healthd`, `rk_app/src/health_ui`, `rk_app/src/health_videod`, and `rk_app/src/health_falld`; shared models, protocol code, storage, and security helpers live in `rk_app/src/shared`. Automated tests are grouped by subsystem in `rk_app/src/tests/*_tests`. `esp_fw/` is a standalone ESP-IDF firmware project for the ESP32-S3 device side. Deployment scripts and systemd assets live in `deploy/`, protocol notes in `protocol/`, and longer-form design/deployment docs in `docs/`. Treat `out/` and `esp_fw/build/` as generated output.

## Build, Test, and Development Commands

- `bash deploy/scripts/build_rk_app.sh host -DBUILD_TESTING=ON` - build the desktop/host Qt apps and enable tests.
- `ctest --test-dir out/build-rk_app-host --output-on-failure` - run the full RK app test suite after a host build.
- `ctest --test-dir out/build-rk_app-host -R healthd_tcp_smoke_test --output-on-failure` - run a focused test target while iterating.
- `bash deploy/scripts/build_rk3588_bundle.sh` - cross-build the RK3588 bundle and verify ARM64 artifacts.
- `cd esp_fw && . ~/esp/esp-idf/export.sh && idf.py build` - build ESP32 firmware.
- `cd esp_fw && . ~/esp/esp-idf/export.sh && idf.py -p /dev/ttyACM0 flash` - flash firmware to hardware.

## Coding Style & Naming Conventions

Use C++17 and existing Qt/CMake patterns. Match the repository’s 4-space indentation and same-line opening braces (`int main(...) {`). Prefer `PascalCase` for types, `camelCase` for functions/methods, and `snake_case` for file names such as `device_session.cpp` and `device_session_test.cpp`. Keep helper functions in anonymous namespaces when they are translation-unit local. No formatter config is checked in, so avoid broad reformatting and keep changes tightly scoped.

## Testing Guidelines

Tests use Qt Test (`QTEST_MAIN`) and are wired through CMake/CTest. Add new tests beside the nearest subsystem under `rk_app/src/tests/`, and keep test file names ending in `_test.cpp`. Enable tests with `BUILD_TESTING=ON`, run the relevant `ctest -R <name>` target locally, and use `docs/testing/` for longer manual or probe-style verification notes when needed.

## Commit & Pull Request Guidelines

Recent history follows short conventional subjects like `feat: ...`, `docs: ...`, and `refactor: ...`. Keep commit titles imperative and focused on one change. Pull requests should name the affected module, list the exact build/test commands you ran, link related issues, and include screenshots or log snippets for UI, deployment, or firmware-facing changes.
