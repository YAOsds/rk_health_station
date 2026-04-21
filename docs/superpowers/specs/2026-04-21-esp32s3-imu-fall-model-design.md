# ESP32-S3 IMU Fall Model Training and Deployment Design

## Goal

Build a three-class IMU fall detection pipeline for the `rk_health_station` ESP32-S3 firmware using the local `sisfall-enhanced` dataset and Espressif `esp-dl`, then integrate the model outputs into the existing telemetry flow for RK3588-side fusion.

This design intentionally targets the first production-like milestone, not the final generalized solution. The first supported wearing position is **waist-mounted**. Wrist-mounted support is explicitly deferred because the current dataset and signal distribution are much closer to waist placement.

## Problem framing

The existing ESP firmware already samples `MPU6050` motion data and uploads telemetry. What it does not yet have is an on-device learned temporal classifier. A rule-based threshold detector is insufficient for the intended direction because it underuses the ESP32-S3 inference capability and does not align well with the existing RK3588 video-side temporal reasoning.

The design therefore uses a compact temporal neural network running directly on the ESP32-S3. The model predicts three classes for each rolling IMU window:

- `non_fall`
- `pre_impact`
- `fall`

The ESP does not collapse these three classes into a binary label. All three probabilities are preserved and reported upstream.

## Dataset assessment

### Local dataset contents

The local dataset under `/home/elf/workspace/imu_fall_detect/sisfall-enhanced` is not the raw SisFall text corpus. It is a preprocessed tensor bundle:

- `x_train_3`
- `x_val_3`
- `x_test_3`
- `y_train_3`
- `y_val_3`
- `y_test_3`
- `weights_3.txt`

The tensor files are raw binary arrays. Based on file sizes and direct decoding:

- `x_train_3` reshapes to approximately `(77871, 256, 6)` as `float32`
- `x_val_3` reshapes to approximately `(20233, 256, 6)` as `float32`
- `x_test_3` reshapes to approximately `(18884, 256, 6)` as `float32`
- `y_*_3` reshapes to `(N, 3)` as one-hot `uint8`

This strongly indicates that each sample is a `256 x 6` IMU window, where the six channels correspond to accelerometer and gyroscope axes.

### Label semantics

The dataset source page identifies the three classes as:

- `Non-fall`
- `Pre-impact fall / alert`
- `Fall`

That definition is accepted as the canonical meaning for the local dataset. No label remapping is performed in the first implementation.

### Class distribution

The local dataset is heavily imbalanced. The train split is approximately:

- `non_fall`: 74937 samples
- `pre_impact`: 696 samples
- `fall`: 2238 samples

This distribution means plain accuracy is not a valid success metric. The training plan must explicitly optimize for minority-class recall and macro metrics.

### Suitability for the current project

The dataset is suitable for the first waist-mounted ESP32-S3 deployment because:

- it already exposes fixed-size windows and labels compatible with a streaming IMU classifier
- it uses six IMU channels compatible with the `MPU6050`
- the original SisFall collection is waist-oriented, which matches the chosen first deployment position

The dataset is not suitable as a final universal model for arbitrary device placement because:

- the current product may later be worn on the wrist
- the local dataset preprocessing pipeline is not fully transparent
- domain shift between controlled public data and real board usage remains significant

The first milestone therefore uses this dataset as a deployment baseline, not as the final long-term data source.

## Deployment framework choice

### Rejected options

#### TFLite Micro as the primary path

This path is rejected for the first implementation because the user explicitly asked to analyze the Espressif `esp-dl` path and because the rest of the firmware is already targeting the Espressif ecosystem.

#### LSTM or GRU as the first deployed architecture

This path is rejected for the first ESP deployment because `esp-dl` is most practical with convolutional, linear, pooling, and activation-heavy graphs, and because streaming deployment support is centered around operators whose state can be managed efficiently for temporal convolution pipelines. A recurrent model may still be revisited later, but it is not the safest first deployment choice.

### Selected framework

The selected inference path is:

- train in `PyTorch`
- export to `ONNX`
- quantize with `ESP-PPQ`
- deploy as `.espdl`
- run inference on ESP32-S3 with `esp-dl`

This path is selected because it is the most aligned with Espressif documentation, quantization tooling, and streaming deployment workflow.

## Model design

### Selected architecture family

The first deployed model will be a compact 1D temporal convolution classifier, closer to a small TCN than an RNN.

A representative first version is:

- input: `256 x 6`
- `Conv1D(16, kernel=5, stride=1)` + batch norm + ReLU
- `Conv1D(24, kernel=5, stride=2)` + batch norm + ReLU
- `Conv1D(32, kernel=3, stride=2)` + batch norm + ReLU
- `SeparableConv1D(48, kernel=3, stride=1)` + ReLU
- `GlobalAveragePooling1D`
- `Dense(32)` + ReLU
- `Dense(3)` + softmax

The architecture may be slimmed further if `.espdl` memory or latency is too high, but the design must remain convolution-based.

### Why this architecture

It fits the target better than an RNN because it:

- is easier to quantize reliably to integer inference
- maps naturally to `esp-dl` operator support and streaming optimization
- has predictable latency and memory usage on ESP32-S3
- is sufficient for a first three-class waist-mounted classifier

## Input and sampling design

### Runtime sampling rate

The first deployment keeps the ESP runtime at **50 Hz**. This matches the current firmware default of `mpu6050_period_ms = 20` and keeps memory and compute bounded.

### Windowing

The runtime window is:

- window length: `256` samples
- channels: `6`
- sample rate: `50 Hz`
- effective window duration: `5.12 seconds`

### Sliding inference cadence

Inference uses a rolling window with a step of either:

- `16` samples (`320 ms`) for higher responsiveness
- or `32` samples (`640 ms`) if memory or CPU load needs to be reduced

The implementation should begin with `32` samples and move to `16` only if performance headroom is confirmed on board.

### Offline dataset alignment

Because the local enhanced dataset is already windowed, the first training iteration uses the provided windows directly. A follow-up verification script must still confirm the scale and temporal assumptions against the original dataset description. If future board testing shows a severe mismatch, the second phase will move back to raw SisFall reconstruction and explicit resampling.

## Training design

### Framework

Use `PyTorch` for all training and evaluation code.

### Loss

The default loss is weighted cross entropy using weights derived from the actual train split. The local `weights_3.txt` is treated as a reference artifact, not as the only source of truth.

If the minority `pre_impact` class remains unstable, the fallback is focal loss or class-balanced focal loss.

### Metrics

The training pipeline must report at least:

- overall accuracy
- macro F1
- per-class precision, recall, F1
- confusion matrix
- top-1 class distribution on validation and test

Primary gating metrics are:

- `macro F1`
- `fall recall`
- `pre_impact recall`

A model that has high accuracy but poor minority-class recall is rejected.

### Data augmentation

Use only light IMU-safe augmentation in the first phase:

- low-amplitude Gaussian noise
- small amplitude scaling
- mild temporal jitter or shift

Do not use aggressive axis mixing or orientation randomization in the first waist-only model because that would blur the very signal structure the model needs to learn.

### Validation policy

The provided train, validation, and test splits are used for the first iteration exactly as delivered. This keeps the first deployment aligned with the downloaded dataset.

A later refinement phase may revisit subject leakage concerns if the original split procedure can be reconstructed and audited.

## Quantization and export design

### Export path

The model pipeline is:

- `PyTorch .pt` checkpoint
- export to `ONNX`
- quantize with `ESP-PPQ`
- output `.espdl`

### Quantization choice

Use post-training quantization first. The primary target is integer inference suitable for ESP32-S3 memory constraints.

Quantization calibration should use a representative subset of validation windows with all three classes present. The calibration subset must not be dominated entirely by `non_fall`.

### Fallback path

If the first model loses too much `pre_impact` or `fall` recall after quantization, the next step is not to abandon `esp-dl`. The next step is to simplify the model architecture and retrain before trying a more complex model.

## ESP firmware integration design

### New firmware components

The ESP firmware gains three focused components:

- `imu_window_buffer`
  - collects `MPU6050` samples
  - maintains the `256 x 6` rolling window
  - emits windows on the selected step interval
- `fall_classifier`
  - owns the `esp-dl` model instance
  - accepts a window tensor
  - returns class logits or probabilities for `non_fall`, `pre_impact`, and `fall`
- `imu_event_state`
  - smooths successive window predictions
  - prevents unstable one-window oscillation
  - exposes the current class plus confidence values

### Data flow in firmware

1. `mpu6050` samples arrive at 50 Hz.
2. Samples are written into `imu_window_buffer`.
3. Every inference step, a `256 x 6` window is prepared.
4. `fall_classifier` runs the `.espdl` model.
5. `imu_event_state` smooths short-term prediction noise.
6. Telemetry includes the latest class and probabilities.

### Telemetry contract changes

The telemetry payload is extended with IMU fall fields:

- `imu_fall_class`
- `imu_nonfall_prob`
- `imu_preimpact_prob`
- `imu_fall_prob`
- optionally `imu_model_version`

The ESP does not decide the final alarm policy. RK3588 remains responsible for fusion with the video-side classifier.

## RK3588 integration design

The RK3588 side already consumes ESP telemetry and runs video inference. This design keeps the final decision at the host side.

The intended host-side use is:

- `non_fall`: no IMU alert contribution
- `pre_impact`: raises suspicion and can prime video-side review
- `fall`: strong IMU evidence that can boost or confirm a video-side fall assessment

This avoids overloading the ESP with final business logic and preserves explainability across both sensing modalities.

## Validation plan

### Offline validation

Before firmware integration, validate on the local dataset with:

- confusion matrix
- macro F1
- per-class PR curves if useful
- quantized-vs-float comparison

### On-device validation

Once `.espdl` integration is done, validate on board for:

- inference latency
- heap usage during model load and run
- stable repeated sliding-window inference
- telemetry field correctness

### End-to-end validation

After board integration, validate with RK3588 by verifying:

- telemetry delivery
- correct class serialization
- host-side receipt and logging
- consistent model outputs during simulated waist-mounted motion sequences

## Explicit non-goals for this phase

The following are intentionally out of scope for the first implementation:

- wrist-mounted generalization
- binary remapping of the three-class output
- RNN-first deployment
- self-collected custom dataset collection
- final decision fusion logic tuning on RK3588
- training from the raw SisFall text corpus from scratch

These may be addressed later, but they are not part of the first deployment milestone.

## Recommended implementation order

1. Create training scripts that load the existing `sisfall-enhanced` binary tensors.
2. Train and evaluate the compact 1D CNN three-class baseline on PC.
3. Export the best model to `ONNX`.
4. Quantize to `.espdl` using `ESP-PPQ`.
5. Add `imu_window_buffer`, `fall_classifier`, and `imu_event_state` to `esp_fw`.
6. Extend telemetry to include class and probabilities.
7. Run on-board performance and correctness checks.
8. Verify RK3588-side ingestion and logging.

## Success criteria

This design is considered successfully implemented when all of the following are true:

- the model trains successfully on the local `sisfall-enhanced` dataset
- the selected checkpoint exports cleanly to `ONNX`
- quantization produces a working `.espdl` model
- ESP32-S3 runs repeated sliding-window inference without instability
- firmware telemetry includes the three-class IMU outputs
- RK3588 receives and logs those outputs

## Sources

- Kaggle SisFall Enhanced dataset page: `https://www.kaggle.com/datasets/nvnikhil0001/sisfall-enhanced/data`
- SisFall original paper: `https://pmc.ncbi.nlm.nih.gov/articles/PMC5298771/`
- ESP-DL getting started: `https://docs.espressif.com/projects/esp-dl/en/latest/getting_started/readme.html`
- ESP-DL quantization tutorial: `https://docs.espressif.com/projects/esp-dl/en/latest/tutorials/how_to_quantize_model.html`
- ESP-DL streaming deployment tutorial: `https://docs.espressif.com/projects/esp-dl/en/latest/tutorials/how_to_deploy_streaming_model.html`
