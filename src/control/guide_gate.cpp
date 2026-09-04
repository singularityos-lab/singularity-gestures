#include "control/guide_gate.hpp"

#include <algorithm>
#include <cmath>

namespace sg::control {

GuideIntent GuideGestureGate::update(const GuidePose &pose, int64_t now_ms) {
    constexpr int64_t kHoldMs = 500;
    constexpr float kMaximumDistance = 1.35f;

    GuideIntent intent;
    const bool guide_pose = pose.left_present && pose.left_open_palm &&
        pose.right_present && pose.right_pointing;
    if (!guide_pose) {
        reset();
        return intent;
    }

    const float distance = std::hypot(
        pose.fingertip_x - pose.palm_x,
        pose.fingertip_y - pose.palm_y) /
        std::max(pose.palm_span, 0.025f);
    if (distance > kMaximumDistance) {
        hold_since_ = 0;
        return intent;
    }

    intent.captured = true;
    if (locked_) {
        intent.progress = 1.0f;
        return intent;
    }
    if (hold_since_ == 0) {
        hold_since_ = now_ms;
    }
    intent.progress = std::clamp(
        (now_ms - hold_since_) / static_cast<float>(kHoldMs),
        0.0f, 1.0f);
    if (intent.progress >= 1.0f) {
        intent.toggle = true;
        locked_ = true;
    }
    return intent;
}

void GuideGestureGate::reset() {
    hold_since_ = 0;
    locked_ = false;
}

} // namespace sg::control
