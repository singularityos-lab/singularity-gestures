#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/gaze_model.hpp"

int main(int argc, char **argv) {
    assert(argc >= 3);
    sg::GazeModel model(argv[1], argv[2], 2);
    assert(model.ready());

    constexpr int width = 640;
    constexpr int height = 480;
    std::vector<uint8_t> pixels(width * height * 3, 127);
    sg::RawFace face;
    for (size_t i = 0; i < face.landmarks.size(); ++i) {
        const float column = static_cast<float>(i % 24) / 23.0f;
        const float row = static_cast<float>((i / 24) % 20) / 19.0f;
        face.landmarks[i] = {
            0.34f + column * 0.32f,
            0.20f + row * 0.56f,
            0.0f,
            1.0f,
        };
    }

    sg::GazeDirection direction;
    assert(model.infer(pixels.data(),
                       width,
                       height,
                       width * 3,
                       face,
                       direction));
    assert(std::isfinite(direction.yaw));
    assert(std::isfinite(direction.pitch));
    assert(direction.confidence > 0.0f);
    return 0;
}
