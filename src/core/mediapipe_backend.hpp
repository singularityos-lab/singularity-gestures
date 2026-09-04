#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/gesture_math.hpp"

namespace sg {

class MediaPipeBackend {
public:
    MediaPipeBackend(const char *runtime_path,
                     const char *model_path,
                     uint32_t max_hands,
                     float detection_confidence,
                     float presence_confidence,
                     float tracking_confidence);
    ~MediaPipeBackend();

    MediaPipeBackend(const MediaPipeBackend &) = delete;
    MediaPipeBackend &operator=(const MediaPipeBackend &) = delete;

    bool ready() const;
    const std::string &error() const;
    bool detect(const uint8_t *pixels,
                int width,
                int height,
                int stride,
                int64_t timestamp_ms,
                std::vector<RawHand> &hands);

private:
    struct Api;
    Api *api_ = nullptr;
    void *library_ = nullptr;
    void *landmarker_ = nullptr;
    std::string error_;

    bool load(const char *runtime_path);
    bool create(const char *model_path,
                uint32_t max_hands,
                float detection_confidence,
                float presence_confidence,
                float tracking_confidence);
    bool set_status(int status, char *message, const char *fallback);
};

} // namespace sg
