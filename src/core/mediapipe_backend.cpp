#include "core/mediapipe_backend.hpp"

#include <algorithm>
#include <cstring>
#include <dlfcn.h>

namespace sg {

namespace {

struct MpBaseOptions {
    const char *model_asset_buffer;
    unsigned int model_asset_buffer_count;
    const char *model_asset_path;
    int file_descriptor;
    int delegate;
    int host_environment;
    int host_system;
    const char *host_version;
    const char *ca_bundle_path;
    const char *app_id;
    const char *app_version;
};

struct MpLandmark {
    float x;
    float y;
    float z;
    bool has_visibility;
    float visibility;
    bool has_presence;
    float presence;
    const char *name;
};

struct MpLandmarks {
    MpLandmark *landmarks;
    uint32_t landmarks_count;
};

struct MpCategory {
    int index;
    float score;
    const char *category_name;
    const char *display_name;
};

struct MpCategories {
    MpCategory *categories;
    uint32_t categories_count;
};

struct MpHandLandmarkerResult {
    MpCategories *handedness;
    uint32_t handedness_count;
    MpLandmarks *hand_landmarks;
    uint32_t hand_landmarks_count;
    MpLandmarks *hand_world_landmarks;
    uint32_t hand_world_landmarks_count;
};

using ResultCallback = void (*)(int32_t,
                                MpHandLandmarkerResult *,
                                void *,
                                int64_t);

struct MpHandLandmarkerOptions {
    MpBaseOptions base_options;
    int running_mode;
    int num_hands;
    float min_hand_detection_confidence;
    float min_hand_presence_confidence;
    float min_tracking_confidence;
    ResultCallback result_callback;
};

template<typename T>
bool load_symbol(void *library, const char *name, T &target) {
    target = reinterpret_cast<T>(dlsym(library, name));
    return target != nullptr;
}

SgHandedness parse_handedness(const MpCategories *categories,
                              float &confidence) {
    confidence = 0.0f;
    if (!categories || categories->categories_count == 0) {
        return SG_HAND_UNKNOWN;
    }
    const auto &category = categories->categories[0];
    confidence = category.score;
    if (!category.category_name) {
        return SG_HAND_UNKNOWN;
    }
    if (std::strcmp(category.category_name, "Left") == 0) {
        return SG_HAND_LEFT;
    }
    if (std::strcmp(category.category_name, "Right") == 0) {
        return SG_HAND_RIGHT;
    }
    return SG_HAND_UNKNOWN;
}

void copy_landmarks(const MpLandmarks &source,
                    std::array<RawLandmark, SG_GESTURE_LANDMARK_COUNT> &target) {
    const auto count = std::min<uint32_t>(source.landmarks_count,
                                          SG_GESTURE_LANDMARK_COUNT);
    for (uint32_t i = 0; i < count; ++i) {
        const auto &point = source.landmarks[i];
        target[i] = {
            point.x,
            point.y,
            point.z,
            point.has_presence ? point.presence : 1.0f,
        };
    }
}

} // namespace

struct MediaPipeBackend::Api {
    using ErrorFree = void (*)(void *);
    using ImageCreate = int (*)(int, int, int, const uint8_t *, int, void **, char **);
    using ImageFree = void (*)(void *);
    using Create = int (*)(const MpHandLandmarkerOptions *, void **, char **);
    using Detect = int (*)(void *, void *, const void *, int64_t,
                           MpHandLandmarkerResult *, char **);
    using CloseResult = void (*)(MpHandLandmarkerResult *);
    using Close = int (*)(void *, char **);

    ErrorFree error_free = nullptr;
    ImageCreate image_create = nullptr;
    ImageFree image_free = nullptr;
    Create create = nullptr;
    Detect detect = nullptr;
    CloseResult close_result = nullptr;
    Close close = nullptr;
};

MediaPipeBackend::MediaPipeBackend(const char *runtime_path,
                                   const char *model_path,
                                   uint32_t max_hands,
                                   float detection_confidence,
                                   float presence_confidence,
                                   float tracking_confidence) {
    if (load(runtime_path)) {
        create(model_path,
               max_hands,
               detection_confidence,
               presence_confidence,
               tracking_confidence);
    }
}

MediaPipeBackend::~MediaPipeBackend() {
    if (landmarker_ && api_ && api_->close) {
        char *message = nullptr;
        api_->close(landmarker_, &message);
        if (message && api_->error_free) {
            api_->error_free(message);
        }
    }
    delete api_;
    if (library_) {
        dlclose(library_);
    }
}

bool MediaPipeBackend::ready() const {
    return library_ && landmarker_;
}

const std::string &MediaPipeBackend::error() const {
    return error_;
}

bool MediaPipeBackend::load(const char *runtime_path) {
    if (!runtime_path || !*runtime_path) {
        error_ = "MediaPipe runtime path is empty";
        return false;
    }

    library_ = dlopen(runtime_path, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!library_) {
        error_ = dlerror();
        return false;
    }

    api_ = new Api;
    const bool complete =
        load_symbol(library_, "MpErrorFree", api_->error_free) &&
        load_symbol(library_, "MpImageCreateFromUint8Data", api_->image_create) &&
        load_symbol(library_, "MpImageFree", api_->image_free) &&
        load_symbol(library_, "MpHandLandmarkerCreate", api_->create) &&
        load_symbol(library_, "MpHandLandmarkerDetectForVideo", api_->detect) &&
        load_symbol(library_, "MpHandLandmarkerCloseResult", api_->close_result) &&
        load_symbol(library_, "MpHandLandmarkerClose", api_->close);
    if (!complete) {
        error_ = "MediaPipe runtime is missing the hand landmarker C API";
        return false;
    }
    return true;
}

bool MediaPipeBackend::create(const char *model_path,
                              uint32_t max_hands,
                              float detection_confidence,
                              float presence_confidence,
                              float tracking_confidence) {
    if (!model_path || !*model_path) {
        error_ = "Hand landmark model path is empty";
        return false;
    }

    MpHandLandmarkerOptions options {};
    options.base_options.model_asset_path = model_path;
    options.base_options.file_descriptor = -1;
    options.base_options.delegate = 0;
    options.base_options.host_environment = 0;
    options.base_options.host_system = 1;
    options.base_options.host_version = "native";
    options.base_options.app_id = "dev.sinty.gesture-lab";
    options.base_options.app_version = "0.1.0";
    options.running_mode = 2;
    options.num_hands = static_cast<int>(std::clamp<uint32_t>(max_hands, 1, 2));
    options.min_hand_detection_confidence = detection_confidence;
    options.min_hand_presence_confidence = presence_confidence;
    options.min_tracking_confidence = tracking_confidence;

    char *message = nullptr;
    const int status = api_->create(&options, &landmarker_, &message);
    return set_status(status, message, "Could not create the hand landmarker");
}

bool MediaPipeBackend::detect(const uint8_t *pixels,
                              int width,
                              int height,
                              int stride,
                              int64_t timestamp_ms,
                              std::vector<RawHand> &hands) {
    hands.clear();
    if (!ready() || !pixels || width < 1 || height < 1 || stride < width * 3) {
        error_ = "Invalid RGB frame";
        return false;
    }

    void *image = nullptr;
    char *message = nullptr;
    const int packed_stride = width * 3;
    std::vector<uint8_t> packed;
    const uint8_t *image_data = pixels;
    if (stride != packed_stride) {
        packed.resize(static_cast<size_t>(packed_stride) * height);
        for (int row = 0; row < height; ++row) {
            std::memcpy(packed.data() + static_cast<size_t>(row) * packed_stride,
                        pixels + static_cast<size_t>(row) * stride,
                        packed_stride);
        }
        image_data = packed.data();
    }
    int status = api_->image_create(1,
                                    width,
                                    height,
                                    image_data,
                                    width * height * 3,
                                    &image,
                                    &message);
    if (!set_status(status, message, "Could not create a MediaPipe image")) {
        return false;
    }

    MpHandLandmarkerResult result {};
    message = nullptr;
    status = api_->detect(landmarker_,
                          image,
                          nullptr,
                          timestamp_ms,
                          &result,
                          &message);
    const bool detected = set_status(status,
                                     message,
                                     "Hand landmark detection failed");
    if (detected) {
        const uint32_t count = std::min({
            result.hand_landmarks_count,
            result.hand_world_landmarks_count,
            result.handedness_count,
            static_cast<uint32_t>(SG_GESTURE_MAX_HANDS),
        });
        hands.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            RawHand hand;
            hand.handedness = parse_handedness(&result.handedness[i],
                                                hand.confidence);
            copy_landmarks(result.hand_landmarks[i], hand.normalized);
            copy_landmarks(result.hand_world_landmarks[i], hand.world);
            hands.push_back(hand);
        }
        api_->close_result(&result);
    }
    api_->image_free(image);
    return detected;
}

bool MediaPipeBackend::set_status(int status,
                                  char *message,
                                  const char *fallback) {
    if (status == 0) {
        if (message && api_ && api_->error_free) {
            api_->error_free(message);
        }
        return true;
    }
    error_ = message ? message : fallback;
    if (message && api_ && api_->error_free) {
        api_->error_free(message);
    }
    return false;
}

} // namespace sg
