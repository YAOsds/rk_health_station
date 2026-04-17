# RK3588 Action Classifier Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current single-path action classifier with a board-oriented classifier stack that tries `ST-GCN -> RKNN` first, falls back to `LSTM -> RKNN` when needed, and keeps a `RuleBased` backend as the minimum deployable path, without changing the existing `health-videod -> analysis socket -> health-falld` architecture.

**Architecture:** Keep the current pose and frame-ingest pipeline intact inside `health-falld`, and limit all classifier changes to a new backend-selection layer plus small runtime configuration additions. Reuse the existing normalized `3 x 45 x 17` skeleton tensor for both `ST-GCN` and `LSTM`, and reserve extra derived features for the rule backend only.

**Tech Stack:** C++17, Qt Core/Network/Test, RKNN Runtime, CMake/CTest, Python 3.10, PyTorch, ONNX, RKNN-Toolkit2 2.3.2, pytest.

---

## File Structure Map

### Existing files to modify

- `src/health_falld/action/action_classifier.h`
  - widen the backend interface so model and non-model backends share one contract
- `src/health_falld/action/stgcn_preprocessor.h`
- `src/health_falld/action/stgcn_preprocessor.cpp`
  - become the shared normalized skeleton tensor builder for trainable backends
- `src/health_falld/app/fall_daemon_app.cpp`
  - use a backend factory instead of constructing `StgcnActionClassifier` directly
- `src/health_falld/CMakeLists.txt`
  - compile new classifier files and backend-specific feature flags
- `src/health_falld/runtime/runtime_config.h`
- `src/health_falld/runtime/runtime_config.cpp`
  - add backend kind and backend-specific model paths
- `src/tests/CMakeLists.txt`
  - register new fall-daemon and python-side verification targets as needed
- `src/tests/fall_daemon_tests/fall_detector_service_test.cpp`
  - extend coverage for new backend contract behavior
- `src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`
  - assert backend selection and degraded startup states
- `yolo_detect/stgcn/export_onnx.py`
  - export a static single-file ONNX artifact suitable for RKNN probing

### New runtime files

- `src/health_falld/action/action_backend_kind.h`
- `src/health_falld/action/action_classifier_factory.h`
- `src/health_falld/action/action_classifier_factory.cpp`
- `src/health_falld/action/rknn_stgcn_action_classifier.h`
- `src/health_falld/action/rknn_stgcn_action_classifier.cpp`
- `src/health_falld/action/rknn_lstm_action_classifier.h`
- `src/health_falld/action/rknn_lstm_action_classifier.cpp`
- `src/health_falld/action/rule_based_action_classifier.h`
- `src/health_falld/action/rule_based_action_classifier.cpp`
- `src/tests/fall_daemon_tests/action_classifier_factory_test.cpp`
- `src/tests/fall_daemon_tests/rule_based_action_classifier_test.cpp`
- `src/tests/fall_daemon_tests/rknn_lstm_tensor_shape_test.cpp`

### New training / conversion files

- `yolo_detect/stgcn/export_static_onnx.py`
- `yolo_detect/stgcn/convert_rknn.py`
- `yolo_detect/lstm/model.py`
- `yolo_detect/lstm/train.py`
- `yolo_detect/lstm/export_onnx.py`
- `yolo_detect/lstm/convert_rknn.py`
- `yolo_detect/tests/test_stgcn_static_export.py`
- `yolo_detect/tests/test_lstm_input_contract.py`

## Notes Before Implementation

- This tree currently has no `.git` metadata, so each "Commit" step below should be treated as a checkpoint command to run if the files are moved into a git worktree later.
- Keep the existing `health-videod` analysis socket contract unchanged throughout this plan.
- Do not preserve `OpenCV DNN` as a runtime fallback path; any remaining CPU/ONNX logic should be deleted or retired as part of the classifier migration.

### Task 1: Add explicit backend selection to `health-falld`

**Files:**
- Create: `src/health_falld/action/action_backend_kind.h`
- Create: `src/health_falld/action/action_classifier_factory.h`
- Create: `src/health_falld/action/action_classifier_factory.cpp`
- Modify: `src/health_falld/runtime/runtime_config.h`
- Modify: `src/health_falld/runtime/runtime_config.cpp`
- Modify: `src/health_falld/app/fall_daemon_app.cpp`
- Modify: `src/health_falld/CMakeLists.txt`
- Test: `src/tests/fall_daemon_tests/action_classifier_factory_test.cpp`
- Modify: `src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing factory/config test**

```cpp
// src/tests/fall_daemon_tests/action_classifier_factory_test.cpp
#include "action/action_classifier_factory.h"
#include "runtime/runtime_config.h"

#include <QtTest/QTest>

class ActionClassifierFactoryTest : public QObject {
    Q_OBJECT

private slots:
    void defaultsToStgcnRknn();
    void parsesRuleBackendFromEnv();
};

void ActionClassifierFactoryTest::defaultsToStgcnRknn() {
    qunsetenv("RK_FALL_ACTION_BACKEND");
    const FallRuntimeConfig config = loadFallRuntimeConfig();
    QCOMPARE(config.actionBackend, ActionBackendKind::StgcnRknn);
}

void ActionClassifierFactoryTest::parsesRuleBackendFromEnv() {
    qputenv("RK_FALL_ACTION_BACKEND", QByteArray("rule_based"));
    const FallRuntimeConfig config = loadFallRuntimeConfig();
    QCOMPARE(config.actionBackend, ActionBackendKind::RuleBased);
    qunsetenv("RK_FALL_ACTION_BACKEND");
}

QTEST_MAIN(ActionClassifierFactoryTest)
#include "action_classifier_factory_test.moc"
```

- [ ] **Step 2: Register and run the new test to verify it fails**

```cmake
# src/tests/CMakeLists.txt
add_executable(action_classifier_factory_test
    fall_daemon_tests/action_classifier_factory_test.cpp
    ../health_falld/runtime/runtime_config.cpp
)
set_target_properties(action_classifier_factory_test PROPERTIES AUTOMOC ON)
target_include_directories(action_classifier_factory_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(action_classifier_factory_test PRIVATE
    ${RK_QT_CORE_TARGET}
    ${RK_QT_TEST_TARGET}
)
add_test(NAME action_classifier_factory_test COMMAND action_classifier_factory_test)
```

Run:

```bash
cmake -S /home/elf/workspace/QTtest/Qt例程源码/rk_health_station/rk_app -B /tmp/rk_health_station-build
cmake --build /tmp/rk_health_station-build --target action_classifier_factory_test -j4
ctest --test-dir /tmp/rk_health_station-build -R action_classifier_factory_test --output-on-failure
```

Expected: FAIL because `ActionBackendKind` and `FallRuntimeConfig::actionBackend` do not exist yet.

- [ ] **Step 3: Add the backend enum, runtime config parsing, and classifier factory shell**

```cpp
// src/health_falld/action/action_backend_kind.h
#pragma once

enum class ActionBackendKind {
    StgcnRknn,
    LstmRknn,
    RuleBased,
};
```

```cpp
// src/health_falld/runtime/runtime_config.h
struct FallRuntimeConfig {
    QString cameraId = QStringLiteral("front_cam");
    QString socketName = QStringLiteral("rk_fall.sock");
    QString analysisSocketPath = QStringLiteral("/tmp/rk_video_analysis.sock");
    QString poseModelPath = QStringLiteral("assets/models/yolov8n-pose.rknn");
    QString stgcnModelPath = QStringLiteral("assets/models/stgcn_fall.rknn");
    QString lstmModelPath = QStringLiteral("assets/models/lstm_fall.rknn");
    ActionBackendKind actionBackend = ActionBackendKind::StgcnRknn;
    int sequenceLength = 45;
    bool enabled = true;
};
```

```cpp
// src/health_falld/runtime/runtime_config.cpp
static ActionBackendKind parseActionBackend(const QString &value) {
    if (value == QStringLiteral("lstm_rknn")) {
        return ActionBackendKind::LstmRknn;
    }
    if (value == QStringLiteral("rule_based")) {
        return ActionBackendKind::RuleBased;
    }
    return ActionBackendKind::StgcnRknn;
}
```

```cpp
// src/health_falld/action/action_classifier_factory.h
#pragma once

#include "action/action_classifier.h"
#include "runtime/runtime_config.h"

#include <memory>

std::unique_ptr<ActionClassifier> createActionClassifier(const FallRuntimeConfig &config);
```

- [ ] **Step 4: Wire `FallDaemonApp` to the factory and rerun the test**

```cpp
// src/health_falld/app/fall_daemon_app.cpp
#include "action/action_classifier_factory.h"

FallDaemonApp::FallDaemonApp(QObject *parent)
    : QObject(parent)
    , config_(loadFallRuntimeConfig())
    , poseEstimator_(std::make_unique<RknnPoseEstimator>())
    , actionClassifier_(createActionClassifier(config_))
    , detectorService_(actionClassifier_.get())
    // keep the rest unchanged
{
}
```

Run:

```bash
cmake --build /tmp/rk_health_station-build --target action_classifier_factory_test -j4
ctest --test-dir /tmp/rk_health_station-build -R action_classifier_factory_test --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/action/action_backend_kind.h \
  src/health_falld/action/action_classifier_factory.h \
  src/health_falld/action/action_classifier_factory.cpp \
  src/health_falld/runtime/runtime_config.h \
  src/health_falld/runtime/runtime_config.cpp \
  src/health_falld/app/fall_daemon_app.cpp \
  src/tests/CMakeLists.txt \
  src/tests/fall_daemon_tests/action_classifier_factory_test.cpp
git commit -m "feat: add action backend selection"
```

### Task 2: Make the normalized skeleton tensor the shared model input contract

**Files:**
- Modify: `src/health_falld/action/action_classifier.h`
- Modify: `src/health_falld/action/stgcn_preprocessor.h`
- Modify: `src/health_falld/action/stgcn_preprocessor.cpp`
- Modify: `src/tests/fall_daemon_tests/stgcn_preprocessor_test.cpp`
- Test: `src/tests/fall_daemon_tests/rknn_lstm_tensor_shape_test.cpp`
- Modify: `src/tests/CMakeLists.txt`

- [ ] **Step 1: Add the failing tensor-contract test**

```cpp
// src/tests/fall_daemon_tests/rknn_lstm_tensor_shape_test.cpp
#include "action/stgcn_preprocessor.h"

#include <QtTest/QTest>

class RknnLstmTensorShapeTest : public QObject {
    Q_OBJECT

private slots:
    void flattensSharedTensorTo45x51();
};

void RknnLstmTensorShapeTest::flattensSharedTensorTo45x51() {
    StgcnInputTensor tensor;
    tensor.channels = 3;
    tensor.frames = 45;
    tensor.joints = 17;
    tensor.values.fill(1.0f, 3 * 45 * 17);

    const QVector<float> flattened = flattenSkeletonSequenceForLstm(tensor);
    QCOMPARE(flattened.size(), 45 * 51);
}

QTEST_MAIN(RknnLstmTensorShapeTest)
#include "rknn_lstm_tensor_shape_test.moc"
```

- [ ] **Step 2: Register and run the test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target rknn_lstm_tensor_shape_test -j4
ctest --test-dir /tmp/rk_health_station-build -R rknn_lstm_tensor_shape_test --output-on-failure
```

Expected: FAIL because `flattenSkeletonSequenceForLstm(...)` does not exist yet.

- [ ] **Step 3: Add the shared trainable-model helpers**

```cpp
// src/health_falld/action/stgcn_preprocessor.h
struct StgcnInputTensor {
    int channels = 0;
    int frames = 0;
    int joints = 0;
    QVector<float> values;
};

bool buildStgcnInputTensor(
    const QVector<PosePerson> &sequence, StgcnInputTensor *tensor, QString *error);

QVector<float> flattenSkeletonSequenceForLstm(const StgcnInputTensor &tensor);
```

```cpp
// src/health_falld/action/stgcn_preprocessor.cpp
QVector<float> flattenSkeletonSequenceForLstm(const StgcnInputTensor &tensor) {
    QVector<float> output;
    if (tensor.channels != 3 || tensor.frames != 45 || tensor.joints != 17) {
        return output;
    }

    output.resize(tensor.frames * tensor.channels * tensor.joints);
    int offset = 0;
    for (int frame = 0; frame < tensor.frames; ++frame) {
        for (int joint = 0; joint < tensor.joints; ++joint) {
            for (int channel = 0; channel < tensor.channels; ++channel) {
                output[offset++] = tensor.values[((channel * tensor.frames) + frame) * tensor.joints + joint];
            }
        }
    }
    return output;
}
```

- [ ] **Step 4: Update the tests and rerun them**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target stgcn_preprocessor_test rknn_lstm_tensor_shape_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'stgcn_preprocessor_test|rknn_lstm_tensor_shape_test' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/action/action_classifier.h \
  src/health_falld/action/stgcn_preprocessor.h \
  src/health_falld/action/stgcn_preprocessor.cpp \
  src/tests/CMakeLists.txt \
  src/tests/fall_daemon_tests/stgcn_preprocessor_test.cpp \
  src/tests/fall_daemon_tests/rknn_lstm_tensor_shape_test.cpp
git commit -m "feat: share normalized tensor contract across trainable backends"
```

### Task 3: Implement the minimum shippable `RuleBased` backend

**Files:**
- Create: `src/health_falld/action/rule_based_action_classifier.h`
- Create: `src/health_falld/action/rule_based_action_classifier.cpp`
- Modify: `src/health_falld/action/action_classifier_factory.cpp`
- Test: `src/tests/fall_daemon_tests/rule_based_action_classifier_test.cpp`
- Modify: `src/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing rule-backend tests**

```cpp
// src/tests/fall_daemon_tests/rule_based_action_classifier_test.cpp
#include "action/rule_based_action_classifier.h"

#include <QtTest/QTest>

class RuleBasedActionClassifierTest : public QObject {
    Q_OBJECT

private slots:
    void returnsMonitoringWhenWindowIsEmpty();
    void emitsFallWhenHipDropsAndTorsoTurnsFlat();
};

void RuleBasedActionClassifierTest::returnsMonitoringWhenWindowIsEmpty() {
    RuleBasedActionClassifier classifier;
    QString error;
    const ActionClassification result = classifier.classify({}, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(result.label, QStringLiteral("monitoring"));
}

void RuleBasedActionClassifierTest::emitsFallWhenHipDropsAndTorsoTurnsFlat() {
    RuleBasedActionClassifier classifier;
    QString error;
    const QVector<PosePerson> sequence = makeSyntheticFallSequence(); // add helper in test file
    const ActionClassification result = classifier.classify(sequence, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(result.label == QStringLiteral("fall") || result.label == QStringLiteral("lie"));
}

QTEST_MAIN(RuleBasedActionClassifierTest)
#include "rule_based_action_classifier_test.moc"
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target rule_based_action_classifier_test -j4
ctest --test-dir /tmp/rk_health_station-build -R rule_based_action_classifier_test --output-on-failure
```

Expected: FAIL because `RuleBasedActionClassifier` does not exist yet.

- [ ] **Step 3: Implement the minimal rule backend**

```cpp
// src/health_falld/action/rule_based_action_classifier.h
#pragma once

#include "action/action_classifier.h"

class RuleBasedActionClassifier : public ActionClassifier {
public:
    bool loadModel(const QString &path, QString *error) override;
    ActionClassification classify(const QVector<PosePerson> &sequence, QString *error) override;
};
```

```cpp
// src/health_falld/action/rule_based_action_classifier.cpp
bool RuleBasedActionClassifier::loadModel(const QString &path, QString *error) {
    Q_UNUSED(path);
    if (error) {
        error->clear();
    }
    return true;
}
```

Implementation rule set:

- compute mid-hip per frame
- compute torso angle from shoulder center to hip center
- detect a fall-like window when hip drops sharply and torso angle moves toward horizontal
- return `monitoring` when reliability is too low

- [ ] **Step 4: Add factory wiring and rerun the tests**

```cpp
// src/health_falld/action/action_classifier_factory.cpp
if (config.actionBackend == ActionBackendKind::RuleBased) {
    return std::make_unique<RuleBasedActionClassifier>();
}
```

Run:

```bash
cmake --build /tmp/rk_health_station-build --target rule_based_action_classifier_test action_classifier_factory_test -j4
ctest --test-dir /tmp/rk_health_station-build -R 'rule_based_action_classifier_test|action_classifier_factory_test' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/action/rule_based_action_classifier.h \
  src/health_falld/action/rule_based_action_classifier.cpp \
  src/health_falld/action/action_classifier_factory.cpp \
  src/tests/CMakeLists.txt \
  src/tests/fall_daemon_tests/rule_based_action_classifier_test.cpp
git commit -m "feat: add rule-based action classifier fallback"
```

### Task 4: Produce a static single-file `ST-GCN` ONNX export and RKNN probe

**Files:**
- Create: `yolo_detect/stgcn/export_static_onnx.py`
- Create: `yolo_detect/stgcn/convert_rknn.py`
- Modify: `yolo_detect/stgcn/export_onnx.py`
- Test: `yolo_detect/tests/test_stgcn_static_export.py`

- [ ] **Step 1: Add the failing export-contract test**

```python
# yolo_detect/tests/test_stgcn_static_export.py
from pathlib import Path

import onnx


def test_static_export_has_fixed_shape_and_no_external_data(tmp_path):
    from yolo_detect.stgcn.export_static_onnx import export_static_onnx

    out_path = tmp_path / "stgcn_fall_static.onnx"
    export_static_onnx(out_path)

    model = onnx.load(out_path, load_external_data=False)
    dims = [d.dim_value for d in model.graph.input[0].type.tensor_type.shape.dim]
    assert dims == [1, 3, 45, 17]
    assert out_path.with_suffix(".onnx.data").exists() is False
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cd /home/elf/workspace/rknn_model_zoo-2.1.0
pytest yolo_detect/tests/test_stgcn_static_export.py -q
```

Expected: FAIL because `export_static_onnx.py` does not exist yet.

- [ ] **Step 3: Implement static export and RKNN probe scripts**

```python
# yolo_detect/stgcn/export_static_onnx.py
def export_static_onnx(out_path: Path) -> Path:
    model = STGCN(num_classes=3)
    ckpt = torch.load(CKPT_PATH, map_location="cpu", weights_only=True)
    model.load_state_dict(ckpt["model_state_dict"])
    model.eval()
    dummy = torch.randn(1, 3, 45, 17)
    torch.onnx.export(
        model,
        dummy,
        str(out_path),
        opset_version=17,
        input_names=["keypoints"],
        output_names=["logits"],
        dynamic_axes=None,
        external_data=False,
    )
    return out_path
```

```python
# yolo_detect/stgcn/convert_rknn.py
def convert_stgcn_rknn(onnx_path: Path, out_path: Path) -> int:
    rknn = RKNN(verbose=True)
    rknn.config(target_platform="rk3588")
    ret = rknn.load_onnx(model=str(onnx_path))
    if ret != 0:
        return ret
    ret = rknn.build(do_quantization=False)
    if ret != 0:
        return ret
    ret = rknn.export_rknn(str(out_path))
    rknn.release()
    return ret
```

- [ ] **Step 4: Rerun export verification**

Run:

```bash
cd /home/elf/workspace/rknn_model_zoo-2.1.0
pytest yolo_detect/tests/test_stgcn_static_export.py -q
python yolo_detect/stgcn/export_static_onnx.py
```

Expected: pytest PASS; script writes a single-file `.onnx`.

- [ ] **Step 5: Checkpoint**

```bash
git add yolo_detect/stgcn/export_static_onnx.py \
  yolo_detect/stgcn/convert_rknn.py \
  yolo_detect/stgcn/export_onnx.py \
  yolo_detect/tests/test_stgcn_static_export.py
git commit -m "feat: add static ST-GCN export and RKNN probe"
```

### Task 5: Integrate `ST-GCN RKNN` into `health-falld`

**Files:**
- Create: `src/health_falld/action/rknn_stgcn_action_classifier.h`
- Create: `src/health_falld/action/rknn_stgcn_action_classifier.cpp`
- Modify: `src/health_falld/action/action_classifier_factory.cpp`
- Modify: `src/health_falld/CMakeLists.txt`
- Modify: `src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`

- [ ] **Step 1: Add the failing startup-status test**

```cpp
// src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
void FallEndToEndStatusTest::reportsActionModelNotReadyWhenRknnModelMissing() {
    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_missing_stgcn.sock"));
    qputenv("RK_FALL_ACTION_BACKEND", QByteArray("stgcn_rknn"));
    qputenv("RK_FALL_STGCN_MODEL_PATH", QByteArray("missing_stgcn.rknn"));

    FallDaemonApp app;
    QVERIFY(app.start());

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("/tmp/rk_fall_missing_stgcn.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 2000);

    const QByteArray payload = socket.readAll();
    QVERIFY(payload.contains("\"action_model_ready\":false"));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-build -R fall_end_to_end_status_test --output-on-failure
```

Expected: FAIL because `stgcn_rknn` is not a supported backend yet.

- [ ] **Step 3: Implement the RKNN ST-GCN backend**

```cpp
// src/health_falld/action/rknn_stgcn_action_classifier.h
#pragma once

#include "action/action_classifier.h"

class RknnStgcnActionClassifier : public ActionClassifier {
public:
    ~RknnStgcnActionClassifier() override;
    bool loadModel(const QString &path, QString *error) override;
    ActionClassification classify(const QVector<PosePerson> &sequence, QString *error) override;

private:
    void *runtime_ = nullptr;
};
```

Implementation requirements:

- reuse `buildStgcnInputTensor(...)`
- bind one `float32` input tensor with shape `1 x 3 x 45 x 17`
- read `3` output logits
- reuse the existing `stand / fall / lie` softmax mapping

- [ ] **Step 4: Wire the factory and rerun the test**

```cpp
// src/health_falld/action/action_classifier_factory.cpp
if (config.actionBackend == ActionBackendKind::StgcnRknn) {
    return std::make_unique<RknnStgcnActionClassifier>();
}
```

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-build -R fall_end_to_end_status_test --output-on-failure
```

Expected: PASS, with missing-model startup reported as degraded rather than crashing.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/action/rknn_stgcn_action_classifier.h \
  src/health_falld/action/rknn_stgcn_action_classifier.cpp \
  src/health_falld/action/action_classifier_factory.cpp \
  src/health_falld/CMakeLists.txt \
  src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
git commit -m "feat: add RKNN ST-GCN classifier backend"
```

### Task 6: Add the `LSTM` training/export path using the same skeleton input contract

**Files:**
- Create: `yolo_detect/lstm/model.py`
- Create: `yolo_detect/lstm/train.py`
- Create: `yolo_detect/lstm/export_onnx.py`
- Create: `yolo_detect/lstm/convert_rknn.py`
- Test: `yolo_detect/tests/test_lstm_input_contract.py`

- [ ] **Step 1: Add the failing contract test**

```python
# yolo_detect/tests/test_lstm_input_contract.py
import torch


def test_lstm_accepts_45_by_51_sequence():
    from yolo_detect.lstm.model import FallLstm

    model = FallLstm(input_size=51, hidden_size=64, num_layers=1, num_classes=3)
    x = torch.randn(2, 45, 51)
    y = model(x)
    assert tuple(y.shape) == (2, 3)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cd /home/elf/workspace/rknn_model_zoo-2.1.0
pytest yolo_detect/tests/test_lstm_input_contract.py -q
```

Expected: FAIL because `yolo_detect/lstm/model.py` does not exist yet.

- [ ] **Step 3: Implement the minimal LSTM stack**

```python
# yolo_detect/lstm/model.py
class FallLstm(nn.Module):
    def __init__(self, input_size=51, hidden_size=64, num_layers=1, num_classes=3):
        super().__init__()
        self.lstm = nn.LSTM(
            input_size=input_size,
            hidden_size=hidden_size,
            num_layers=num_layers,
            batch_first=True,
        )
        self.head = nn.Linear(hidden_size, num_classes)

    def forward(self, x):
        out, _ = self.lstm(x)
        return self.head(out[:, -1, :])
```

`export_onnx.py` must export:

- input shape `1 x 45 x 51`
- output shape `1 x 3`
- single-file ONNX

- [ ] **Step 4: Rerun the test and one export smoke**

Run:

```bash
cd /home/elf/workspace/rknn_model_zoo-2.1.0
pytest yolo_detect/tests/test_lstm_input_contract.py -q
python yolo_detect/lstm/export_onnx.py
```

Expected: PASS; an ONNX file is written for RKNN probing.

- [ ] **Step 5: Checkpoint**

```bash
git add yolo_detect/lstm/model.py \
  yolo_detect/lstm/train.py \
  yolo_detect/lstm/export_onnx.py \
  yolo_detect/lstm/convert_rknn.py \
  yolo_detect/tests/test_lstm_input_contract.py
git commit -m "feat: add LSTM RKNN fallback training pipeline"
```

### Task 7: Integrate the `LSTM RKNN` backend and final degraded-startup path

**Files:**
- Create: `src/health_falld/action/rknn_lstm_action_classifier.h`
- Create: `src/health_falld/action/rknn_lstm_action_classifier.cpp`
- Modify: `src/health_falld/action/action_classifier_factory.cpp`
- Modify: `src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp`

- [ ] **Step 1: Add the failing LSTM-backend selection test**

```cpp
// extend src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
void FallEndToEndStatusTest::startsWithRuleBackendWhenConfiguredExplicitly() {
    qputenv("RK_FALL_SOCKET_NAME", QByteArray("/tmp/rk_fall_rule_backend.sock"));
    qputenv("RK_FALL_ACTION_BACKEND", QByteArray("rule_based"));

    FallDaemonApp app;
    QVERIFY(app.start());

    QLocalSocket socket;
    socket.connectToServer(QStringLiteral("/tmp/rk_fall_rule_backend.sock"));
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{\"action\":\"get_runtime_status\"}\n");
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0 || socket.waitForReadyRead(50), 2000);

    const QByteArray payload = socket.readAll();
    QVERIFY(payload.contains("\"action_model_ready\":true"));
}
```

- [ ] **Step 2: Run the test suite to verify current behavior is incomplete**

Run:

```bash
cmake --build /tmp/rk_health_station-build --target fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-build -R fall_end_to_end_status_test --output-on-failure
```

Expected: FAIL because the factory does not yet handle all three backend kinds cleanly.

- [ ] **Step 3: Implement the RKNN LSTM backend**

```cpp
// src/health_falld/action/rknn_lstm_action_classifier.cpp
ActionClassification RknnLstmActionClassifier::classify(const QVector<PosePerson> &sequence, QString *error) {
    StgcnInputTensor tensor;
    if (!buildStgcnInputTensor(sequence, &tensor, error)) {
        return {};
    }
    const QVector<float> flattened = flattenSkeletonSequenceForLstm(tensor);
    // bind flattened as 1 x 45 x 51 float32 input
    // run RKNN
    // map logits to stand/fall/lie
}
```

- [ ] **Step 4: Update the factory and rerun the suite**

```cpp
// src/health_falld/action/action_classifier_factory.cpp
if (config.actionBackend == ActionBackendKind::LstmRknn) {
    return std::make_unique<RknnLstmActionClassifier>();
}
```

Run:

```bash
cmake --build /tmp/rk_health_station-build --target \
  action_classifier_factory_test \
  rule_based_action_classifier_test \
  fall_end_to_end_status_test -j4
ctest --test-dir /tmp/rk_health_station-build -R \
  'action_classifier_factory_test|rule_based_action_classifier_test|fall_end_to_end_status_test' \
  --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Checkpoint**

```bash
git add src/health_falld/action/rknn_lstm_action_classifier.h \
  src/health_falld/action/rknn_lstm_action_classifier.cpp \
  src/health_falld/action/action_classifier_factory.cpp \
  src/tests/fall_daemon_tests/fall_end_to_end_status_test.cpp
git commit -m "feat: add RKNN LSTM fallback backend"
```

## Self-Review Notes

- Spec coverage:
  - `ST-GCN -> RKNN` primary path is covered by Task 4 and Task 5.
  - `LSTM -> RKNN` first fallback is covered by Task 6 and Task 7.
  - `RuleBased` minimum deployable fallback is covered by Task 3.
  - shared `3 x 45 x 17` / `45 x 51` input contract is covered by Task 2.
  - preserving the existing daemon boundary is covered by Task 1 and maintained throughout all runtime tasks.
- Placeholder scan:
  - no `TODO` / `TBD` markers remain in the plan body.
- Type consistency:
  - `ActionBackendKind`, `FallRuntimeConfig::actionBackend`, `buildStgcnInputTensor(...)`, and `flattenSkeletonSequenceForLstm(...)` are used consistently across all tasks.
