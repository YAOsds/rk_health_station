#include "action/rknn_action_model_runner.h"

#ifdef RKAPP_ENABLE_REAL_RKNN_ACTION
#include "rknn_api.h"

#include <cstdlib>
#include <cstring>
#endif

#ifdef RKAPP_ENABLE_REAL_RKNN_ACTION
struct RknnActionRuntime {
    rknn_context ctx = 0;
    rknn_input_output_num ioNum;
    rknn_tensor_attr *inputAttrs = nullptr;
    rknn_tensor_attr *outputAttrs = nullptr;
};

namespace {
void releaseRuntime(RknnActionRuntime *runtime) {
    if (!runtime) {
        return;
    }

    if (runtime->inputAttrs != nullptr) {
        free(runtime->inputAttrs);
        runtime->inputAttrs = nullptr;
    }
    if (runtime->outputAttrs != nullptr) {
        free(runtime->outputAttrs);
        runtime->outputAttrs = nullptr;
    }
    if (runtime->ctx != 0) {
        rknn_destroy(runtime->ctx);
        runtime->ctx = 0;
    }
    delete runtime;
}
}
#endif

RknnActionModelRunner::~RknnActionModelRunner() {
#ifdef RKAPP_ENABLE_REAL_RKNN_ACTION
    releaseRuntime(static_cast<RknnActionRuntime *>(runtime_));
    runtime_ = nullptr;
#endif
}

bool RknnActionModelRunner::loadModel(const QString &path, QString *error) {
    if (path.isEmpty()) {
        if (error) {
            *error = QStringLiteral("action_model_path_empty");
        }
        return false;
    }

    modelPath_ = path;

#ifdef RKAPP_ENABLE_REAL_RKNN_ACTION
    releaseRuntime(static_cast<RknnActionRuntime *>(runtime_));
    runtime_ = new RknnActionRuntime();
    auto *runtime = static_cast<RknnActionRuntime *>(runtime_);
    memset(&runtime->ioNum, 0, sizeof(runtime->ioNum));

    const QByteArray modelPathBytes = path.toUtf8();
    int ret = rknn_init(&runtime->ctx, const_cast<char *>(modelPathBytes.constData()), 0, 0, nullptr);
    if (ret < 0) {
        if (error) {
            *error = QStringLiteral("rknn_init_failed_%1").arg(ret);
        }
        releaseRuntime(runtime);
        runtime_ = nullptr;
        return false;
    }

    ret = rknn_query(runtime->ctx, RKNN_QUERY_IN_OUT_NUM, &runtime->ioNum, sizeof(runtime->ioNum));
    if (ret != RKNN_SUCC) {
        if (error) {
            *error = QStringLiteral("rknn_query_io_num_failed_%1").arg(ret);
        }
        releaseRuntime(runtime);
        runtime_ = nullptr;
        return false;
    }

    runtime->inputAttrs = static_cast<rknn_tensor_attr *>(
        malloc(runtime->ioNum.n_input * sizeof(rknn_tensor_attr)));
    runtime->outputAttrs = static_cast<rknn_tensor_attr *>(
        malloc(runtime->ioNum.n_output * sizeof(rknn_tensor_attr)));
    memset(runtime->inputAttrs, 0, runtime->ioNum.n_input * sizeof(rknn_tensor_attr));
    memset(runtime->outputAttrs, 0, runtime->ioNum.n_output * sizeof(rknn_tensor_attr));

    for (uint32_t i = 0; i < runtime->ioNum.n_input; ++i) {
        runtime->inputAttrs[i].index = i;
        ret = rknn_query(runtime->ctx, RKNN_QUERY_INPUT_ATTR,
            &runtime->inputAttrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            if (error) {
                *error = QStringLiteral("rknn_query_input_attr_failed_%1").arg(ret);
            }
            releaseRuntime(runtime);
            runtime_ = nullptr;
            return false;
        }
    }

    for (uint32_t i = 0; i < runtime->ioNum.n_output; ++i) {
        runtime->outputAttrs[i].index = i;
        ret = rknn_query(runtime->ctx, RKNN_QUERY_OUTPUT_ATTR,
            &runtime->outputAttrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            if (error) {
                *error = QStringLiteral("rknn_query_output_attr_failed_%1").arg(ret);
            }
            releaseRuntime(runtime);
            runtime_ = nullptr;
            return false;
        }
    }
#endif

    if (error) {
        error->clear();
    }
    return true;
}

QVector<float> RknnActionModelRunner::infer(const QVector<float> &input, QString *error) {
    if (input.isEmpty()) {
        if (error) {
            *error = QStringLiteral("action_input_empty");
        }
        return {};
    }

#ifdef RKAPP_ENABLE_REAL_RKNN_ACTION
    auto *runtime = static_cast<RknnActionRuntime *>(runtime_);
    if (!runtime) {
        if (error) {
            *error = QStringLiteral("action_model_not_loaded");
        }
        return {};
    }

    rknn_input rknnInput;
    memset(&rknnInput, 0, sizeof(rknnInput));
    rknnInput.index = 0;
    rknnInput.type = RKNN_TENSOR_FLOAT32;
    rknnInput.fmt = runtime->inputAttrs[0].fmt;
    rknnInput.size = input.size() * static_cast<int>(sizeof(float));
    rknnInput.buf = const_cast<float *>(input.constData());

    int ret = rknn_inputs_set(runtime->ctx, runtime->ioNum.n_input, &rknnInput);
    if (ret < 0) {
        if (error) {
            *error = QStringLiteral("rknn_inputs_set_failed_%1").arg(ret);
        }
        return {};
    }

    ret = rknn_run(runtime->ctx, nullptr);
    if (ret < 0) {
        if (error) {
            *error = QStringLiteral("rknn_run_failed_%1").arg(ret);
        }
        return {};
    }

    QVector<rknn_output> outputs(runtime->ioNum.n_output);
    memset(outputs.data(), 0, outputs.size() * sizeof(rknn_output));
    for (uint32_t i = 0; i < runtime->ioNum.n_output; ++i) {
        outputs[i].index = i;
        outputs[i].want_float = 1;
    }

    ret = rknn_outputs_get(runtime->ctx, runtime->ioNum.n_output, outputs.data(), nullptr);
    if (ret < 0) {
        if (error) {
            *error = QStringLiteral("rknn_outputs_get_failed_%1").arg(ret);
        }
        return {};
    }

    QVector<float> result;
    if (!outputs.isEmpty() && outputs[0].buf != nullptr) {
        const int elementCount = runtime->outputAttrs[0].n_elems > 0
            ? static_cast<int>(runtime->outputAttrs[0].n_elems)
            : static_cast<int>(outputs[0].size / sizeof(float));
        result.resize(elementCount);
        memcpy(result.data(), outputs[0].buf, elementCount * sizeof(float));
    }
    rknn_outputs_release(runtime->ctx, runtime->ioNum.n_output, outputs.data());

    if (result.isEmpty()) {
        if (error) {
            *error = QStringLiteral("action_output_invalid");
        }
        return {};
    }
#else
    QVector<float> result = {0.8f, 0.1f, 0.1f};
#endif

    if (error) {
        error->clear();
    }
    return result;
}
