#include "control/toy_controller.hpp"

#include <algorithm>
#include <cmath>

namespace sg::control {

namespace {

float pixel_distance(float ax,
                     float ay,
                     float bx,
                     float by,
                     int width,
                     int height) {
    return std::hypot((ax - bx) * width, (ay - by) * height);
}

} // namespace

void ToyController::update(const ToyFrame &frame,
                           int64_t now_ms,
                           int desktop_width,
                           int desktop_height) {
    const int width = std::max(desktop_width, 1);
    const int height = std::max(desktop_height, 1);
    const float elapsed = tick_ms_ > 0
        ? std::clamp((now_ms - tick_ms_) / 1000.0f, 0.0f, 0.05f)
        : 0.0f;
    tick_ms_ = now_ms;
    if (!frame.paused) {
        reset();
        tick_ms_ = now_ms;
        return;
    }

    const auto previous_pinching = pinching_;
    int pinch_count = 0;
    int single_slot = -1;
    for (int i = 0; i < 2; ++i) {
        pinching_[i] = frame.hands[i].present &&
            frame.hands[i].pinch_strength >=
                (pinching_[i] ? 0.46f : 0.76f);
        if (pinching_[i]) {
            ++pinch_count;
            single_slot = i;
        }
    }

    if (pinch_count == 2) {
        const auto &a = frame.hands[0];
        const auto &b = frame.hands[1];
        const float center_x = (a.x + b.x) * 0.5f;
        const float center_y = (a.y + b.y) * 0.5f;
        const float distance = pixel_distance(a.x, a.y, b.x, b.y,
                                              width, height);
        if (!state_.stretching) {
            state_.active = true;
            state_.held = false;
            state_.stretching = true;
            stretch_start_distance_ = distance;
            stretch_start_radius_ = state_.radius;
            previous_stretch_x_ = center_x;
            previous_stretch_y_ = center_y;
        } else if (elapsed > 0.0f) {
            state_.velocity_x = std::clamp(
                (center_x - previous_stretch_x_) / elapsed, -2.5f, 2.5f);
            state_.velocity_y = std::clamp(
                (center_y - previous_stretch_y_) / elapsed, -2.5f, 2.5f);
        }
        state_.x = center_x;
        state_.y = center_y;
        state_.radius = std::clamp(
            stretch_start_radius_ +
                (distance - stretch_start_distance_) * 0.22f,
            16.0f,
            180.0f);
        state_.string_ax = a.x;
        state_.string_ay = a.y;
        state_.string_bx = b.x;
        state_.string_by = b.y;
        previous_stretch_x_ = center_x;
        previous_stretch_y_ = center_y;
        held_slot_ = -1;
        return;
    }

    if (state_.stretching) {
        state_.stretching = false;
        block_single_until_release_ = pinch_count > 0;
    }
    if (pinch_count == 0) {
        if (state_.held) {
            state_.held = false;
            held_slot_ = -1;
        }
        block_single_until_release_ = false;
        update_physics(elapsed, width, height);
        return;
    }
    if (block_single_until_release_) {
        update_physics(elapsed, width, height);
        return;
    }

    const auto &hand = frame.hands[single_slot];
    const bool pinch_started = !previous_pinching[single_slot];
    if (pinch_started) {
        const bool can_catch = !state_.active || pixel_distance(
            hand.x, hand.y, state_.x, state_.y, width, height) <=
                state_.radius + 52.0f;
        if (can_catch) {
            if (!state_.active) {
                state_.radius = 28.0f;
                state_.x = hand.x;
                state_.y = hand.y;
            }
            state_.active = true;
            state_.held = true;
            state_.velocity_x = 0.0f;
            state_.velocity_y = 0.0f;
            held_slot_ = single_slot;
            previous_hold_x_ = hand.x;
            previous_hold_y_ = hand.y;
        }
    }
    if (state_.held && held_slot_ == single_slot) {
        if (elapsed > 0.0f) {
            state_.velocity_x = std::clamp(
                (hand.x - previous_hold_x_) / elapsed, -2.5f, 2.5f);
            state_.velocity_y = std::clamp(
                (hand.y - previous_hold_y_) / elapsed, -2.5f, 2.5f);
        }
        state_.x = hand.x;
        state_.y = hand.y;
        previous_hold_x_ = hand.x;
        previous_hold_y_ = hand.y;
    } else {
        update_physics(elapsed, width, height);
    }
}

const ToyState &ToyController::state() const {
    return state_;
}

void ToyController::reset() {
    state_ = {};
    pinching_ = {};
    held_slot_ = -1;
    block_single_until_release_ = false;
    tick_ms_ = 0;
    previous_hold_x_ = 0.0f;
    previous_hold_y_ = 0.0f;
    stretch_start_distance_ = 0.0f;
    stretch_start_radius_ = 28.0f;
    previous_stretch_x_ = 0.0f;
    previous_stretch_y_ = 0.0f;
}

void ToyController::update_physics(float elapsed, int width, int height) {
    if (!state_.active || elapsed <= 0.0f) {
        return;
    }
    const float radius_x = state_.radius / width;
    const float radius_y = state_.radius / height;
    state_.velocity_y += 0.72f * elapsed;
    state_.x += state_.velocity_x * elapsed;
    state_.y += state_.velocity_y * elapsed;
    if (state_.x < radius_x || state_.x > 1.0f - radius_x) {
        state_.x = std::clamp(state_.x, radius_x, 1.0f - radius_x);
        state_.velocity_x *= -0.82f;
    }
    if (state_.y < radius_y) {
        state_.y = radius_y;
        state_.velocity_y *= -0.82f;
    } else if (state_.y > 1.0f - radius_y) {
        state_.y = 1.0f - radius_y;
        state_.velocity_y *= -0.76f;
        state_.velocity_x *= 0.985f;
        if (std::abs(state_.velocity_y) < 0.025f) {
            state_.velocity_y = 0.0f;
        }
    }
}

} // namespace sg::control
