#pragma once

#include <cstdint>

namespace sg::control {

struct GuidePose {
    bool left_present = false;
    bool left_open_palm = false;
    bool right_present = false;
    bool right_pointing = false;
    float palm_x = 0.0f;
    float palm_y = 0.0f;
    float fingertip_x = 0.0f;
    float fingertip_y = 0.0f;
    float palm_span = 0.0f;
};

struct GuideIntent {
    bool captured = false;
    bool toggle = false;
    float progress = 0.0f;
};

class GuideGestureGate {
public:
    GuideIntent update(const GuidePose &pose, int64_t now_ms);
    void reset();

private:
    int64_t hold_since_ = 0;
    bool locked_ = false;
};

} // namespace sg::control
