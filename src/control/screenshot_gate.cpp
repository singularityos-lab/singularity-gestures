#include "control/screenshot_gate.hpp"

#include <algorithm>
#include <cmath>

namespace sg::control {

ScreenshotIntent ScreenshotGestureGate::update(const ScreenshotPose &pose,
                                                int64_t now_ms,
                                                int screen_width,
                                                int screen_height) {
    constexpr int64_t kFrameHoldMs = 420;
    constexpr int64_t kRegionHoldMs = 150;
    constexpr int kReleaseFrames = 2;
    constexpr float kReleaseStrength = 0.58f;
    constexpr float kMinimumRegionPixels = 32.0f;

    ScreenshotIntent intent;
    const bool both_pinching = pose.hands[0].present
        && pose.hands[1].present
        && pose.hands[0].pinching
        && pose.hands[1].pinching;
    const bool either_holding =
        (pose.hands[0].present && pose.hands[0].pinching &&
         pose.hands[0].pinch_strength >= kReleaseStrength) ||
        (pose.hands[1].present && pose.hands[1].pinching &&
         pose.hands[1].pinch_strength >= kReleaseStrength);

    if (both_pinching && !region_locked_) {
        frame_hold_since_ = 0;
        frame_locked_ = false;
        intent.captured = true;
        ax_ = pose.hands[0].x;
        ay_ = pose.hands[0].y;
        bx_ = pose.hands[1].x;
        by_ = pose.hands[1].y;
        if (region_hold_since_ == 0) {
            region_hold_since_ = now_ms;
        }
        if (now_ms - region_hold_since_ >= kRegionHoldMs) {
            region_active_ = true;
        }
        region_release_frames_ = 0;
        intent.region_active = region_active_;
        intent.progress = std::clamp(
            (now_ms - region_hold_since_) /
                static_cast<float>(kRegionHoldMs),
            0.0f, 1.0f);
    } else if (region_active_) {
        intent.captured = true;
        intent.region_active = true;
        intent.progress = 1.0f;
        if (either_holding) {
            region_release_frames_ = 0;
        } else if (++region_release_frames_ >= kReleaseFrames) {
            const float width = std::abs(bx_ - ax_)
                * std::max(screen_width, 1);
            const float height = std::abs(by_ - ay_)
                * std::max(screen_height, 1);
            intent.capture_region = width >= kMinimumRegionPixels
                && height >= kMinimumRegionPixels;
            intent.region_active = false;
            region_active_ = false;
            region_locked_ = true;
            region_hold_since_ = 0;
            region_release_frames_ = 0;
        }
    } else if (!both_pinching) {
        region_hold_since_ = 0;
        region_release_frames_ = 0;
        if (!either_holding) {
            region_locked_ = false;
        }
    }

    intent.ax = ax_;
    intent.ay = ay_;
    intent.bx = bx_;
    intent.by = by_;
    if (intent.captured || region_locked_ || either_holding) {
        intent.captured = true;
        return intent;
    }

    if (!pose.camera_frame) {
        frame_hold_since_ = 0;
        frame_locked_ = false;
        return intent;
    }

    intent.captured = true;
    if (frame_locked_) {
        intent.progress = 1.0f;
        return intent;
    }
    if (frame_hold_since_ == 0) {
        frame_hold_since_ = now_ms;
    }
    intent.progress = std::clamp(
        (now_ms - frame_hold_since_) / static_cast<float>(kFrameHoldMs),
        0.0f, 1.0f);
    if (intent.progress >= 1.0f) {
        intent.capture_fullscreen = true;
        frame_locked_ = true;
    }
    return intent;
}

void ScreenshotGestureGate::reset() {
    frame_hold_since_ = 0;
    region_hold_since_ = 0;
    region_release_frames_ = 0;
    frame_locked_ = false;
    region_active_ = false;
    region_locked_ = false;
    ax_ = 0.0f;
    ay_ = 0.0f;
    bx_ = 0.0f;
    by_ = 0.0f;
}

} // namespace sg::control
