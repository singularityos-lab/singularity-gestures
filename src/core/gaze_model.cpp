#include "core/gaze_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <dlfcn.h>
#include <limits>
#include <vector>

#include <onnxruntime_c_api.h>

namespace sg {

namespace {

constexpr int kInputSize = 448;
constexpr int kOutputBins = 90;
constexpr float kPi = 3.14159265358979323846f;

float decode_angle(const float *logits, float &confidence) {
    float maximum = logits[0];
    for (int i = 1; i < kOutputBins; ++i) {
        maximum = std::max(maximum, logits[i]);
    }

    float total = 0.0f;
    float weighted = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < kOutputBins; ++i) {
        const float probability = std::exp(logits[i] - maximum);
        total += probability;
        weighted += probability * i;
        peak = std::max(peak, probability);
    }
    if (total <= std::numeric_limits<float>::epsilon()) {
        confidence = 0.0f;
        return 0.0f;
    }
    confidence = peak / total;
    const float degrees = weighted / total * 4.0f - 180.0f;
    return degrees * kPi / 180.0f;
}

} // namespace

struct GazeModel::Impl {
    using GetApiBase = const OrtApiBase *(*)();

    void *library = nullptr;
    const OrtApi *api = nullptr;
    OrtEnv *environment = nullptr;
    OrtSessionOptions *options = nullptr;
    OrtSession *session = nullptr;
    OrtMemoryInfo *memory = nullptr;
    std::vector<float> input;
    std::string error;

    ~Impl() {
        if (api) {
            if (memory) {
                api->ReleaseMemoryInfo(memory);
            }
            if (session) {
                api->ReleaseSession(session);
            }
            if (options) {
                api->ReleaseSessionOptions(options);
            }
            if (environment) {
                api->ReleaseEnv(environment);
            }
        }
        if (library) {
            dlclose(library);
        }
    }

    bool check(OrtStatus *status, const char *fallback) {
        if (!status) {
            return true;
        }
        error = api ? api->GetErrorMessage(status) : fallback;
        if (api) {
            api->ReleaseStatus(status);
        }
        return false;
    }

    bool initialize(const char *runtime_path,
                    const char *model_path,
                    uint32_t cpu_threads) {
        if (!runtime_path || !*runtime_path || !model_path || !*model_path) {
            error = "Gaze runtime or model path is empty";
            return false;
        }
        library = dlopen(runtime_path, RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            error = dlerror();
            return false;
        }
        auto get_api_base = reinterpret_cast<GetApiBase>(
            dlsym(library, "OrtGetApiBase"));
        if (!get_api_base) {
            error = "ONNX Runtime is missing OrtGetApiBase";
            return false;
        }
        const OrtApiBase *base = get_api_base();
        api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
        if (!api) {
            error = "ONNX Runtime API version is not supported";
            return false;
        }
        if (!check(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,
                                  "singularity-gaze",
                                  &environment),
                   "Could not create the ONNX Runtime environment") ||
            !check(api->CreateSessionOptions(&options),
                   "Could not create gaze session options") ||
            !check(api->SetIntraOpNumThreads(options,
                                             static_cast<int>(std::max(
                                                 cpu_threads,
                                                 1u))),
                   "Could not configure gaze model threads") ||
            !check(api->SetInterOpNumThreads(options, 1),
                   "Could not configure gaze model scheduling") ||
            !check(api->SetSessionGraphOptimizationLevel(options,
                                                         ORT_ENABLE_ALL),
                   "Could not enable gaze model optimizations") ||
            !check(api->CreateSession(environment,
                                      model_path,
                                      options,
                                      &session),
                   "Could not load the gaze model") ||
            !check(api->CreateCpuMemoryInfo(OrtArenaAllocator,
                                            OrtMemTypeDefault,
                                            &memory),
                   "Could not create gaze model memory")) {
            return false;
        }
        input.resize(3 * kInputSize * kInputSize);
        return true;
    }

    bool prepare(const uint8_t *pixels,
                 int width,
                 int height,
                 int stride,
                 const RawFace &face) {
        if (!pixels || width < 2 || height < 2 || stride < width * 3) {
            error = "Invalid RGB frame for gaze estimation";
            return false;
        }

        float left = 1.0f;
        float top = 1.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        int valid_points = 0;
        for (const auto &point : face.landmarks) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                point.confidence < 0.20f) {
                continue;
            }
            left = std::min(left, point.x);
            top = std::min(top, point.y);
            right = std::max(right, point.x);
            bottom = std::max(bottom, point.y);
            ++valid_points;
        }
        if (valid_points < 100 || right - left < 0.05f || bottom - top < 0.05f) {
            error = "Face crop is not stable enough for gaze estimation";
            return false;
        }

        const float center_x = (left + right) * 0.5f * width;
        const float center_y = (top + bottom) * 0.5f * height;
        const float crop_size = std::max((right - left) * width,
                                         (bottom - top) * height) * 1.32f;
        left = center_x - crop_size * 0.5f;
        top = center_y - crop_size * 0.5f;
        const std::array<float, 3> mean {{0.485f, 0.456f, 0.406f}};
        const std::array<float, 3> scale {{0.229f, 0.224f, 0.225f}};
        const size_t plane = static_cast<size_t>(kInputSize) * kInputSize;

        for (int y = 0; y < kInputSize; ++y) {
            const float source_y = top +
                (y + 0.5f) * crop_size / kInputSize - 0.5f;
            const int y0 = std::clamp(static_cast<int>(std::floor(source_y)),
                                      0,
                                      height - 1);
            const int y1 = std::min(y0 + 1, height - 1);
            const float fy = std::clamp(source_y - std::floor(source_y),
                                        0.0f,
                                        1.0f);
            for (int x = 0; x < kInputSize; ++x) {
                const float source_x = left +
                    (x + 0.5f) * crop_size / kInputSize - 0.5f;
                const int x0 = std::clamp(static_cast<int>(std::floor(source_x)),
                                          0,
                                          width - 1);
                const int x1 = std::min(x0 + 1, width - 1);
                const float fx = std::clamp(source_x - std::floor(source_x),
                                            0.0f,
                                            1.0f);
                const uint8_t *p00 = pixels + static_cast<size_t>(y0) * stride + x0 * 3;
                const uint8_t *p10 = pixels + static_cast<size_t>(y0) * stride + x1 * 3;
                const uint8_t *p01 = pixels + static_cast<size_t>(y1) * stride + x0 * 3;
                const uint8_t *p11 = pixels + static_cast<size_t>(y1) * stride + x1 * 3;
                for (int channel = 0; channel < 3; ++channel) {
                    const float upper = p00[channel] +
                        (p10[channel] - p00[channel]) * fx;
                    const float lower = p01[channel] +
                        (p11[channel] - p01[channel]) * fx;
                    const float value = (upper + (lower - upper) * fy) / 255.0f;
                    input[static_cast<size_t>(channel) * plane +
                          static_cast<size_t>(y) * kInputSize + x] =
                        (value - mean[channel]) / scale[channel];
                }
            }
        }
        return true;
    }

    bool run(GazeDirection &direction) {
        const std::array<int64_t, 4> shape {{1, 3, kInputSize, kInputSize}};
        OrtValue *input_value = nullptr;
        if (!check(api->CreateTensorWithDataAsOrtValue(
                       memory,
                       input.data(),
                       input.size() * sizeof(float),
                       shape.data(),
                       shape.size(),
                       ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                       &input_value),
                   "Could not create the gaze model input")) {
            return false;
        }

        const char *input_names[] = {"input"};
        const char *output_names[] = {"yaw", "pitch"};
        const OrtValue *input_values[] = {input_value};
        OrtValue *outputs[] = {nullptr, nullptr};
        const bool completed = check(api->Run(session,
                                              nullptr,
                                              input_names,
                                              input_values,
                                              1,
                                              output_names,
                                              2,
                                              outputs),
                                     "Gaze model inference failed");
        api->ReleaseValue(input_value);
        if (!completed) {
            return false;
        }

        void *yaw_data = nullptr;
        void *pitch_data = nullptr;
        const bool readable = check(api->GetTensorMutableData(outputs[0],
                                                              &yaw_data),
                                    "Could not read gaze yaw") &&
            check(api->GetTensorMutableData(outputs[1], &pitch_data),
                  "Could not read gaze pitch");
        if (readable) {
            float yaw_confidence = 0.0f;
            float pitch_confidence = 0.0f;
            direction.yaw = decode_angle(static_cast<float *>(yaw_data),
                                         yaw_confidence);
            direction.pitch = decode_angle(static_cast<float *>(pitch_data),
                                           pitch_confidence);
            direction.confidence = static_cast<float>(std::sqrt(
                static_cast<long double>(
                    yaw_confidence * pitch_confidence)));
        }
        api->ReleaseValue(outputs[0]);
        api->ReleaseValue(outputs[1]);
        return readable && std::isfinite(direction.yaw) &&
            std::isfinite(direction.pitch);
    }
};

GazeModel::GazeModel(const char *runtime_path,
                     const char *model_path,
                     uint32_t cpu_threads)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(runtime_path, model_path, cpu_threads);
}

GazeModel::~GazeModel() = default;

bool GazeModel::ready() const {
    return impl_->session && impl_->memory;
}

const std::string &GazeModel::error() const {
    return impl_->error;
}

bool GazeModel::infer(const uint8_t *pixels,
                      int width,
                      int height,
                      int stride,
                      const RawFace &face,
                      GazeDirection &direction) {
    direction = {};
    return ready() && impl_->prepare(pixels,
                                    width,
                                    height,
                                    stride,
                                    face) &&
        impl_->run(direction);
}

} // namespace sg
