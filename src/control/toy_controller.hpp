#pragma once

#include <array>
#include <cstdint>

namespace sg::control {

struct ToyHand {
    bool present = false;
    float pinch_strength = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
};

struct ToyFrame {
    bool paused = false;
    std::array<ToyHand, 2> hands {};
};

struct ToyState {
    bool active = false;
    bool held = false;
    bool stretching = false;
    float x = 0.5f;
    float y = 0.5f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    float radius = 28.0f;
    float string_ax = 0.0f;
    float string_ay = 0.0f;
    float string_bx = 0.0f;
    float string_by = 0.0f;
};

class ToyController {
public:
    void update(const ToyFrame &frame,
                int64_t now_ms,
                int desktop_width,
                int desktop_height);
    const ToyState &state() const;
    void reset();

private:
    ToyState state_;
    std::array<bool, 2> pinching_ {};
    int held_slot_ = -1;
    bool block_single_until_release_ = false;
    int64_t tick_ms_ = 0;
    float previous_hold_x_ = 0.0f;
    float previous_hold_y_ = 0.0f;
    float stretch_start_distance_ = 0.0f;
    float stretch_start_radius_ = 28.0f;
    float previous_stretch_x_ = 0.0f;
    float previous_stretch_y_ = 0.0f;

    void update_physics(float elapsed, int width, int height);
};

} // namespace sg::control
