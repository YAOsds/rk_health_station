# ESP IMU Fall Validation Report

## Validation matrix

- Firmware build: `idf.py build`
- Host parse tests: `ctest --test-dir out/build-rk_app-host -R 'shared_models_contract_test|healthd_tcp_smoke_test' --output-on-failure`
- Model artifact present: `esp_fw/models/imu_fall_waist_3class.espdl`
- Board flash: `idf.py -p /dev/ttyACM0 flash monitor`
- Expected runtime log: `imu_fall class=<0|1|2> probs=[...]`

## Runtime notes

- The first implementation targets waist-mounted SisFall-enhanced windows only.
- The ESP side keeps all three class probabilities and does not collapse them to a binary fall label.
- The RK3588 `healthd` side parses the extra IMU fields for future fusion logic.

## Offline model snapshot

- Training command: `python3 -u esp_fw/ml/imu_fall_model/train.py --dataset /home/elf/workspace/imu_fall_detect/sisfall-enhanced --output-dir /tmp/imu_fall_train_final --epochs 8 --batch-size 512`
- Best validation loss observed: `0.2147`
- Test-set report from `evaluate.py`:
  - `non_fall`: precision `0.9916`, recall `0.9746`
  - `pre_impact`: precision `0.1033`, recall `0.2166`
  - `fall`: precision `0.7289`, recall `0.9141`
  - macro F1: `0.6447`
- Export + quantization path:
  - `python3 esp_fw/ml/imu_fall_model/export_onnx.py --checkpoint /tmp/imu_fall_train_final/fallnet_waist_3class.pt --output /tmp/imu_fall_train_final/fallnet_waist_3class.onnx`
  - `python3 esp_fw/ml/imu_fall_model/quantize_espdl.py --onnx /tmp/imu_fall_train_final/fallnet_waist_3class.onnx --output esp_fw/models/imu_fall_waist_3class.espdl --calibration-dir /home/elf/workspace/imu_fall_detect/sisfall-enhanced --max-samples 64 --batch-size 8`
- Embedded artifact size: about `15 KB`
