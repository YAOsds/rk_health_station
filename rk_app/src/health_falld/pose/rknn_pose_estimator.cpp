#include "pose/rknn_pose_estimator.h"
#include "pose/nv12_preprocessor.h"
#include "pose/pose_stage_timing_logger.h"

#ifdef RKAPP_ENABLE_REAL_RKNN_POSE
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>

#include "yolov8-pose.h"

struct RknnPoseRuntime {
    rknn_app_context_t appCtx;
    bool postProcessReady = false;
};

namespace {
const char kPoseTimingPathEnvVar[] = "RK_FALL_POSE_TIMING_PATH";

void releaseRuntime(RknnPoseRuntime *runtime) {
    if (!runtime) {
        return;
    }

    if (runtime->appCtx.input_attrs != nullptr) {
        free(runtime->appCtx.input_attrs);
        runtime->appCtx.input_attrs = nullptr;
    }
    if (runtime->appCtx.output_attrs != nullptr) {
        free(runtime->appCtx.output_attrs);
        runtime->appCtx.output_attrs = nullptr;
    }
    if (runtime->appCtx.rknn_ctx != 0) {
        rknn_destroy(runtime->appCtx.rknn_ctx);
        runtime->appCtx.rknn_ctx = 0;
    }
    if (runtime->postProcessReady) {
        deinit_post_process();
        runtime->postProcessReady = false;
    }
    delete runtime;
}

PosePreprocessResult preprocessDecodedImageForPose(
    const QImage &inputImage, int targetWidth, int targetHeight, QString *error) {
    PosePreprocessResult result;
    if (error) {
        error->clear();
    }

    if (inputImage.isNull() || targetWidth <= 0 || targetHeight <= 0) {
        if (error) {
            *error = QStringLiteral("pose_input_invalid_image");
        }
        return result;
    }

    QImage letterboxed(targetWidth, targetHeight, QImage::Format_RGB888);
    letterboxed.fill(QColor(114, 114, 114));

    const qreal scale = qMin(
        static_cast<qreal>(targetWidth) / inputImage.width(),
        static_cast<qreal>(targetHeight) / inputImage.height());
    const int scaledWidth = qRound(inputImage.width() * scale);
    const int scaledHeight = qRound(inputImage.height() * scale);
    const int xPad = (targetWidth - scaledWidth) / 2;
    const int yPad = (targetHeight - scaledHeight) / 2;

    {
        QPainter painter(&letterboxed);
        painter.drawImage(QRect(xPad, yPad, scaledWidth, scaledHeight), inputImage);
    }

    result.packedRgb.resize(targetWidth * targetHeight * 3);
    for (int y = 0; y < targetHeight; ++y) {
        memcpy(result.packedRgb.data() + (y * targetWidth * 3),
            letterboxed.constScanLine(y), targetWidth * 3);
    }
    result.xPad = xPad;
    result.yPad = yPad;
    result.scale = static_cast<float>(scale);
    return result;
}
}
#endif

RknnPoseEstimator::~RknnPoseEstimator() {
#ifdef RKAPP_ENABLE_REAL_RKNN_POSE
    releaseRuntime(static_cast<RknnPoseRuntime *>(runtime_));
    runtime_ = nullptr;
#endif
}

bool RknnPoseEstimator::loadModel(const QString &path, QString *error) {
    if (path.isEmpty()) {
        if (error) {
            *error = QStringLiteral("pose_model_path_empty");
        }
        return false;
    }

    modelPath_ = path;

#ifdef RKAPP_ENABLE_REAL_RKNN_POSE
    releaseRuntime(static_cast<RknnPoseRuntime *>(runtime_));
    runtime_ = new RknnPoseRuntime();
    auto *runtime = static_cast<RknnPoseRuntime *>(runtime_);

    memset(&runtime->appCtx, 0, sizeof(runtime->appCtx));

    const QByteArray modelPathBytes = path.toUtf8();
    int ret = rknn_init(&runtime->appCtx.rknn_ctx, const_cast<char *>(modelPathBytes.constData()), 0, 0, nullptr);
    if (ret < 0) {
        if (error) {
            *error = QStringLiteral("rknn_init_failed_%1").arg(ret);
        }
        releaseRuntime(runtime);
        runtime_ = nullptr;
        return false;
    }

    ret = rknn_query(runtime->appCtx.rknn_ctx, RKNN_QUERY_IN_OUT_NUM,
        &runtime->appCtx.io_num, sizeof(runtime->appCtx.io_num));
    if (ret != RKNN_SUCC) {
        if (error) {
            *error = QStringLiteral("rknn_query_io_num_failed_%1").arg(ret);
        }
        releaseRuntime(runtime);
        runtime_ = nullptr;
        return false;
    }

    runtime->appCtx.input_attrs = static_cast<rknn_tensor_attr *>(
        malloc(runtime->appCtx.io_num.n_input * sizeof(rknn_tensor_attr)));
    runtime->appCtx.output_attrs = static_cast<rknn_tensor_attr *>(
        malloc(runtime->appCtx.io_num.n_output * sizeof(rknn_tensor_attr)));
    memset(runtime->appCtx.input_attrs, 0, runtime->appCtx.io_num.n_input * sizeof(rknn_tensor_attr));
    memset(runtime->appCtx.output_attrs, 0, runtime->appCtx.io_num.n_output * sizeof(rknn_tensor_attr));

    for (uint32_t i = 0; i < runtime->appCtx.io_num.n_input; ++i) {
        runtime->appCtx.input_attrs[i].index = i;
        ret = rknn_query(runtime->appCtx.rknn_ctx, RKNN_QUERY_INPUT_ATTR,
            &runtime->appCtx.input_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            if (error) {
                *error = QStringLiteral("rknn_query_input_attr_failed_%1").arg(ret);
            }
            releaseRuntime(runtime);
            runtime_ = nullptr;
            return false;
        }
    }

    for (uint32_t i = 0; i < runtime->appCtx.io_num.n_output; ++i) {
        runtime->appCtx.output_attrs[i].index = i;
        ret = rknn_query(runtime->appCtx.rknn_ctx, RKNN_QUERY_OUTPUT_ATTR,
            &runtime->appCtx.output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            if (error) {
                *error = QStringLiteral("rknn_query_output_attr_failed_%1").arg(ret);
            }
            releaseRuntime(runtime);
            runtime_ = nullptr;
            return false;
        }
    }

    const rknn_tensor_attr &inputAttr = runtime->appCtx.input_attrs[0];
    if (inputAttr.fmt == RKNN_TENSOR_NCHW) {
        runtime->appCtx.model_channel = inputAttr.dims[1];
        runtime->appCtx.model_height = inputAttr.dims[2];
        runtime->appCtx.model_width = inputAttr.dims[3];
    } else {
        runtime->appCtx.model_height = inputAttr.dims[1];
        runtime->appCtx.model_width = inputAttr.dims[2];
        runtime->appCtx.model_channel = inputAttr.dims[3];
    }

    runtime->appCtx.is_quant = runtime->appCtx.output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC
        && runtime->appCtx.output_attrs[0].type != RKNN_TENSOR_FLOAT16;

    runtime->postProcessReady = (init_post_process() == 0);
#endif

    if (error) {
        error->clear();
    }
    return true;
}

QVector<PosePerson> RknnPoseEstimator::infer(const AnalysisFramePacket &frame, QString *error) {
#ifdef RKAPP_ENABLE_REAL_RKNN_POSE
    auto *runtime = static_cast<RknnPoseRuntime *>(runtime_);
    PoseStageTimingLogger poseTimingLogger(qEnvironmentVariable(kPoseTimingPathEnvVar));
    PoseStageTimingSample poseTimingSample;
    QElapsedTimer totalTimer;
    totalTimer.start();
    if (!runtime) {
        if (error) {
            *error = QStringLiteral("pose_model_not_loaded");
        }
        return {};
    }

    const int targetWidth = runtime->appCtx.model_width;
    const int targetHeight = runtime->appCtx.model_height;
    PosePreprocessResult preprocessed;
    const char *inputBuffer = nullptr;
    int inputSize = 0;
    QElapsedTimer stageTimer;
    stageTimer.start();
    if (frame.pixelFormat == AnalysisPixelFormat::Nv12) {
        preprocessed = preprocessNv12ForPose(frame, targetWidth, targetHeight, error);
        if (error && !error->isEmpty()) {
            return {};
        }
        inputBuffer = preprocessed.packedRgb.constData();
        inputSize = preprocessed.packedRgb.size();
    } else if (frame.pixelFormat == AnalysisPixelFormat::Rgb) {
        QString fastPathError;
        if (canUseRgbPoseFastPath(frame, targetWidth, targetHeight, &fastPathError)) {
            inputBuffer = frame.payload.constData();
            inputSize = frame.payload.size();
            preprocessed.xPad = 0;
            preprocessed.yPad = 0;
            preprocessed.scale = 1.0f;
        } else if (!fastPathError.isEmpty()) {
            if (error) {
                *error = fastPathError;
            }
            return {};
        } else {
            preprocessed = preprocessRgbFrameForPose(frame, targetWidth, targetHeight, error);
            if (error && !error->isEmpty()) {
                return {};
            }
            inputBuffer = preprocessed.packedRgb.constData();
            inputSize = preprocessed.packedRgb.size();
        }
    } else {
        QImage inputImage = QImage::fromData(frame.payload, "JPEG").convertToFormat(QImage::Format_RGB888);
        if (inputImage.isNull()) {
            if (error) {
                *error = QStringLiteral("pose_input_decode_failed");
            }
            return {};
        }

        preprocessed = preprocessDecodedImageForPose(inputImage, targetWidth, targetHeight, error);
        if (error && !error->isEmpty()) {
            return {};
        }
        inputBuffer = preprocessed.packedRgb.constData();
        inputSize = preprocessed.packedRgb.size();
    }
    poseTimingSample.preprocessMs = stageTimer.elapsed();

    rknn_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    input.size = inputSize;
    input.buf = const_cast<char *>(inputBuffer);

    stageTimer.restart();
    int ret = rknn_inputs_set(runtime->appCtx.rknn_ctx, runtime->appCtx.io_num.n_input, &input);
    poseTimingSample.inputsSetMs = stageTimer.elapsed();
    if (ret < 0) {
        if (error) {
            *error = QStringLiteral("rknn_inputs_set_failed_%1").arg(ret);
        }
        return {};
    }

    stageTimer.restart();
    ret = rknn_run(runtime->appCtx.rknn_ctx, nullptr);
    poseTimingSample.rknnRunMs = stageTimer.elapsed();
    if (ret < 0) {
        if (error) {
            *error = QStringLiteral("rknn_run_failed_%1").arg(ret);
        }
        return {};
    }

    QVector<rknn_output> outputs(runtime->appCtx.io_num.n_output);
    memset(outputs.data(), 0, outputs.size() * sizeof(rknn_output));
    for (uint32_t i = 0; i < runtime->appCtx.io_num.n_output; ++i) {
        outputs[i].index = i;
        outputs[i].want_float = !runtime->appCtx.is_quant;
    }

    stageTimer.restart();
    ret = rknn_outputs_get(runtime->appCtx.rknn_ctx, runtime->appCtx.io_num.n_output, outputs.data(), nullptr);
    poseTimingSample.outputsGetMs = stageTimer.elapsed();
    if (ret < 0) {
        if (error) {
            *error = QStringLiteral("rknn_outputs_get_failed_%1").arg(ret);
        }
        return {};
    }

    letterbox_t letterBox;
    letterBox.x_pad = preprocessed.xPad;
    letterBox.y_pad = preprocessed.yPad;
    letterBox.scale = preprocessed.scale;

    object_detect_result_list results;
    memset(&results, 0, sizeof(results));
    stageTimer.restart();
    post_process(&runtime->appCtx, outputs.data(), &letterBox, BOX_THRESH, NMS_THRESH, &results);
    rknn_outputs_release(runtime->appCtx.rknn_ctx, runtime->appCtx.io_num.n_output, outputs.data());
    poseTimingSample.postProcessMs = stageTimer.elapsed();

    QVector<PosePerson> people;
    for (int i = 0; i < results.count; ++i) {
        const object_detect_result &result = results.results[i];
        PosePerson person;
        person.box = QRectF(result.box.left, result.box.top,
            result.box.right - result.box.left, result.box.bottom - result.box.top);
        person.score = result.prop;
        person.keypoints.reserve(17);
        for (int j = 0; j < 17; ++j) {
            PoseKeypoint keypoint;
            keypoint.x = result.keypoints[j][0];
            keypoint.y = result.keypoints[j][1];
            keypoint.score = result.keypoints[j][2];
            person.keypoints.push_back(keypoint);
        }
        people.push_back(person);
    }

    poseTimingSample.totalMs = totalTimer.elapsed();
    poseTimingSample.peopleCount = people.size();
    poseTimingLogger.appendSample(frame, poseTimingSample);

    if (error) {
        error->clear();
    }
    return people;
#else
    Q_UNUSED(frame);
    if (error) {
        error->clear();
    }
    return {};
#endif
}
