# RK3588 LSTM RKNN Board Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Use `LSTM -> RKNN` as the active fall-classifier backend, complete host-side cross-build and bundle packaging, deploy the bundle to the RK3588 Ubuntu board, run backend smoke tests on the board, and fix any runtime issues discovered during validation.

**Architecture:** Keep the existing `health-videod -> analysis socket -> health-falld` service boundary unchanged. Only update `health-falld` runtime selection, bundle environment export, and bundle packaging so the board receives a real `lstm_fall.rknn` model plus the minimum RKNN runtime library set needed for `health-falld`.

**Tech Stack:** C++17, Qt 5, CMake/CTest, bash, RKNN Runtime, RKNN-Toolkit2 2.3.2, rsync/scp, ssh.

---

## File Structure Map

### Existing files to modify

- `deploy/scripts/build_rk3588_bundle.sh`
  - enable real RKNN action runtime during cross-build and package `lstm_fall.rknn`
- `deploy/bundle/start.sh`
  - export backend-specific fall-classifier environment variables for `health-falld`
- `deploy/tests/start_fall_bundle_test.sh`
  - assert the bundle start flow exports `lstm_rknn` and `RK_FALL_LSTM_MODEL_PATH`
- `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
  - keep a runtime-status guard that expects the LSTM backend to start cleanly by default
- `rk_app/docs/superpowers/specs/2026-04-16-stgcn-rknn-design.md`
  - no code change required; keep as design reference for future ST-GCN swap-back

### Files expected to already exist and be consumed

- `yolo_detect/lstm/exports/lstm_fall.rknn`
  - host-side converted RKNN model that will be copied into the bundle
- `rk_app/src/health_falld/action/rknn_action_model_runner.cpp`
  - generic RKNN runner used by the LSTM classifier
- `rk_app/src/health_falld/action/rknn_lstm_action_classifier.cpp`
  - active temporal classifier implementation
- `rk_app/src/health_falld/runtime/runtime_config.cpp`
  - reads `RK_FALL_ACTION_BACKEND` and `RK_FALL_LSTM_MODEL_PATH`

### No-change boundaries

- `rk_app/src/health_videod/**`
  - do not change the analysis stream protocol or frame producer ownership
- `rk_app/src/shared/protocol/**`
  - do not change socket protocol during this validation cycle

## Notes Before Implementation

- Current known gap: `build_rk3588_bundle.sh` still enables only `RKAPP_ENABLE_REAL_RKNN_POSE`, so `health-falld` is not yet guaranteed to be linked with the real RKNN action runtime on the board.
- Current known gap: `deploy/bundle/start.sh` still exports `RK_FALL_ACTION_MODEL_PATH`, but `FallRuntimeConfig` now consumes `RK_FALL_LSTM_MODEL_PATH` and `RK_FALL_ACTION_BACKEND`; this must be corrected before board validation.
- Current known asset: `/home/elf/workspace/rknn_model_zoo-2.1.0/yolo_detect/lstm/exports/lstm_fall.rknn` already exists and is the source model for bundle packaging.
- The board target is Ubuntu on RK3588, so runtime should prefer `system` mode for public Qt/glibc libraries and keep `lib/app/librknnrt.so` as the private bundle library.

### Task 1: Make the bundle start flow LSTM-aware

**Files:**
- Modify: `deploy/bundle/start.sh`
- Modify: `deploy/tests/start_fall_bundle_test.sh`
- Test: `deploy/tests/start_fall_bundle_test.sh`

- [ ] **Step 1: Extend the failing bundle-env assertions first**

Add assertions like:

```bash
grep -Fxq "RK_FALL_ACTION_BACKEND=lstm_rknn" "${TMP_ROOT}/health-falld.env"
grep -Fxq "RK_FALL_LSTM_MODEL_PATH=${TMP_ROOT}/assets/models/lstm_fall.rknn" "${TMP_ROOT}/health-falld.env"
```

Run:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station
bash deploy/tests/start_fall_bundle_test.sh
```

Expected: FAIL, because `start.sh` does not export those variables yet.

- [ ] **Step 2: Update `start.sh` to export backend-specific classifier variables**

Use this shape in `deploy/bundle/start.sh`:

```bash
ACTION_BACKEND=${RK_FALL_ACTION_BACKEND:-lstm_rknn}
LSTM_MODEL_PATH=${RK_FALL_LSTM_MODEL_PATH:-${ASSETS_DIR}/models/lstm_fall.rknn}
STGCN_MODEL_PATH=${RK_FALL_STGCN_MODEL_PATH:-${ASSETS_DIR}/models/stgcn_fall.rknn}

export RK_FALL_ACTION_BACKEND="${ACTION_BACKEND}"
export RK_FALL_LSTM_MODEL_PATH="${LSTM_MODEL_PATH}"
export RK_FALL_STGCN_MODEL_PATH="${STGCN_MODEL_PATH}"
```

Keep `RK_FALL_ACTION_MODEL_PATH` only if backward compatibility is still useful during transition, but `health-falld` must no longer depend on it.

- [ ] **Step 3: Re-run the bundle-env test**

Run:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station
bash deploy/tests/start_fall_bundle_test.sh
```

Expected: PASS.

- [ ] **Step 4: Checkpoint**

```bash
git add deploy/bundle/start.sh deploy/tests/start_fall_bundle_test.sh
git commit -m "fix: export lstm fall backend bundle env"
```

### Task 2: Enable real RKNN action runtime in the cross-build and package the LSTM model

**Files:**
- Modify: `deploy/scripts/build_rk3588_bundle.sh`
- Test: `deploy/scripts/build_rk3588_bundle.sh`

- [ ] **Step 1: Make the build script fail fast when the LSTM RKNN model is missing**

Add a preflight check:

```bash
verify_path "${RKNN_MODEL_ZOO_ROOT}/yolo_detect/lstm/exports/lstm_fall.rknn"
```

Run:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station
bash deploy/scripts/build_rk3588_bundle.sh
```

Expected today: either FAIL because the build is still missing real action RKNN support, or PASS without packaging the LSTM model correctly. In either case, record the exact output before patching.

- [ ] **Step 2: Turn on the real RKNN action runtime for `health-falld`**

Update the configure line to include:

```bash
-DRKAPP_ENABLE_REAL_RKNN_ACTION=ON
```

Keep:

```bash
-DRKAPP_ENABLE_REAL_RKNN_POSE=ON
-DRKNN_MODEL_ZOO_ROOT="${RKNN_MODEL_ZOO_ROOT}"
```

so both pose and LSTM action inference use the same bundled `librknnrt.so` source.

- [ ] **Step 3: Package the LSTM model and backend defaults into the bundle**

Add install lines like:

```bash
install -m 644 "${RKNN_MODEL_ZOO_ROOT}/yolo_detect/lstm/exports/lstm_fall.rknn" \
  "${BUNDLE_DIR}/assets/models/lstm_fall.rknn"
```

Update `bundle.env` to export:

```bash
RK_FALL_ACTION_BACKEND=${RK_FALL_ACTION_BACKEND:-lstm_rknn}
RK_FALL_LSTM_MODEL_PATH=${RK_FALL_LSTM_MODEL_PATH:-${PWD}/assets/models/lstm_fall.rknn}
RK_FALL_STGCN_MODEL_PATH=${RK_FALL_STGCN_MODEL_PATH:-${PWD}/assets/models/stgcn_fall.rknn}
```

- [ ] **Step 4: Rebuild and verify the bundle artifacts**

Run:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station
bash deploy/scripts/build_rk3588_bundle.sh
file out/rk3588_bundle/bin/health-falld
ls -l out/rk3588_bundle/assets/models/lstm_fall.rknn
sed -n '1,120p' out/rk3588_bundle/bundle.env
```

Expected:
- `health-falld` is `ARM aarch64`
- `assets/models/lstm_fall.rknn` exists
- `bundle.env` points to `lstm_rknn`

- [ ] **Step 5: Checkpoint**

```bash
git add deploy/scripts/build_rk3588_bundle.sh
git commit -m "feat: package lstm rknn backend for rk3588 bundle"
```

### Task 3: Keep host-side regression coverage around the LSTM default path

**Files:**
- Modify: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
- Test: `rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
- Modify: `rk_app/src/tests/CMakeLists.txt` only if a new targeted test is needed

- [ ] **Step 1: Add or update a failing test that locks the default backend to LSTM**

Use an assertion like:

```cpp
QVERIFY(payload.contains("\"action_model_ready\":true"));
QVERIFY(payload.contains("\"latest_state\":\"monitoring\""));
```

and in a config-focused test keep:

```cpp
QCOMPARE(config.actionBackend, ActionBackendKind::LstmRknn);
```

Run:

```bash
cmake -S /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/rk_app -B /tmp/rk_health_station-host
cmake --build /tmp/rk_health_station-host --target action_classifier_factory_test fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-host -R "action_classifier_factory_test|fall_end_to_end_status_test" --output-on-failure
```

Expected: PASS after any required assertion refresh.

- [ ] **Step 2: Checkpoint**

```bash
git add rk_app/src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp rk_app/src/tests/fall_daemon_tests/action_classifier_factory_test.cpp
git commit -m "test: lock lstm backend as default fall path"
```

### Task 4: Deploy the bundle to the RK3588 board and run backend-only smoke validation

**Files:**
- No repo changes required for the first smoke pass
- Runtime evidence to collect from the board:
  - `~/rk3588_bundle/logs/health-videod.log`
  - `~/rk3588_bundle/logs/health-falld.log`
  - `~/rk3588_bundle/scripts/status.sh` output

- [ ] **Step 1: Sync the full bundle to the board**

Run from the host:

```bash
export RK_BOARD_HOST=${RK_BOARD_HOST:-elf@<rk3588-ip>}
rsync -av /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/ "${RK_BOARD_HOST}:~/rk3588_bundle/"
```

Expected: updated `bin/`, `assets/models/`, `lib/app/`, and `scripts/` arrive on the board.

- [ ] **Step 2: Start the backend stack in `system` runtime mode**

Run:

```bash
ssh "${RK_BOARD_HOST}" 'cd ~/rk3588_bundle && chmod +x scripts/*.sh && RK_RUNTIME_MODE=system ./scripts/stop.sh || true && RK_RUNTIME_MODE=system ./scripts/start.sh --backend-only'
```

Expected:
- `healthd`, `health-videod`, and `health-falld` start
- `health-falld` does not exit immediately

- [ ] **Step 3: Inspect process state and logs immediately**

Run:

```bash
ssh "${RK_BOARD_HOST}" 'cd ~/rk3588_bundle && ./scripts/status.sh && echo "--- FALLD LOG ---" && tail -n 120 logs/health-falld.log && echo "--- VIDEOD LOG ---" && tail -n 80 logs/health-videod.log'
```

Expected:
- `health-videod` and `health-falld` both show `running`
- `health-falld.log` does not contain `GLIBC_x.xx not found`
- `health-falld.log` does not contain `rknn_init_failed`, `action_model_not_loaded`, or missing-model errors

- [ ] **Step 4: Query the fall daemon runtime socket from the board**

Run:

```bash
ssh "${RK_BOARD_HOST}" 'cd ~/rk3588_bundle && python3 - <<"PY"
import socket
path = "./run/rk_fall.sock"
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
client.connect(path)
client.sendall(b"{\"action\":\"get_runtime_status\"}\n")
print(client.recv(4096).decode())
client.close()
PY'
```

Expected JSON markers:
- `"pose_model_ready":true`
- `"action_model_ready":true`
- `"input_connected":true` after `health-videod` connects
- `"latest_state":"monitoring"` before a real fall sequence is observed

### Task 5: Fix-loop for any board runtime failure and revalidate immediately

**Files:**
- Modify the smallest file that matches the actual failure:
  - `deploy/bundle/start.sh` for env/runtime-lib issues
  - `deploy/scripts/build_rk3588_bundle.sh` for missing asset or link/package issues
  - `rk_app/src/health_falld/action/rknn_action_model_runner.cpp` for RKNN I/O handling mismatches
  - `rk_app/src/health_falld/action/rknn_lstm_action_classifier.cpp` for output decoding or label mapping issues

- [ ] **Step 1: Classify the failure before editing anything**

Use this mapping:

```text
GLIBC / Qt plugin / symbol lookup -> runtime mode or bundled library issue
rknn_init_failed_* -> model path or RKNN runtime/library issue
rknn_inputs_set_failed_* -> input tensor type/size/fmt mismatch
rknn_outputs_get_failed_* or empty logits -> output tensor parsing mismatch
input_connected=false -> analysis socket / health-videod bring-up issue
```

- [ ] **Step 2: Patch only the layer responsible for the failure**

Examples:

```text
Do not modify health-videod if health-falld fails before model load.
Do not modify protocol files if the socket exists and only RKNN inference fails.
```

- [ ] **Step 3: Re-run the shortest possible validation loop**

Run locally after a patch if applicable:

```bash
cd /home/elf/workspace/QTtest/Qt例程源码/rk_health_station
bash deploy/tests/start_fall_bundle_test.sh
bash deploy/scripts/build_rk3588_bundle.sh
```

Then re-sync and re-test on the board:

```bash
rsync -av /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/out/rk3588_bundle/ "${RK_BOARD_HOST}:~/rk3588_bundle/"
ssh "${RK_BOARD_HOST}" 'cd ~/rk3588_bundle && RK_RUNTIME_MODE=system ./scripts/stop.sh || true && RK_RUNTIME_MODE=system ./scripts/start.sh --backend-only && ./scripts/status.sh && tail -n 120 logs/health-falld.log'
```

Expected: the previously observed error disappears and `health-falld` remains alive.

### Task 6: Record the validated board result and the next swap-ready boundary

**Files:**
- Modify: `rk_app/docs/superpowers/specs/2026-04-16-stgcn-rknn-design.md` only if the final result changes design assumptions
- Prefer creating a short validation note near deployment docs if needed

- [ ] **Step 1: Capture the final verified state**

Record:
- bundle build command used
- board runtime mode used (`system`)
- `health-falld` status JSON sample
- whether a real analysis frame reached `health-falld`
- whether LSTM inference loaded successfully on the board

- [ ] **Step 2: Capture the model-swap boundary explicitly**

Keep this statement in the closing notes:

```text
The active backend is LSTM -> RKNN, but the shared `17 keypoints x 45 frames` contract and generic RKNN runner remain intact, so another same-input model can replace LSTM without changing health-videod or the analysis socket protocol.
```

- [ ] **Step 3: Checkpoint**

```bash
git add deploy/ rk_app/docs/
git commit -m "docs: record rk3588 lstm board validation"
```
