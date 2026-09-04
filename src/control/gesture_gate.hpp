#pragma once

#include <cstdint>

namespace sg::control {

enum class GestureMode {
    None,
    Point,
    Click,
    Drag,
    Scroll,
    SecondaryClick,
    Paused,
    SwitchNext,
    SwitchPrevious,
};

struct GesturePose {
    bool open_palm = false;
    bool index = false;
    bool middle = false;
    bool ring = false;
    bool pinky = false;
    bool pinching = false;
    bool middle_pinching = false;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
    float palm_x = 0.0f;
    float palm_y = 0.0f;
    float palm_velocity_x = 0.0f;
    float palm_velocity_y = 0.0f;
};

struct GestureIntent {
    GestureMode mode = GestureMode::None;
    bool move = false;
    bool left_click = false;
    bool left_down = false;
    bool left_up = false;
    bool secondary_click = false;
    bool switch_next = false;
    bool switch_previous = false;
    float x = 0.0f;
    float y = 0.0f;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
};

class GestureGate {
public:
    GestureIntent update(const GesturePose &pose, int64_t now_ms);
    void reset();

private:
    enum class State {
        Neutral,
        Pointing,
        Dragging,
        Scrolling,
        Palm,
    };

    enum class ScrollAxis {
        None,
        Horizontal,
        Vertical,
    };

    State state_ = State::Neutral;
    ScrollAxis scroll_axis_ = ScrollAxis::None;
    int point_frames_ = 0;
    int pinch_frames_ = 0;
    int release_frames_ = 0;
    int scroll_frames_ = 0;
    int invalid_frames_ = 0;
    int palm_frames_ = 0;
    int palm_exit_frames_ = 0;
    int tap_frames_ = 0;
    int secondary_frames_ = 0;
    bool secondary_locked_ = false;
    bool slap_armed_ = true;
    int64_t palm_started_ms_ = 0;
    int64_t pinch_started_ms_ = 0;
    int64_t tap_started_ms_ = 0;
    int64_t last_secondary_ms_ = 0;
    int64_t last_slap_ms_ = 0;
    float palm_origin_x_ = 0.0f;
    float palm_origin_y_ = 0.0f;
    float pinch_origin_x_ = 0.0f;
    float pinch_origin_y_ = 0.0f;
    float tap_origin_x_ = 0.0f;
    float tap_origin_y_ = 0.0f;
    float scroll_origin_x_ = 0.0f;
    float scroll_origin_y_ = 0.0f;
    float previous_scroll_x_ = 0.0f;
    float previous_scroll_y_ = 0.0f;
    float scroll_accumulator_ = 0.0f;

    void enter_neutral();
};

enum class DesktopGestureMode {
    None,
    Tiling,
    Workspace,
    Overview,
};

struct DesktopGesturePose {
    bool two_fingers_joined = false;
    bool three_fingers_joined = false;
    bool fist = false;
    bool open_palm = false;
    float x = 0.0f;
    float y = 0.0f;
    float openness = 0.0f;
};

struct DesktopGestureIntent {
    DesktopGestureMode mode = DesktopGestureMode::None;
    bool captured = false;
    bool begin = false;
    bool update = false;
    bool end = false;
    bool cancelled = false;
    bool committed = false;
    uint32_t fingers = 0;
    uint32_t direction = 0;
    float dx = 0.0f;
    float dy = 0.0f;
    float progress = 0.0f;
};

class DesktopGestureGate {
public:
    DesktopGestureIntent update(const DesktopGesturePose &pose,
                                int64_t now_ms,
                                float screen_width,
                                float screen_height);
    DesktopGestureIntent cancel();

private:
    enum class Candidate {
        None,
        Tiling,
        Workspace,
        OverviewOpen,
    };

    Candidate candidate_ = Candidate::None;
    DesktopGestureMode mode_ = DesktopGestureMode::None;
    int candidate_frames_ = 0;
    int invalid_frames_ = 0;
    int finish_frames_ = 0;
    int64_t started_ms_ = 0;
    int64_t previous_ms_ = 0;
    bool active_ = false;
    uint32_t direction_ = 0;
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
    float origin_openness_ = 0.0f;
    float previous_x_ = 0.0f;
    float velocity_x_ = 0.0f;
    float last_dx_ = 0.0f;
    float last_dy_ = 0.0f;
    float progress_ = 0.0f;
    float peak_progress_ = 0.0f;
    float screen_width_ = 1.0f;

    DesktopGestureIntent finish(bool committed);
    void reset();
};

} // namespace sg::control
