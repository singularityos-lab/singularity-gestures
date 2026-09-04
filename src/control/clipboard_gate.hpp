#pragma once

#include <cstdint>

namespace sg::control {

struct ClipboardPose {
    bool left_present = false;
    bool left_open_palm = false;
    bool right_present = false;
    bool right_pinching = false;
    float palm_span = 0.0f;
    float anchor_camera_x = 0.0f;
    float anchor_camera_y = 0.0f;
    float pinch_camera_x = 0.0f;
    float pinch_camera_y = 0.0f;
    float anchor_screen_x = 0.0f;
    float anchor_screen_y = 0.0f;
    float pinch_screen_x = 0.0f;
    float pinch_screen_y = 0.0f;
};

struct ClipboardIntent {
    bool captured = false;
    bool active = false;
    bool paste = false;
    float progress = 0.0f;
    float anchor_x = 0.0f;
    float anchor_y = 0.0f;
    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
};

class ClipboardGestureGate {
public:
    ClipboardIntent update(const ClipboardPose &pose, int64_t now_ms);
    void reset();

private:
    int priming_frames_ = 0;
    int release_frames_ = 0;
    int invalid_frames_ = 0;
    int64_t started_ms_ = 0;
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
    float span_ = 1.0f;
    float peak_pull_ = 0.0f;
    float anchor_x_ = 0.0f;
    float anchor_y_ = 0.0f;
    float cursor_x_ = 0.0f;
    float cursor_y_ = 0.0f;
    bool active_ = false;
};

} // namespace sg::control
