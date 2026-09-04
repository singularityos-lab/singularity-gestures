#pragma once

#include <array>
#include <cstdint>

namespace sg::control {

struct ScreenshotHandPose {
    bool present = false;
    bool pinching = false;
    float x = 0.0f;
    float y = 0.0f;
    float pinch_strength = 0.0f;
};

struct ScreenshotPose {
    bool camera_frame = false;
    std::array<ScreenshotHandPose, 2> hands {};
};

struct ScreenshotIntent {
    bool captured = false;
    bool region_active = false;
    bool capture_fullscreen = false;
    bool capture_region = false;
    float progress = 0.0f;
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 0.0f;
    float by = 0.0f;
};

class ScreenshotGestureGate {
public:
    ScreenshotIntent update(const ScreenshotPose &pose,
                            int64_t now_ms,
                            int screen_width,
                            int screen_height);
    void reset();

private:
    int64_t frame_hold_since_ = 0;
    int64_t region_hold_since_ = 0;
    int region_release_frames_ = 0;
    bool frame_locked_ = false;
    bool region_active_ = false;
    bool region_locked_ = false;
    float ax_ = 0.0f;
    float ay_ = 0.0f;
    float bx_ = 0.0f;
    float by_ = 0.0f;
};

} // namespace sg::control
