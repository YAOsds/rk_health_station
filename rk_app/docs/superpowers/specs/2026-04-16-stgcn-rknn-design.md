# RK3588 Action Classifier Design

## 1. Purpose and Scope

This document defines the model strategy for action classification inside `health-falld` on `RK3588`.

The service architecture is already fixed:

- `health-videod` owns the camera and produces analysis frames
- `health-falld` owns pose inference, action classification, and fall-event decisions
- UI integration is out of scope for this phase

The goal of this document is narrower than the full backend architecture:

- keep the approved service split unchanged
- define the primary and fallback model paths for fall classification
- ensure model replacement does not affect the camera path, analysis socket, or event protocol

Out of scope:

- changing `health-videod`
- changing the analysis stream protocol
- UI work
- cloud or history features

## 2. Current State

The current online chain is conceptually:

```text
analysis frame
 -> yolov8n-pose.rknn
 -> 17-keypoint person
 -> 45-frame temporal buffer
 -> action classifier
 -> stand / fall / lie
```

The current pose model already provides enough raw data for all downstream paths:

- person bounding box
- person score
- `17` keypoints
- each keypoint includes `x`, `y`, and `score`

In the current codebase this shape is represented by:

- `rk_app/src/health_falld/pose/pose_types.h`
- `rk_app/src/health_falld/pose/rknn_pose_estimator.cpp`

The important conclusion is:

- the pose stage is not the blocker
- the classifier runtime strategy is the real design question

## 3. Confirmed Constraints

### 3.1 `ST-GCN -> RKNN` is still worth trying first

The existing preprocessing and temporal-window design already align well with `ST-GCN`:

- fixed `45`-frame window
- `17` keypoints
- per-keypoint `x`, `y`, `score`
- normalized skeleton-centered tensor already exists in the codebase

This makes `ST-GCN -> RKNN` the lowest-change primary path.

### 3.2 `OpenCV DNN` is no longer a valid fallback

The project should not retain `ONNX + OpenCV DNN + CPU` as the formal backup route.

Reasons:

- it keeps the deployment path split across incompatible runtime strategies
- it encourages spending engineering effort on a temporary branch
- it weakens the focus on board-ready RKNN deployment

Decision:

- `OpenCV DNN` is dropped as an official fallback path

### 3.3 Fallbacks must remain board-oriented and low-coupling

When the primary model path fails, fallback should still preserve:

- deployment on the board
- the same upstream pose interface
- the same `health-falld` classifier boundary

This leads to the required fallback tree:

1. `ST-GCN -> RKNN`
2. `LSTM -> RKNN`
3. `RuleBased`

## 4. Final Model Strategy

### 4.1 Primary path

Primary path:

- `ST-GCN -> RKNN`

This remains the first choice because it reuses the current temporal input contract with minimal upstream change.

### 4.2 First fallback

If `ST-GCN -> RKNN` is blocked at conversion, deployment, or quality level, switch immediately to:

- `LSTM -> RKNN`

This is not a side experiment. It is the formal first fallback.

### 4.3 Second fallback

If a trainable temporal model is not ready in time, use:

- `RuleBased`

This is the minimum shippable fallback used to guarantee an end-to-end fall-event path on the board.

### 4.4 Removed option

Explicitly removed:

- `OpenCV DNN` as a formal runtime fallback

## 5. Action Classifier Architecture

The `health-falld` boundary stays unchanged. Only the classifier implementation behind it is allowed to change.

Recommended structure:

```text
ActionClassifier
  |- StgcnRknnActionClassifier
  |- LstmRknnActionClassifier
  `- RuleBasedActionClassifier
```

Shared upstream inputs:

- selected primary person
- ordered temporal sequence
- normalized keypoint data

Must remain outside classifier backends:

- camera ownership
- frame transport
- pose inference ownership
- event transport protocol

This preserves low coupling and keeps model replacement local to `health-falld`.

## 6. Unified Input Contract

### 6.1 Principle

Trainable model paths should use the simplest possible input that comes directly from pose output.

The design goal is:

- do not engineer many extra features for `LSTM`
- do not create a second upstream data contract just for fallback
- keep `ST-GCN` and `LSTM` aligned on the same temporal skeleton representation

### 6.2 Raw pose data available per frame

Per frame, the pose stage already gives:

- `17` keypoints
- each keypoint as `x`, `y`, `score`

This means the core sequence data can be represented as:

- `17 x 3` per frame
- over `45` frames

### 6.3 Normalized sequence representation

The recommended common representation for trainable classifiers is:

- relative-to-mid-hip normalized `x`
- relative-to-mid-hip normalized `y`
- keypoint confidence `score`

Over time this gives the same semantic tensor already used by the current `ST-GCN` preprocessing path:

- `3 x 45 x 17`

This is considered a light normalization step, not heavy feature engineering.

### 6.4 Model-specific view of the same data

For `ST-GCN`:

- consume the sequence as `3 x 45 x 17`

For `LSTM`:

- consume the same sequence flattened per frame
- equivalent shape: `45 x 51`

This gives both models the same information while minimizing code churn.

## 7. Primary Path: `ST-GCN -> RKNN`

### 7.1 Why it is still first

`ST-GCN` remains first because:

- the current preprocessing path already matches it
- the current window length already matches it
- changing only the classifier backend is lower-risk than redesigning all temporal logic first

### 7.2 Known risk

The model-conversion path is still risky because:

- the current ONNX artifact was incomplete
- the host environment had package mismatch
- the exported graph included `Einsum`

This means `ST-GCN -> RKNN` should be attempted first, but only within a controlled effort budget.

### 7.3 Gate for continuation

If the conversion/deployment effort starts requiring disproportionate graph surgery or unstable runtime handling, stop and switch to `LSTM`.

The project should not become hostage to one graph operator problem.

## 8. First Fallback: `LSTM -> RKNN`

### 8.1 Why `LSTM` is the right fallback

`LSTM` is preferred over `OpenCV DNN` fallback because:

- it remains a board-oriented RKNN path
- it uses a simpler temporal model family
- it can reuse the same upstream sequence contract
- it avoids maintaining a separate CPU inference deployment line

### 8.2 Input strategy

`LSTM` should not depend on many handcrafted features.

Recommended input:

- exactly the same normalized keypoint sequence used by `ST-GCN`

Operationally:

- reuse the `3 x 45 x 17` semantic input
- flatten each frame into `51` values when required by the model

This keeps the fallback cheap:

- same source data
- same temporal buffer
- same normalization logic
- no extra pose-side dependencies

### 8.3 Acceptable model simplicity

The fallback `LSTM` should prioritize deployability over model novelty.

Recommended direction:

- `1` or `2` LSTM layers
- lightweight hidden dimension
- small classifier head
- output labels: `stand`, `fall`, `lie`

This model is a deployment fallback, not a research target.

## 9. Second Fallback: `RuleBased`

### 9.1 Role

`RuleBased` is the minimum shippable safety net.

Its purpose is:

- keep the board-side event path alive
- validate end-to-end daemon behavior
- provide a practical fallback if trainable temporal models are not ready

### 9.2 Data strategy

Unlike `LSTM`, the rule path is allowed to compute more derived signals.

Examples:

- hip center height trend
- torso angle trend
- bbox aspect ratio
- low-position persistence
- keypoint reliability checks

This is appropriate because rules need explicit geometry and motion thresholds to be useful.

### 9.3 Minimal target

The rule path only needs to detect clear falls with acceptable stability.

It does not need to match the best achievable model accuracy before it can be useful.

## 10. Gate and Switch Rules

The decision tree must be explicit.

### Gate A - Conversion feasibility

If `ST-GCN` cannot be converted into a stable RKNN artifact:

- stop the `ST-GCN` path
- switch to `LSTM`

### Gate B - Board runtime stability

If `ST-GCN RKNN` converts but fails to run reliably on RK3588:

- stop the `ST-GCN` path
- switch to `LSTM`

### Gate C - Business readiness

If `ST-GCN RKNN` runs but fails accuracy, latency, or maintainability expectations:

- stop the `ST-GCN` path
- switch to `LSTM`

### Gate D - Schedule protection

If `LSTM` also cannot be stabilized within the project window:

- activate `RuleBased`
- keep the external interfaces unchanged

## 11. Verification Strategy

### 11.1 For `ST-GCN`

Validate in this order:

1. clean ONNX export
2. host-side RKNN conversion
3. board-side model load
4. board-side inference stability
5. classification usefulness

### 11.2 For `LSTM`

Validate in this order:

1. sequence input contract reuse
2. clean RKNN conversion
3. board-side inference stability
4. label usefulness

### 11.3 For `RuleBased`

Validate in this order:

1. reliable feature extraction from pose sequence
2. threshold stability on a small replay set
3. event emission stability in daemon runtime

## 12. Final Recommendation

The final delivery path is:

```text
ST-GCN -> RKNN
    if blocked
 -> LSTM -> RKNN
    if blocked by schedule or stability
 -> RuleBased
```

Core constraints that never change:

- keep `health-videod` untouched
- keep the analysis socket contract unchanged
- keep model replacement internal to `health-falld`
- do not reintroduce `OpenCV DNN` as a formal fallback path

This gives the project a clear primary path, a realistic first fallback, and a guaranteed deployable minimum path.
