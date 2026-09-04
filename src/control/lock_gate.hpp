#pragma once

#include <array>
#include <cstdint>

namespace sg::control {

struct LockHandPose {
    bool present = false;
    bool fist = false;
    float camera_x = 0.0f;
    float camera_y = 0.0f;
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float palm_span = 0.0f;
};

struct LockPose {
    std::array<LockHandPose, 2> hands {};
};

struct LockIntent {
    bool captured = false;
    bool active = false;
    bool lock_screen = false;
    float progress = 0.0f;
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 0.0f;
    float by = 0.0f;
};

class LockGestureGate {
public:
    LockIntent update(const LockPose &pose, int64_t now_ms);
    void reset();

private:
    int64_t hold_since_ = 0;
    bool locked_ = false;
};

} // namespace sg::control
