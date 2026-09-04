#include "control/gesture_gate.hpp"

#include <algorithm>
#include <cmath>

namespace sg::control {

namespace {

constexpr int kPointFrames = 5;
constexpr int kClickPinchFrames = 2;
constexpr int kReleaseFrames = 3;
constexpr int kScrollFrames = 7;
constexpr int kPoseExitFrames = 4;
constexpr int kPalmFrames = 4;
constexpr int kSecondaryFrames = 5;
constexpr int kTapFrames = 2;
constexpr float kScrollAxisThreshold = 0.018f;
constexpr float kScrollStep = 0.010f;
constexpr int64_t kDragHoldMs = 220;
constexpr float kDragDistance = 0.012f;
constexpr int64_t kTapMaximumMs = 450;
constexpr float kTapPalmDrift = 0.040f;

} // namespace

GestureIntent GestureGate::update(const GesturePose &pose, int64_t now_ms) {
    GestureIntent intent;
    intent.x = pose.pointer_x;
    intent.y = pose.pointer_y;

    const bool point_pose = pose.index && !pose.middle &&
        !pose.ring && !pose.pinky;
    const bool scroll_pose = pose.index && pose.middle &&
        !pose.ring && !pose.pinky && !pose.pinching &&
        !pose.middle_pinching;
    const bool tap_pose = !pose.open_palm && !pose.pinching &&
        !pose.middle_pinching && !pose.index && !pose.middle &&
        !pose.ring && !pose.pinky;

    if (pose.open_palm) {
        intent.mode = GestureMode::Paused;
        if (state_ == State::Dragging) {
            intent.left_up = true;
        }
        if (state_ != State::Palm) {
            state_ = State::Palm;
            palm_frames_ = 1;
            palm_started_ms_ = now_ms;
            palm_origin_x_ = pose.palm_x;
            palm_origin_y_ = pose.palm_y;
        } else {
            ++palm_frames_;
        }
        palm_exit_frames_ = 0;
        point_frames_ = 0;
        pinch_frames_ = 0;
        tap_frames_ = 0;
        tap_started_ms_ = 0;
        scroll_frames_ = 0;
        secondary_frames_ = 0;

        const float dx = pose.palm_x - palm_origin_x_;
        const float dy = pose.palm_y - palm_origin_y_;
        const int64_t elapsed = now_ms - palm_started_ms_;
        const bool horizontal = std::abs(dx) > 0.10f &&
            std::abs(dx) > std::abs(dy) * 1.8f;
        const bool fast = std::abs(pose.palm_velocity_x) > 0.65f &&
            std::abs(pose.palm_velocity_x) >
                std::abs(pose.palm_velocity_y) * 1.6f;
        if (slap_armed_ && palm_frames_ >= kPalmFrames &&
            elapsed <= 650 && now_ms - last_slap_ms_ > 900 &&
            horizontal && fast) {
            slap_armed_ = false;
            last_slap_ms_ = now_ms;
            if (dx < 0.0f) {
                intent.mode = GestureMode::SwitchNext;
                intent.switch_next = true;
            } else {
                intent.mode = GestureMode::SwitchPrevious;
                intent.switch_previous = true;
            }
        } else if (elapsed > 650) {
            palm_started_ms_ = now_ms;
            palm_origin_x_ = pose.palm_x;
            palm_origin_y_ = pose.palm_y;
        }
        return intent;
    }

    if (state_ == State::Palm) {
        intent.mode = GestureMode::Paused;
        if (++palm_exit_frames_ < kPoseExitFrames) {
            return intent;
        }
        enter_neutral();
        slap_armed_ = true;
    }
    palm_frames_ = 0;

    if (state_ == State::Dragging) {
        intent.mode = GestureMode::Drag;
        intent.move = true;
        if (pose.pinching) {
            release_frames_ = 0;
        } else if (++release_frames_ >= kReleaseFrames) {
            intent.left_up = true;
            enter_neutral();
        }
        return intent;
    }

    if (state_ == State::Scrolling) {
        intent.mode = GestureMode::Scroll;
        if (!scroll_pose) {
            if (++invalid_frames_ >= kPoseExitFrames) {
                enter_neutral();
            }
            return intent;
        }
        invalid_frames_ = 0;
        const float dx = pose.pointer_x - scroll_origin_x_;
        const float dy = pose.pointer_y - scroll_origin_y_;
        if (scroll_axis_ == ScrollAxis::None &&
            std::hypot(dx, dy) >= kScrollAxisThreshold) {
            scroll_axis_ = std::abs(dx) > std::abs(dy)
                ? ScrollAxis::Horizontal
                : ScrollAxis::Vertical;
        }
        const float delta = scroll_axis_ == ScrollAxis::Horizontal
            ? pose.pointer_x - previous_scroll_x_
            : pose.pointer_y - previous_scroll_y_;
        previous_scroll_x_ = pose.pointer_x;
        previous_scroll_y_ = pose.pointer_y;
        if (scroll_axis_ != ScrollAxis::None) {
            scroll_accumulator_ += delta;
            if (std::abs(scroll_accumulator_) >= kScrollStep) {
                const float amount = std::clamp(
                    scroll_accumulator_ * 360.0f, -9.0f, 9.0f);
                if (scroll_axis_ == ScrollAxis::Horizontal) {
                    intent.scroll_x = amount;
                } else {
                    intent.scroll_y = amount;
                }
                scroll_accumulator_ = 0.0f;
            }
        }
        return intent;
    }

    if (state_ == State::Pointing && tap_started_ms_ > 0) {
        const int64_t elapsed = now_ms - tap_started_ms_;
        const float drift = std::hypot(
            pose.palm_x - tap_origin_x_, pose.palm_y - tap_origin_y_);
        if (point_pose) {
            intent.mode = GestureMode::Point;
            if (tap_frames_ >= kTapFrames && elapsed <= kTapMaximumMs &&
                drift <= kTapPalmDrift) {
                intent.mode = GestureMode::Click;
                intent.left_click = true;
            }
            tap_frames_ = 0;
            tap_started_ms_ = 0;
            invalid_frames_ = 0;
            return intent;
        }
        if (tap_pose && elapsed <= kTapMaximumMs &&
            drift <= kTapPalmDrift) {
            ++tap_frames_;
            intent.mode = GestureMode::Point;
            return intent;
        }
        tap_frames_ = 0;
        tap_started_ms_ = 0;
    }

    if (state_ == State::Pointing && tap_pose) {
        tap_started_ms_ = now_ms;
        tap_frames_ = 1;
        tap_origin_x_ = pose.palm_x;
        tap_origin_y_ = pose.palm_y;
        intent.mode = GestureMode::Point;
        return intent;
    }

    if (!pose.middle_pinching) {
        secondary_frames_ = 0;
        secondary_locked_ = false;
    }
    if (state_ == State::Pointing && pose.middle_pinching &&
        !secondary_locked_) {
        intent.mode = GestureMode::Point;
        if (++secondary_frames_ >= kSecondaryFrames &&
            now_ms - last_secondary_ms_ > 700) {
            intent.mode = GestureMode::SecondaryClick;
            intent.secondary_click = true;
            secondary_locked_ = true;
            last_secondary_ms_ = now_ms;
            enter_neutral();
        }
        return intent;
    }

    if (scroll_pose) {
        intent.mode = GestureMode::None;
        point_frames_ = 0;
        pinch_frames_ = 0;
        if (++scroll_frames_ >= kScrollFrames) {
            state_ = State::Scrolling;
            scroll_axis_ = ScrollAxis::None;
            scroll_origin_x_ = pose.pointer_x;
            scroll_origin_y_ = pose.pointer_y;
            previous_scroll_x_ = pose.pointer_x;
            previous_scroll_y_ = pose.pointer_y;
            scroll_accumulator_ = 0.0f;
            invalid_frames_ = 0;
            intent.mode = GestureMode::Scroll;
        }
        return intent;
    }
    scroll_frames_ = 0;

    if (state_ == State::Pointing && pose.pinching) {
        tap_frames_ = 0;
        tap_started_ms_ = 0;
        intent.mode = GestureMode::Point;
        intent.move = true;
        if (pinch_frames_ == 0) {
            pinch_started_ms_ = now_ms;
            pinch_origin_x_ = pose.pointer_x;
            pinch_origin_y_ = pose.pointer_y;
        }
        ++pinch_frames_;
        const float pinch_distance = std::hypot(
            pose.pointer_x - pinch_origin_x_,
            pose.pointer_y - pinch_origin_y_);
        if (pinch_frames_ >= kClickPinchFrames &&
            (now_ms - pinch_started_ms_ >= kDragHoldMs ||
             pinch_distance >= kDragDistance)) {
            state_ = State::Dragging;
            release_frames_ = 0;
            intent.mode = GestureMode::Drag;
            intent.left_down = true;
        }
        return intent;
    }
    if (state_ == State::Pointing && pinch_frames_ > 0) {
        pinch_frames_ = 0;
        pinch_started_ms_ = 0;
        intent.mode = GestureMode::Point;
        return intent;
    }
    pinch_frames_ = 0;

    if (point_pose) {
        invalid_frames_ = 0;
        point_frames_ = std::min(point_frames_ + 1, kPointFrames);
        if (state_ == State::Neutral && point_frames_ >= kPointFrames) {
            state_ = State::Pointing;
        }
        if (state_ == State::Pointing) {
            intent.mode = GestureMode::Point;
            intent.move = true;
        }
        return intent;
    }

    point_frames_ = 0;
    if (state_ == State::Pointing && ++invalid_frames_ >= kPoseExitFrames) {
        enter_neutral();
    }
    return intent;
}

void GestureGate::reset() {
    state_ = State::Neutral;
    scroll_axis_ = ScrollAxis::None;
    point_frames_ = 0;
    pinch_frames_ = 0;
    release_frames_ = 0;
    scroll_frames_ = 0;
    invalid_frames_ = 0;
    palm_frames_ = 0;
    palm_exit_frames_ = 0;
    tap_frames_ = 0;
    secondary_frames_ = 0;
    secondary_locked_ = false;
    slap_armed_ = true;
    palm_started_ms_ = 0;
    pinch_started_ms_ = 0;
    tap_started_ms_ = 0;
    scroll_accumulator_ = 0.0f;
}

void GestureGate::enter_neutral() {
    state_ = State::Neutral;
    scroll_axis_ = ScrollAxis::None;
    point_frames_ = 0;
    pinch_frames_ = 0;
    release_frames_ = 0;
    scroll_frames_ = 0;
    invalid_frames_ = 0;
    secondary_frames_ = 0;
    tap_frames_ = 0;
    pinch_started_ms_ = 0;
    tap_started_ms_ = 0;
    scroll_accumulator_ = 0.0f;
}

DesktopGestureIntent DesktopGestureGate::update(
    const DesktopGesturePose &pose,
    int64_t now_ms,
    float screen_width,
    float screen_height) {
    constexpr int kLatchFrames = 5;
    constexpr int kExitFrames = 4;
    constexpr float kAxisLockDistance = 0.018f;
    constexpr float kCommitDistance = 0.35f;
    constexpr float kFlingDistance = 0.10f;
    constexpr float kFlingVelocity = 0.75f;
    constexpr float kTilingGain = 1.65f;
    // FIXME: Use the active output width instead of estimating it from height.
    constexpr float kOutputAspect = 1.60f;

    screen_width_ = std::max(screen_width, 1.0f);
    DesktopGestureIntent intent;

    Candidate observed = Candidate::None;
    if (pose.three_fingers_joined) {
        observed = Candidate::Workspace;
    } else if (pose.two_fingers_joined) {
        observed = Candidate::Tiling;
    } else if (pose.fist) {
        observed = Candidate::OverviewOpen;
    }

    if (mode_ == DesktopGestureMode::None) {
        if (observed != candidate_) {
            candidate_ = observed;
            candidate_frames_ = observed == Candidate::None ? 0 : 1;
        } else if (observed != Candidate::None) {
            ++candidate_frames_;
        }
        intent.captured = observed == Candidate::Tiling
            || observed == Candidate::Workspace
            || observed == Candidate::OverviewOpen;
        if (candidate_frames_ < kLatchFrames) {
            return intent;
        }

        switch (candidate_) {
        case Candidate::Tiling:
            mode_ = DesktopGestureMode::Tiling;
            break;
        case Candidate::Workspace:
            mode_ = DesktopGestureMode::Workspace;
            break;
        case Candidate::OverviewOpen:
            mode_ = DesktopGestureMode::Overview;
            break;
        case Candidate::None:
            return intent;
        }
        origin_x_ = pose.x;
        origin_y_ = pose.y;
        origin_openness_ = pose.openness;
        previous_x_ = pose.x;
        previous_ms_ = now_ms;
        started_ms_ = now_ms;
        intent.captured = true;
    }

    intent.mode = mode_;
    if (mode_ == DesktopGestureMode::Tiling
        || mode_ == DesktopGestureMode::Workspace) {
        const float gesture_width = mode_ == DesktopGestureMode::Tiling
            ? std::min(screen_width_,
                       std::max(screen_height, 1.0f) * kOutputAspect)
            : screen_width_;
        const float horizontal_gain = mode_ == DesktopGestureMode::Tiling
            ? kTilingGain : 1.0f;
        intent.captured = true;
        const bool valid = mode_ == DesktopGestureMode::Tiling
            ? pose.two_fingers_joined : pose.three_fingers_joined;
        if (!valid) {
            if (++invalid_frames_ >= kExitFrames) {
                if (!active_) {
                    reset();
                    return intent;
                }
                const float directed_distance = direction_ == 1
                    ? -last_dx_ : last_dx_;
                const float directed_velocity = direction_ == 1
                    ? -velocity_x_ : velocity_x_;
                const bool committed =
                    directed_distance >= gesture_width * kCommitDistance
                    || (directed_distance >= gesture_width * kFlingDistance
                        && directed_velocity >= kFlingVelocity);
                return finish(committed);
            }
            return intent;
        }
        invalid_frames_ = 0;

        const int64_t elapsed = std::max<int64_t>(1, now_ms - previous_ms_);
        const float raw_velocity = (pose.x - previous_x_) * 1000.0f
            / static_cast<float>(elapsed);
        velocity_x_ = velocity_x_ * 0.55f + raw_velocity * 0.45f;
        previous_x_ = pose.x;
        previous_ms_ = now_ms;

        const float normalized_dx = pose.x - origin_x_;
        const float normalized_dy = pose.y - origin_y_;
        if (!active_) {
            if (std::abs(normalized_dy) > 0.07f
                && std::abs(normalized_dy) > std::abs(normalized_dx) * 1.25f) {
                reset();
                return intent;
            }
            if (std::abs(normalized_dx) < kAxisLockDistance) {
                return intent;
            }
            active_ = true;
            direction_ = normalized_dx < 0.0f ? 1u : 2u;
            intent.begin = true;
        }

        last_dx_ = normalized_dx * screen_width_ * horizontal_gain;
        last_dy_ = normalized_dy * std::max(screen_height, 1.0f);
        const float directed_distance = direction_ == 1
            ? -last_dx_ : last_dx_;
        progress_ = std::clamp(
            directed_distance / (gesture_width * kCommitDistance),
            0.0f, 1.0f);
        peak_progress_ = std::max(peak_progress_, progress_);
        intent.fingers = mode_ == DesktopGestureMode::Tiling ? 3u : 4u;
        intent.direction = direction_;
        intent.dx = last_dx_;
        intent.dy = 0.0f;
        intent.progress = progress_;
        intent.update = true;
        return intent;
    }

    intent.captured = true;
    const float span = std::max(0.85f - origin_openness_, 0.30f);
    const float raw_progress = (pose.openness - origin_openness_) / span;
    progress_ = std::clamp(raw_progress, 0.0f, 1.0f);
    peak_progress_ = std::max(peak_progress_, progress_);

    if (!active_) {
        if (progress_ < 0.06f) {
            if (now_ms - started_ms_ > 1800) {
                reset();
            }
            return intent;
        }
        active_ = true;
        direction_ = 4u;
        intent.begin = true;
        intent.captured = true;
    }

    intent.fingers = 4;
    intent.direction = direction_;
    intent.dy = progress_ * 240.0f;
    intent.progress = progress_;
    intent.update = true;

    if (progress_ >= 0.92f) {
        ++finish_frames_;
    } else if (progress_ <= 0.025f && peak_progress_ >= 0.14f) {
        --finish_frames_;
    } else {
        finish_frames_ = 0;
    }
    if (finish_frames_ >= kExitFrames) {
        return finish(true);
    }
    if (finish_frames_ <= -kExitFrames) {
        return finish(false);
    }
    if (now_ms - started_ms_ > 4000) {
        return finish(progress_ >= kCommitDistance);
    }
    return intent;
}

DesktopGestureIntent DesktopGestureGate::cancel() {
    if (!active_) {
        reset();
        return {};
    }
    return finish(false);
}

DesktopGestureIntent DesktopGestureGate::finish(bool committed) {
    DesktopGestureIntent intent;
    intent.mode = mode_;
    intent.captured = true;
    intent.end = active_;
    intent.cancelled = !committed;
    intent.committed = committed;
    intent.fingers = mode_ == DesktopGestureMode::Tiling ? 3u : 4u;
    intent.direction = direction_;
    intent.dx = last_dx_;
    intent.dy = last_dy_;
    intent.progress = progress_;
    reset();
    return intent;
}

void DesktopGestureGate::reset() {
    candidate_ = Candidate::None;
    mode_ = DesktopGestureMode::None;
    candidate_frames_ = 0;
    invalid_frames_ = 0;
    finish_frames_ = 0;
    started_ms_ = 0;
    previous_ms_ = 0;
    active_ = false;
    direction_ = 0;
    velocity_x_ = 0.0f;
    last_dx_ = 0.0f;
    last_dy_ = 0.0f;
    progress_ = 0.0f;
    peak_progress_ = 0.0f;
}

} // namespace sg::control
