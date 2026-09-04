#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace sg {

constexpr int kFaceLandmarkCount = 478;

struct RawFaceLandmark {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float confidence = 1.0f;
};

struct RawFace {
    std::array<RawFaceLandmark, kFaceLandmarkCount> landmarks;
    float confidence = 0.0f;
};

class FaceBackend {
public:
    FaceBackend(const char *runtime_path,
                const char *model_path,
                float detection_confidence,
                float presence_confidence,
                float tracking_confidence);
    ~FaceBackend();

    FaceBackend(const FaceBackend &) = delete;
    FaceBackend &operator=(const FaceBackend &) = delete;

    bool ready() const;
    const std::string &error() const;
    bool detect(const uint8_t *pixels,
                int width,
                int height,
                int stride,
                int64_t timestamp_ms,
                RawFace &face,
                bool &present);

private:
    struct Api;
    Api *api_ = nullptr;
    void *library_ = nullptr;
    void *landmarker_ = nullptr;
    std::string error_;

    bool load(const char *runtime_path);
    bool create(const char *model_path,
                float detection_confidence,
                float presence_confidence,
                float tracking_confidence);
    bool set_status(int status, char *message, const char *fallback);
};

} // namespace sg
