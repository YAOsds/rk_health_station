# ESP IMU Fall Validation Report

## Validation matrix

- Firmware build: `idf.py build`
- Host parse tests: `ctest --test-dir out/build-rk_app-host -R 'shared_models_contract_test|healthd_tcp_smoke_test' --output-on-failure`
- Model artifact present: `esp_fw/models/imu_fall_waist_3class.espdl`
- External offline workspace: `/home/elf/workspace/imu_fall_detect/imu_fall_model`
- Board flash: `idf.py -p /dev/ttyACM0 flash monitor`
- Expected runtime logs:
  - `FALL_CLS: model_addr=0x........ aligned16=1`
  - `FALL_CLS: largest_internal_kb=... largest_psram_kb=...`
  - `FALL_CLS: esp-dl model loaded successfully`
  - `imu_fall class=<0|1|2> probs=[...]`

## Runtime notes

- The first implementation targets waist-mounted SisFall-enhanced windows only.
- The ESP side keeps all three class probabilities and does not collapse them to a binary fall label.
- The RK3588 `healthd` side parses the extra IMU fields for future fusion logic.

## Offline model snapshot

- Training command: `python3 -u /home/elf/workspace/imu_fall_detect/imu_fall_model/train.py --dataset /home/elf/workspace/imu_fall_detect/sisfall-enhanced --output-dir /tmp/imu_fall_train_final --epochs 8 --batch-size 512`
- Best validation loss observed: `0.2147`
- Test-set report from `evaluate.py`:
  - `non_fall`: precision `0.9916`, recall `0.9746`
  - `pre_impact`: precision `0.1033`, recall `0.2166`
  - `fall`: precision `0.7289`, recall `0.9141`
  - macro F1: `0.6447`
- Export + quantization path:
  - `python3 /home/elf/workspace/imu_fall_detect/imu_fall_model/export_onnx.py --checkpoint /tmp/imu_fall_train_final/fallnet_waist_3class.pt --output /tmp/imu_fall_train_final/fallnet_waist_3class.onnx`
  - `python3 /home/elf/workspace/imu_fall_detect/imu_fall_model/quantize_espdl.py --onnx /tmp/imu_fall_train_final/fallnet_waist_3class.onnx --output /home/elf/workspace/imu_fall_detect/artifacts/imu_fall_waist_3class.espdl --calibration-dir /home/elf/workspace/imu_fall_detect/sisfall-enhanced --max-samples 64 --batch-size 8`
  - `bash esp_fw/tools/run_imu_model_pipeline.sh`
- Embedded artifact size: about `15 KB`

## Runtime repair notes

- The original reboot loop was caused by two conditions at the same time:
  - the embedded `.espdl` blob was not guaranteed to be 16-byte aligned
  - PSRAM was disabled, so `esp-dl` exhausted available allocatable memory during model load
- The repaired runtime now depends on:
  - aligned model embedding via `target_add_aligned_binary_data(...)`
  - PSRAM enabled in `sdkconfig.defaults`
  - explicit serial diagnostics for model size, flash address alignment, and largest free internal/PSRAM blocks
