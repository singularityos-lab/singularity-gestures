#include "core/face_backend.hpp"

#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <vector>

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

struct MpFaceLandmarkerResult {
    MpLandmarks *face_landmarks;
    uint32_t face_landmarks_count;
    void *face_blendshapes;
    uint32_t face_blendshapes_count;
    void *facial_transformation_matrixes;
    uint32_t facial_transformation_matrixes_count;
};

using ResultCallback = void (*)(int32_t,
                                MpFaceLandmarkerResult *,
                                void *,
                                int64_t);

struct MpFaceLandmarkerOptions {
    MpBaseOptions base_options;
    int running_mode;
    int num_faces;
    float min_face_detection_confidence;
    float min_face_presence_confidence;
    float min_tracking_confidence;
    bool output_face_blendshapes;
    bool output_facial_transformation_matrixes;
    ResultCallback result_callback;
};

template<typename T>
bool load_symbol(void *library, const char *name, T &target) {
    target = reinterpret_cast<T>(dlsym(library, name));
    return target != nullptr;
}

} // namespace

struct FaceBackend::Api {
    using ErrorFree = void (*)(void *);
    using ImageCreate = int (*)(int, int, int, const uint8_t *, int, void **, char **);
    using ImageFree = void (*)(void *);
    using Create = int (*)(const MpFaceLandmarkerOptions *, void **, char **);
    using Detect = int (*)(void *, void *, const void *, int64_t,
                           MpFaceLandmarkerResult *, char **);
    using CloseResult = void (*)(MpFaceLandmarkerResult *);
    using Close = int (*)(void *, char **);

    ErrorFree error_free = nullptr;
    ImageCreate image_create = nullptr;
    ImageFree image_free = nullptr;
    Create create = nullptr;
    Detect detect = nullptr;
    CloseResult close_result = nullptr;
    Close close = nullptr;
};

FaceBackend::FaceBackend(const char *runtime_path,
                         const char *model_path,
                         float detection_confidence,
                         float presence_confidence,
                         float tracking_confidence) {
    if (load(runtime_path)) {
        create(model_path,
               detection_confidence,
               presence_confidence,
               tracking_confidence);
    }
}

FaceBackend::~FaceBackend() {
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

bool FaceBackend::ready() const {
    return library_ && landmarker_;
}

const std::string &FaceBackend::error() const {
    return error_;
}

bool FaceBackend::load(const char *runtime_path) {
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
        load_symbol(library_, "MpFaceLandmarkerCreate", api_->create) &&
        load_symbol(library_, "MpFaceLandmarkerDetectForVideo", api_->detect) &&
        load_symbol(library_, "MpFaceLandmarkerCloseResult", api_->close_result) &&
        load_symbol(library_, "MpFaceLandmarkerClose", api_->close);
    if (!complete) {
        error_ = "MediaPipe runtime is missing the face landmarker C API";
        return false;
    }
    return true;
}

bool FaceBackend::create(const char *model_path,
                         float detection_confidence,
                         float presence_confidence,
                         float tracking_confidence) {
    if (!model_path || !*model_path) {
        error_ = "Face landmark model path is empty";
        return false;
    }

    MpFaceLandmarkerOptions options {};
    options.base_options.model_asset_path = model_path;
    options.base_options.file_descriptor = -1;
    options.base_options.delegate = 0;
    options.base_options.host_environment = 0;
    options.base_options.host_system = 1;
    options.base_options.host_version = "native";
    options.base_options.app_id = "dev.sinty.gesture-lab";
    options.base_options.app_version = "0.1.0";
    options.running_mode = 2;
    options.num_faces = 1;
    options.min_face_detection_confidence = detection_confidence;
    options.min_face_presence_confidence = presence_confidence;
    options.min_tracking_confidence = tracking_confidence;

    char *message = nullptr;
    const int status = api_->create(&options, &landmarker_, &message);
    return set_status(status, message, "Could not create the face landmarker");
}

bool FaceBackend::detect(const uint8_t *pixels,
                         int width,
                         int height,
                         int stride,
                         int64_t timestamp_ms,
                         RawFace &face,
                         bool &present) {
    face = {};
    present = false;
    if (!ready() || !pixels || width < 1 || height < 1 || stride < width * 3) {
        error_ = "Invalid RGB frame";
        return false;
    }

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

    void *image = nullptr;
    char *message = nullptr;
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

    MpFaceLandmarkerResult result {};
    message = nullptr;
    status = api_->detect(landmarker_,
                          image,
                          nullptr,
                          timestamp_ms,
                          &result,
                          &message);
    const bool detected = set_status(status,
                                     message,
                                     "Face landmark detection failed");
    if (detected && result.face_landmarks_count > 0) {
        const auto &landmarks = result.face_landmarks[0];
        if (landmarks.landmarks_count >= kFaceLandmarkCount) {
            float confidence = 0.0f;
            for (int i = 0; i < kFaceLandmarkCount; ++i) {
                const auto &point = landmarks.landmarks[i];
                const float point_confidence = point.has_presence
                    ? point.presence
                    : 1.0f;
                face.landmarks[i] = {
                    point.x,
                    point.y,
                    point.z,
                    point_confidence,
                };
                confidence += point_confidence;
            }
            face.confidence = confidence / kFaceLandmarkCount;
            present = true;
        }
    }
    if (detected) {
        api_->close_result(&result);
    }
    api_->image_free(image);
    return detected;
}

bool FaceBackend::set_status(int status,
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
