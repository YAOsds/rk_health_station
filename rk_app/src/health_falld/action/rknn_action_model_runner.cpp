#include "action/rknn_action_model_runner.h"

#ifdef RKAPP_ENABLE_REAL_RKNN_ACTION
#include "rknn_api.h"

#include <QByteArray>
#include <QDebug>

#include <cstdint>
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
using HalfBits = uint16_t;

QString tensorDimsString(const rknn_tensor_attr &attr) {
    QStringList dims;
    for (uint32_t i = 0; i < attr.n_dims; ++i) {
        dims.push_back(QString::number(attr.dims[i]));
    }
    return QStringLiteral("[%1]").arg(dims.join(','));
}

void logTensorAttr(const char *prefix, const rknn_tensor_attr &attr) {
    qInfo().noquote()
        << QStringLiteral("%1 index=%2 name=%3 n_dims=%4 dims=%5 fmt=%6 type=%7 qnt=%8 zp=%9 scale=%10 elems=%11 size=%12")
               .arg(QString::fromUtf8(prefix))
               .arg(attr.index)
               .arg(QString::fromUtf8(attr.name))
               .arg(attr.n_dims)
               .arg(tensorDimsString(attr))
               .arg(attr.fmt)
               .arg(attr.type)
               .arg(attr.qnt_type)
               .arg(attr.zp)
               .arg(QString::number(attr.scale, 'g', 6))
               .arg(attr.n_elems)
               .arg(attr.size);
}

uint32_t floatBits(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float bitsToFloat(uint32_t bits) {
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

float halfToFloat(HalfBits value) {
    const uint32_t exponent = (value & 0x7C00u) >> 10;
    const uint32_t mantissa = (value & 0x03FFu) << 13;
    const uint32_t leading = floatBits(static_cast<float>(mantissa)) >> 23;
    const uint32_t normalized = (exponent != 0u) ? ((exponent + 112u) << 23) | mantissa : 0u;
    const uint32_t denormalized =
        (exponent == 0u && mantissa != 0u)
        ? ((leading - 37u) << 23) | ((mantissa << (150u - leading)) & 0x007FE000u)
        : 0u;
    return bitsToFloat(((value & 0x8000u) << 16) | normalized | denormalized);
}

HalfBits floatToHalf(float value) {
    union {
        uint32_t bits;
        float value;
    } storage;
    storage.value = value;
    const uint32_t sign = storage.bits & 0x80000000u;
    storage.bits ^= sign;
    HalfBits result = 0;

    if (storage.bits >= 0x47800000u) {
        result = static_cast<HalfBits>(storage.bits > 0x7f800000u ? 0x7e00u : 0x7c00u);
    } else if (storage.bits < 0x38800000u) {
        storage.value += 0.5f;
        result = static_cast<HalfBits>(storage.bits - 0x3f000000u);
    } else {
        const uint32_t rounded = storage.bits + 0xc8000fffu;
        result = static_cast<HalfBits>((rounded + ((storage.bits >> 13) & 1u)) >> 13);
    }

    result = static_cast<HalfBits>(result | (sign >> 16));
    return result;
}

bool encodeInputTensor(const QVector<float> &input, const rknn_tensor_attr &inputAttr,
    QByteArray *storage, rknn_input *rknnInput, QString *error) {
    if (!storage || !rknnInput) {
        if (error) {
            *error = QStringLiteral("action_input_encode_failed");
        }
        return false;
    }

    if (inputAttr.type == RKNN_TENSOR_FLOAT32) {
        rknnInput->type = RKNN_TENSOR_FLOAT32;
        rknnInput->size = input.size() * static_cast<int>(sizeof(float));
        rknnInput->buf = const_cast<float *>(input.constData());
        return true;
    }

    if (inputAttr.type == RKNN_TENSOR_FLOAT16) {
        storage->resize(input.size() * static_cast<int>(sizeof(HalfBits)));
        auto *halves = reinterpret_cast<HalfBits *>(storage->data());
        for (int index = 0; index < input.size(); ++index) {
            halves[index] = floatToHalf(input[index]);
        }
        rknnInput->type = RKNN_TENSOR_FLOAT16;
        rknnInput->size = storage->size();
        rknnInput->buf = storage->data();
        return true;
    }

    if (error) {
        *error = QStringLiteral("action_input_type_unsupported_%1").arg(inputAttr.type);
    }
    return false;
}

QVector<float> decodeOutputTensor(const rknn_output &output, const rknn_tensor_attr &outputAttr) {
    const int elementCount = outputAttr.n_elems > 0
        ? static_cast<int>(outputAttr.n_elems)
        : static_cast<int>(output.size / sizeof(float));
    if (output.buf == nullptr || elementCount <= 0) {
        return {};
    }

    QVector<float> result(elementCount);
    if (outputAttr.type == RKNN_TENSOR_FLOAT16) {
        const auto *halves = static_cast<const HalfBits *>(output.buf);
        for (int index = 0; index < elementCount; ++index) {
            result[index] = halfToFloat(halves[index]);
        }
        return result;
    }

    memcpy(result.data(), output.buf, elementCount * static_cast<int>(sizeof(float)));
    return result;
}

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

RknnActionModelRunner::RknnActionModelRunner(bool actionDebug)
    : actionDebug_(actionDebug) {
}

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

    if (actionDebug_) {
        qInfo().noquote()
            << QStringLiteral("loaded action model path=%1 inputs=%2 outputs=%3")
                   .arg(path)
                   .arg(runtime->ioNum.n_input)
                   .arg(runtime->ioNum.n_output);
        for (uint32_t i = 0; i < runtime->ioNum.n_input; ++i) {
            logTensorAttr("action_input_attr", runtime->inputAttrs[i]);
        }
        for (uint32_t i = 0; i < runtime->ioNum.n_output; ++i) {
            logTensorAttr("action_output_attr", runtime->outputAttrs[i]);
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
    rknnInput.fmt = runtime->inputAttrs[0].fmt;
    QByteArray encodedInput;
    if (!encodeInputTensor(input, runtime->inputAttrs[0], &encodedInput, &rknnInput, error)) {
        return {};
    }

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
        outputs[i].want_float = runtime->outputAttrs[i].type == RKNN_TENSOR_FLOAT16 ? 0 : 1;
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
        result = decodeOutputTensor(outputs[0], runtime->outputAttrs[0]);
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
