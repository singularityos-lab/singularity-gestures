#include "control/lock_gate.hpp"

#include <algorithm>
#include <cmath>

namespace sg::control {

LockIntent LockGestureGate::update(const LockPose &pose, int64_t now_ms) {
    constexpr int64_t kHoldMs = 600;
    constexpr float kMaximumDistance = 2.4f;

    LockIntent intent;
    const auto &a = pose.hands[0];
    const auto &b = pose.hands[1];
    const bool both_fists = a.present && b.present && a.fist && b.fist;
    if (!both_fists) {
        reset();
        return intent;
    }

    intent.captured = true;
    intent.active = true;
    intent.ax = a.screen_x;
    intent.ay = a.screen_y;
    intent.bx = b.screen_x;
    intent.by = b.screen_y;
    if (locked_) {
        intent.progress = 1.0f;
        return intent;
    }

    const float span = std::max(
        (a.palm_span + b.palm_span) * 0.5f, 0.025f);
    const float distance = std::hypot(a.camera_x - b.camera_x,
                                      a.camera_y - b.camera_y) / span;
    if (distance > kMaximumDistance) {
        hold_since_ = 0;
        return intent;
    }
    if (hold_since_ == 0) {
        hold_since_ = now_ms;
    }
    intent.progress = std::clamp(
        (now_ms - hold_since_) / static_cast<float>(kHoldMs),
        0.0f, 1.0f);
    if (intent.progress >= 1.0f) {
        intent.lock_screen = true;
        locked_ = true;
    }
    return intent;
}

void LockGestureGate::reset() {
    hold_since_ = 0;
    locked_ = false;
}

} // namespace sg::control
