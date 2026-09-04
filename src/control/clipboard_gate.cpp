#include "control/clipboard_gate.hpp"

#include <algorithm>
#include <cmath>

namespace sg::control {

ClipboardIntent ClipboardGestureGate::update(const ClipboardPose &pose,
                                              int64_t now_ms) {
    constexpr int kPrimeFrames = 4;
    constexpr int kReleaseFrames = 3;
    constexpr int kInvalidFrames = 4;
    constexpr int64_t kTimeoutMs = 2500;
    constexpr float kStartDistance = 1.35f;
    constexpr float kPasteDistance = 1.10f;

    ClipboardIntent intent;
    const float span = std::max(pose.palm_span, 0.025f);
    const float start_distance = std::hypot(
        pose.pinch_camera_x - pose.anchor_camera_x,
        pose.pinch_camera_y - pose.anchor_camera_y) / span;
    const bool preload = pose.left_present
        && pose.left_open_palm
        && pose.right_present
        && pose.right_pinching
        && start_distance <= kStartDistance;

    if (!active_) {
        if (!preload) {
            priming_frames_ = 0;
            return intent;
        }
        intent.captured = true;
        priming_frames_ = std::min(priming_frames_ + 1, kPrimeFrames);
        intent.progress = priming_frames_ / static_cast<float>(kPrimeFrames);
        if (priming_frames_ == kPrimeFrames) {
            active_ = true;
            started_ms_ = now_ms;
            origin_x_ = pose.pinch_camera_x;
            origin_y_ = pose.pinch_camera_y;
            span_ = span;
            anchor_x_ = pose.anchor_screen_x;
            anchor_y_ = pose.anchor_screen_y;
            cursor_x_ = pose.pinch_screen_x;
            cursor_y_ = pose.pinch_screen_y;
        }
        intent.active = active_;
        intent.anchor_x = anchor_x_;
        intent.anchor_y = anchor_y_;
        intent.cursor_x = cursor_x_;
        intent.cursor_y = cursor_y_;
        return intent;
    }

    intent.captured = true;
    intent.active = true;
    intent.progress = std::clamp(peak_pull_ / kPasteDistance, 0.0f, 1.0f);
    intent.anchor_x = anchor_x_;
    intent.anchor_y = anchor_y_;
    intent.cursor_x = cursor_x_;
    intent.cursor_y = cursor_y_;
    if (!pose.left_present || !pose.left_open_palm || !pose.right_present) {
        if (++invalid_frames_ >= kInvalidFrames) {
            reset();
            intent.active = false;
        }
        return intent;
    }
    invalid_frames_ = 0;
    anchor_x_ = pose.anchor_screen_x;
    anchor_y_ = pose.anchor_screen_y;
    cursor_x_ = pose.pinch_screen_x;
    cursor_y_ = pose.pinch_screen_y;
    const float pull = std::hypot(
        pose.pinch_camera_x - origin_x_,
        pose.pinch_camera_y - origin_y_) / span_;
    peak_pull_ = std::max(peak_pull_, pull);
    intent.progress = std::clamp(peak_pull_ / kPasteDistance, 0.0f, 1.0f);
    intent.anchor_x = anchor_x_;
    intent.anchor_y = anchor_y_;
    intent.cursor_x = cursor_x_;
    intent.cursor_y = cursor_y_;

    if (now_ms - started_ms_ > kTimeoutMs) {
        reset();
        intent.active = false;
        return intent;
    }
    if (pose.right_pinching) {
        release_frames_ = 0;
        return intent;
    }
    if (++release_frames_ < kReleaseFrames) {
        return intent;
    }
    intent.paste = peak_pull_ >= kPasteDistance;
    intent.active = false;
    reset();
    return intent;
}

void ClipboardGestureGate::reset() {
    priming_frames_ = 0;
    release_frames_ = 0;
    invalid_frames_ = 0;
    started_ms_ = 0;
    origin_x_ = 0.0f;
    origin_y_ = 0.0f;
    span_ = 1.0f;
    peak_pull_ = 0.0f;
    anchor_x_ = 0.0f;
    anchor_y_ = 0.0f;
    cursor_x_ = 0.0f;
    cursor_y_ = 0.0f;
    active_ = false;
}

} // namespace sg::control
