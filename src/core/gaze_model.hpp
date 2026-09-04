#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "core/face_backend.hpp"

namespace sg {

struct GazeDirection {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float confidence = 0.0f;
};

class GazeModel {
public:
    GazeModel(const char *runtime_path,
              const char *model_path,
              uint32_t cpu_threads);
    ~GazeModel();

    GazeModel(const GazeModel &) = delete;
    GazeModel &operator=(const GazeModel &) = delete;

    bool ready() const;
    const std::string &error() const;
    bool infer(const uint8_t *pixels,
               int width,
               int height,
               int stride,
               const RawFace &face,
               GazeDirection &direction);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sg
