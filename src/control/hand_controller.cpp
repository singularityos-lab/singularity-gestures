#include "control/hand_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include <linux/input-event-codes.h>

namespace sg::control {

namespace {

int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

float distance(float ax, float ay, float bx, float by) {
    return std::hypot(ax - bx, ay - by);
}

float pinch_ratio(const SgGestureHand &hand, int fingertip) {
    const auto &thumb = hand.landmarks[4].camera;
    const auto &finger = hand.landmarks[fingertip].camera;
    return distance(thumb.x, thumb.y, finger.x, finger.y) /
        std::max(hand.palm_span, 0.025f);
}

bool finger_extended(const SgGestureHand &hand, int tip, int pip) {
    const auto &tip_point = hand.landmarks[tip].camera;
    const auto &pip_point = hand.landmarks[pip].camera;
    const auto &wrist = hand.landmarks[0].camera;
    return distance(tip_point.x, tip_point.y, wrist.x, wrist.y) >
        distance(pip_point.x, pip_point.y, wrist.x, wrist.y) * 1.13f;
}

bool fingertips_joined(const SgGestureHand &hand, int first, int second) {
    const auto &a = hand.landmarks[first].camera;
    const auto &b = hand.landmarks[second].camera;
    return distance(a.x, a.y, b.x, b.y) /
        std::max(hand.palm_span, 0.025f) < 0.32f;
}

float hand_openness(const SgGestureHand &hand) {
    constexpr std::array<std::array<int, 2>, 4> fingers {{
        {{8, 6}}, {{12, 10}}, {{16, 14}}, {{20, 18}},
    }};
    const auto &wrist = hand.landmarks[0].camera;
    float total = 0.0f;
    for (const auto &finger : fingers) {
        const auto &tip = hand.landmarks[finger[0]].camera;
        const auto &joint = hand.landmarks[finger[1]].camera;
        const float ratio = distance(tip.x, tip.y, wrist.x, wrist.y) /
            std::max(distance(joint.x, joint.y, wrist.x, wrist.y), 0.001f);
        total += std::clamp((ratio - 0.88f) / 0.52f, 0.0f, 1.0f);
    }
    return total / static_cast<float>(fingers.size());
}

bool camera_corner(const SgGestureHand &hand) {
    if (!finger_extended(hand, 4, 3)
        || !finger_extended(hand, 8, 6)
        || finger_extended(hand, 12, 10)
        || finger_extended(hand, 16, 14)
        || finger_extended(hand, 20, 18)
        || pinch_ratio(hand, 8) < 0.55f) {
        return false;
    }
    const auto &thumb_tip = hand.landmarks[4].camera;
    const auto &thumb_base = hand.landmarks[2].camera;
    const auto &index_tip = hand.landmarks[8].camera;
    const auto &index_base = hand.landmarks[5].camera;
    const float thumb_x = thumb_tip.x - thumb_base.x;
    const float thumb_y = thumb_tip.y - thumb_base.y;
    const float index_x = index_tip.x - index_base.x;
    const float index_y = index_tip.y - index_base.y;
    const float lengths = std::hypot(thumb_x, thumb_y)
        * std::hypot(index_x, index_y);
    if (lengths < 0.001f) {
        return false;
    }
    return std::abs((thumb_x * index_x + thumb_y * index_y) / lengths)
        < 0.62f;
}

} // namespace

void HandController::EngineDeleter::operator()(SgGestureEngine *engine) const {
    sg_gesture_engine_destroy(engine);
}

HandController::HandController(std::string runtime_dir)
    : runtime_dir_(std::move(runtime_dir)) {
}

HandController::~HandController() {
    stop();
}

bool HandController::start(int screen_width,
                           int screen_height,
                           std::string &error) {
    const std::string runtime_path = runtime_dir_ + "/libmediapipe.so";
    const std::string model_path = runtime_dir_ + "/hand_landmarker.task";
    SgGestureConfig config {};
    config.runtime_path = runtime_path.c_str();
    config.model_path = model_path.c_str();
    config.max_hands = 2;
    config.cpu_threads = 4;
    config.min_detection_confidence = 0.55f;
    config.min_presence_confidence = 0.50f;
    config.min_tracking_confidence = 0.55f;
    char engine_error[512] {};
    engine_.reset(sg_gesture_engine_create(&config,
                                           engine_error,
                                           sizeof(engine_error)));
    if (!engine_) {
        error = engine_error;
        return false;
    }

    // FIXME: Read the camera device from Desktop settings.
    if (!camera_.start("/dev/video0", 1280, 720)) {
        error = camera_.error();
        engine_.reset();
        return false;
    }

    std::string pointer_error;
    const bool pointer_available = pointer_.connect(pointer_error);
    screen_width_ = std::max(screen_width, 1);
    screen_height_ = std::max(screen_height, 1);
    calibration_.screen_aspect = screen_width_ / screen_height_;
    const bool calibrated = load_calibration(calibration_.screen_aspect);
    if (!calibrated) {
        reset_calibration(calibration_.screen_aspect);
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_.calibrated = calibrated;
        snapshot_.input_available = pointer_available;
        snapshot_.status = pointer_available
            ? "Camera ready"
            : pointer_error;
    }
    running_ = true;
    inference_thread_ = std::thread(&HandController::inference_loop, this);
    return true;
}

void HandController::stop() {
    running_ = false;
    if (inference_thread_.joinable()) {
        inference_thread_.join();
    }
    cancel_desktop_gesture();
    pointer_.release_buttons(monotonic_ms());
    pointer_.disconnect();
    camera_.stop();
    engine_.reset();
}

void HandController::inference_loop() {
    uint64_t last_camera_sequence = 0;
    // FIXME: Add adaptive frame pacing without losing fast gesture tracking.
    while (running_) {
        const auto camera_frame = camera_.latest();
        if (!camera_frame || camera_frame->sequence == last_camera_sequence) {
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            continue;
        }
        last_camera_sequence = camera_frame->sequence;
        SgGestureFrame frame {};
        char error[512] {};
        bool ok = false;
        {
            std::lock_guard lock(engine_mutex_);
            ok = sg_gesture_engine_process_rgb(engine_.get(),
                                               camera_frame->pixels.data(),
                                               camera_frame->width,
                                               camera_frame->height,
                                               camera_frame->stride,
                                               camera_frame->timestamp_ms,
                                               &frame,
                                               error,
                                               sizeof(error));
        }
        std::lock_guard lock(mutex_);
        if (ok) {
            snapshot_.frame = frame;
            if (snapshot_.status != "Camera ready") {
                snapshot_.status = "Camera ready";
            }
        } else {
            snapshot_.status = error;
        }
    }
}

void HandController::tick(int64_t now_ms) {
    SgGestureFrame frame {};
    {
        std::lock_guard lock(mutex_);
        frame = snapshot_.frame;
    }
    if (frame.sequence == 0 || frame.sequence == handled_sequence_) {
        if (calibration_phase_ == CalibrationPhase::Complete &&
            last_frame_received_ms_ > 0 &&
            now_ms - last_frame_received_ms_ > 220) {
            cancel_desktop_gesture();
            pointer_.release_buttons(now_ms);
            gesture_gate_.reset();
            screenshot_gate_.reset();
            lock_gate_.reset();
            clipboard_gate_.reset();
            guide_gate_.reset();
            std::lock_guard lock(mutex_);
            snapshot_.frame.hand_count = 0;
            snapshot_.action = guide_visible_ ? HandAction::Guide
                : paused_ ? HandAction::Paused : HandAction::None;
            snapshot_.pause_progress = 0.0f;
            snapshot_.gesture_progress = 0.0f;
            snapshot_.screenshot_region_active = false;
            snapshot_.clipboard_active = false;
            snapshot_.lock_gesture_active = false;
        }
        publish_calibration_view(now_ms);
        return;
    }
    handled_sequence_ = frame.sequence;
    last_frame_received_ms_ = now_ms;
    if (calibration_phase_ != CalibrationPhase::Complete) {
        pointer_.release_buttons(now_ms);
        update_calibration(frame, now_ms);
    } else {
        update_input(frame, now_ms);
    }
    publish_calibration_view(now_ms);
}

void HandController::reset_calibration(float screen_aspect) {
    pointer_.release_buttons(monotonic_ms());
    calibration_ = {};
    calibration_.screen_aspect = std::max(screen_aspect, 0.1f);
    target_index_ = 0;
    pinch_count_ = 0;
    hold_since_ = 0;
    calibration_sample_sequence_ = 0;
    calibration_sample_x_ = 0.0f;
    calibration_sample_y_ = 0.0f;
    calibration_sample_count_ = 0;
    sampled_near_span_ = 0.0f;
    sampled_far_span_ = 1.0f;
    sampled_closed_ratio_ = 1.0f;
    sampled_open_ratio_ = 0.0f;
    previous_pinch_ = false;
    pause_hold_since_ = 0;
    last_frame_received_ms_ = 0;
    last_point_pose_ms_ = 0;
    pause_toggle_latched_ = false;
    pause_started_paused_ = false;
    paused_ = false;
    gesture_gate_.reset();
    cancel_desktop_gesture();
    screenshot_gate_.reset();
    lock_gate_.reset();
    clipboard_gate_.reset();
    guide_gate_.reset();
    guide_visible_ = false;
    calibration_phase_ = CalibrationPhase::Welcome;
    {
        std::lock_guard lock(engine_mutex_);
        if (engine_) {
            sg_gesture_engine_set_calibration(engine_.get(), &calibration_);
        }
    }
    std::lock_guard lock(mutex_);
    snapshot_.calibrated = false;
    snapshot_.action = HandAction::None;
    snapshot_.paused = false;
    snapshot_.gesture_progress = 0.0f;
    snapshot_.screenshot_region_active = false;
    snapshot_.clipboard_active = false;
    snapshot_.lock_gesture_active = false;
    snapshot_.guide_visible = false;
}

void HandController::update_calibration(const SgGestureFrame &frame,
                                        int64_t now_ms) {
    if (frame.hand_count == 0) {
        hold_since_ = 0;
        previous_pinch_ = false;
        return;
    }
    const auto &hand = frame.hands[0];
    if (calibration_phase_ == CalibrationPhase::Welcome) {
        if (!hand.open_palm) {
            hold_since_ = 0;
            return;
        }
        if (hold_since_ == 0) {
            hold_since_ = now_ms;
        } else if (now_ms - hold_since_ > 650) {
            calibration_phase_ = CalibrationPhase::Screen;
            hold_since_ = 0;
        }
        return;
    }

    if (calibration_phase_ == CalibrationPhase::Screen) {
        const auto &target = screen_targets_[target_index_];
        const auto &finger = hand.landmarks[8].camera;
        if (distance(finger.x, finger.y, target[0], target[1]) >= 0.10f ||
            !hand.pinching) {
            hold_since_ = 0;
            calibration_sample_count_ = 0;
            return;
        }
        if (hold_since_ == 0) {
            hold_since_ = now_ms;
            calibration_sample_x_ = 0.0f;
            calibration_sample_y_ = 0.0f;
            calibration_sample_count_ = 0;
            calibration_sample_sequence_ = 0;
        }
        if (frame.sequence != calibration_sample_sequence_) {
            calibration_sample_x_ += finger.x;
            calibration_sample_y_ += finger.y;
            ++calibration_sample_count_;
            calibration_sample_sequence_ = frame.sequence;
        }
        if (now_ms - hold_since_ <= 320 || calibration_sample_count_ == 0) {
            return;
        }
        calibration_.camera_points[target_index_][0] =
            calibration_sample_x_ / calibration_sample_count_;
        calibration_.camera_points[target_index_][1] =
            calibration_sample_y_ / calibration_sample_count_;
        calibration_.screen_points[target_index_][0] = target[0];
        calibration_.screen_points[target_index_][1] = target[1];
        ++target_index_;
        hold_since_ = 0;
        if (target_index_ == SG_GESTURE_SCREEN_POINT_COUNT) {
            calibration_.has_screen_mapping = true;
            {
                std::lock_guard lock(engine_mutex_);
                sg_gesture_engine_set_calibration(engine_.get(), &calibration_);
            }
            calibration_phase_ = CalibrationPhase::NearDepth;
        }
        return;
    }

    if (calibration_phase_ == CalibrationPhase::NearDepth) {
        if (!hand.open_palm) {
            hold_since_ = 0;
            return;
        }
        if (hold_since_ == 0) {
            hold_since_ = now_ms;
        }
        sampled_near_span_ = std::max(sampled_near_span_, hand.palm_span);
        if (now_ms - hold_since_ > 1500) {
            calibration_phase_ = CalibrationPhase::FarDepth;
            hold_since_ = 0;
        }
        return;
    }

    if (calibration_phase_ == CalibrationPhase::FarDepth) {
        if (!hand.open_palm) {
            hold_since_ = 0;
            return;
        }
        if (hold_since_ == 0) {
            hold_since_ = now_ms;
        }
        sampled_far_span_ = std::min(sampled_far_span_, hand.palm_span);
        if (now_ms - hold_since_ > 1500) {
            calibration_phase_ = CalibrationPhase::Pinch;
            hold_since_ = 0;
        }
        return;
    }

    const float ratio = pinch_ratio(hand, 8);
    if (hand.pinching) {
        sampled_closed_ratio_ = std::min(sampled_closed_ratio_, ratio);
    } else {
        sampled_open_ratio_ = std::max(sampled_open_ratio_, ratio);
    }
    if (hand.pinching && !previous_pinch_) {
        ++pinch_count_;
    }
    previous_pinch_ = hand.pinching;
    if (pinch_count_ >= 3 && !hand.pinching) {
        finish_calibration();
    }
}

void HandController::finish_calibration() {
    calibration_.near_palm_span = std::max(sampled_near_span_, 0.20f);
    calibration_.far_palm_span = std::clamp(
        sampled_far_span_, 0.05f, calibration_.near_palm_span - 0.06f);
    calibration_.pinch_closed_ratio = std::min(sampled_closed_ratio_, 0.32f);
    calibration_.pinch_open_ratio = std::max(
        sampled_open_ratio_, calibration_.pinch_closed_ratio + 0.18f);
    calibration_.has_depth_range = true;
    calibration_.has_pinch_range = true;
    {
        std::lock_guard lock(engine_mutex_);
        sg_gesture_engine_set_calibration(engine_.get(), &calibration_);
    }
    calibration_phase_ = CalibrationPhase::Complete;
    save_calibration();
    std::lock_guard lock(mutex_);
    snapshot_.calibrated = true;
}

void HandController::update_input(const SgGestureFrame &frame, int64_t now_ms) {
    const SgGestureHand *left_hand = nullptr;
    const SgGestureHand *right_hand = nullptr;
    for (uint32_t i = 0; i < frame.hand_count; ++i) {
        if (frame.hands[i].handedness == SG_HAND_LEFT) {
            left_hand = &frame.hands[i];
        } else if (frame.hands[i].handedness == SG_HAND_RIGHT) {
            right_hand = &frame.hands[i];
        }
    }

    GuidePose guide_pose;
    if (left_hand && right_hand) {
        guide_pose.left_present = left_hand->present;
        guide_pose.left_open_palm = left_hand->open_palm;
        guide_pose.right_present = right_hand->present;
        guide_pose.right_pointing =
            finger_extended(*right_hand, 8, 6)
            && !finger_extended(*right_hand, 12, 10)
            && !finger_extended(*right_hand, 16, 14)
            && !finger_extended(*right_hand, 20, 18)
            && !right_hand->pinching;
        guide_pose.palm_span = left_hand->palm_span;
        constexpr std::array<int, 5> palm_points {0, 5, 9, 13, 17};
        for (int landmark : palm_points) {
            guide_pose.palm_x += left_hand->landmarks[landmark].camera.x;
            guide_pose.palm_y += left_hand->landmarks[landmark].camera.y;
        }
        guide_pose.palm_x /= palm_points.size();
        guide_pose.palm_y /= palm_points.size();
        guide_pose.fingertip_x = right_hand->landmarks[8].camera.x;
        guide_pose.fingertip_y = right_hand->landmarks[8].camera.y;
    }
    const GuideIntent guide_intent = guide_gate_.update(guide_pose, now_ms);
    if (guide_intent.captured || guide_visible_) {
        screenshot_gate_.reset();
        lock_gate_.reset();
        clipboard_gate_.reset();
        cancel_desktop_gesture();
        pointer_.release_buttons(now_ms);
        gesture_gate_.reset();
        pause_hold_since_ = 0;
        pause_toggle_latched_ = false;
        if (guide_intent.toggle) {
            guide_visible_ = !guide_visible_;
        }
        std::lock_guard lock(mutex_);
        snapshot_.action = HandAction::Guide;
        snapshot_.paused = paused_;
        snapshot_.pause_progress = 0.0f;
        snapshot_.gesture_progress = guide_intent.progress;
        snapshot_.screenshot_region_active = false;
        snapshot_.clipboard_active = false;
        snapshot_.lock_gesture_active = false;
        snapshot_.guide_visible = guide_visible_;
        return;
    }

    const bool pause_pose = frame.hand_count == 2 &&
        frame.hands[0].open_palm && frame.hands[1].open_palm;
    if (pause_pose) {
        screenshot_gate_.reset();
        lock_gate_.reset();
        clipboard_gate_.reset();
        cancel_desktop_gesture();
        pointer_.release_buttons(now_ms);
        gesture_gate_.reset();
        if (pause_hold_since_ == 0) {
            pause_hold_since_ = now_ms;
            pause_started_paused_ = paused_;
        } else if (!pause_toggle_latched_ &&
                   now_ms - pause_hold_since_ >= 850) {
            paused_ = !paused_;
            pause_toggle_latched_ = true;
        }
        std::lock_guard lock(mutex_);
        snapshot_.action = HandAction::Paused;
        snapshot_.paused = paused_;
        snapshot_.pause_progress = std::clamp(
            (now_ms - pause_hold_since_) / 850.0f, 0.0f, 1.0f);
        snapshot_.pause_target_enabled = !pause_started_paused_;
        snapshot_.gesture_progress = 0.0f;
        snapshot_.screenshot_region_active = false;
        snapshot_.clipboard_active = false;
        snapshot_.lock_gesture_active = false;
        snapshot_.guide_visible = false;
        return;
    }
    pause_hold_since_ = 0;
    pause_toggle_latched_ = false;
    {
        std::lock_guard lock(mutex_);
        snapshot_.pause_progress = 0.0f;
        snapshot_.pause_target_enabled = true;
    }

    if (paused_) {
        screenshot_gate_.reset();
        lock_gate_.reset();
        clipboard_gate_.reset();
        cancel_desktop_gesture();
        pointer_.release_buttons(now_ms);
        gesture_gate_.reset();
        std::lock_guard lock(mutex_);
        snapshot_.action = HandAction::Paused;
        snapshot_.paused = true;
        snapshot_.gesture_progress = 0.0f;
        snapshot_.screenshot_region_active = false;
        snapshot_.clipboard_active = false;
        snapshot_.lock_gesture_active = false;
        snapshot_.guide_visible = false;
        return;
    }

    LockPose lock_pose;
    for (uint32_t i = 0; i < std::min(frame.hand_count, 2u); ++i) {
        const auto &hand = frame.hands[i];
        auto &lock_hand = lock_pose.hands[i];
        lock_hand.present = hand.present;
        lock_hand.fist = hand.fist;
        lock_hand.palm_span = hand.palm_span;
        constexpr std::array<int, 4> palm_points {5, 9, 13, 17};
        for (int landmark : palm_points) {
            lock_hand.camera_x += hand.landmarks[landmark].camera.x;
            lock_hand.camera_y += hand.landmarks[landmark].camera.y;
            lock_hand.screen_x += hand.landmarks[landmark].screen.x;
            lock_hand.screen_y += hand.landmarks[landmark].screen.y;
        }
        lock_hand.camera_x /= palm_points.size();
        lock_hand.camera_y /= palm_points.size();
        lock_hand.screen_x /= palm_points.size();
        lock_hand.screen_y /= palm_points.size();
    }
    const LockIntent lock_intent = lock_gate_.update(lock_pose, now_ms);
    if (lock_intent.captured) {
        screenshot_gate_.reset();
        clipboard_gate_.reset();
        cancel_desktop_gesture();
        pointer_.release_buttons(now_ms);
        gesture_gate_.reset();
        if (lock_intent.lock_screen) {
            pointer_.lock_screen();
        }
        std::lock_guard lock(mutex_);
        snapshot_.action = HandAction::Lock;
        snapshot_.paused = false;
        snapshot_.gesture_progress = lock_intent.progress;
        snapshot_.lock_gesture_active = lock_intent.active;
        snapshot_.lock_ax = lock_intent.ax;
        snapshot_.lock_ay = lock_intent.ay;
        snapshot_.lock_bx = lock_intent.bx;
        snapshot_.lock_by = lock_intent.by;
        snapshot_.screenshot_region_active = false;
        snapshot_.clipboard_active = false;
        return;
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_.lock_gesture_active = false;
    }

    ClipboardPose clipboard_pose;
    if (left_hand && right_hand) {
        clipboard_pose.left_present = left_hand->present;
        clipboard_pose.left_open_palm = left_hand->open_palm;
        clipboard_pose.right_present = right_hand->present;
        clipboard_pose.right_pinching = right_hand->pinching;
        clipboard_pose.palm_span = left_hand->palm_span;
        constexpr std::array<int, 5> palm_points {0, 5, 9, 13, 17};
        for (int landmark : palm_points) {
            clipboard_pose.anchor_camera_x +=
                left_hand->landmarks[landmark].camera.x;
            clipboard_pose.anchor_camera_y +=
                left_hand->landmarks[landmark].camera.y;
            clipboard_pose.anchor_screen_x +=
                left_hand->landmarks[landmark].screen.x;
            clipboard_pose.anchor_screen_y +=
                left_hand->landmarks[landmark].screen.y;
        }
        clipboard_pose.anchor_camera_x /= palm_points.size();
        clipboard_pose.anchor_camera_y /= palm_points.size();
        clipboard_pose.anchor_screen_x /= palm_points.size();
        clipboard_pose.anchor_screen_y /= palm_points.size();
        clipboard_pose.pinch_camera_x =
            (right_hand->landmarks[4].camera.x
                + right_hand->landmarks[8].camera.x) * 0.5f;
        clipboard_pose.pinch_camera_y =
            (right_hand->landmarks[4].camera.y
                + right_hand->landmarks[8].camera.y) * 0.5f;
        clipboard_pose.pinch_screen_x =
            (right_hand->landmarks[4].screen.x
                + right_hand->landmarks[8].screen.x) * 0.5f;
        clipboard_pose.pinch_screen_y =
            (right_hand->landmarks[4].screen.y
                + right_hand->landmarks[8].screen.y) * 0.5f;
    }
    const ClipboardIntent clipboard_intent = clipboard_gate_.update(
        clipboard_pose, now_ms);
    if (clipboard_intent.captured) {
        screenshot_gate_.reset();
        cancel_desktop_gesture();
        pointer_.release_buttons(now_ms);
        gesture_gate_.reset();
        if (clipboard_intent.paste) {
            pointer_.paste(now_ms);
        }
        std::lock_guard lock(mutex_);
        snapshot_.action = HandAction::Paste;
        snapshot_.paused = false;
        snapshot_.gesture_progress = clipboard_intent.progress;
        snapshot_.clipboard_active = clipboard_intent.active;
        snapshot_.clipboard_anchor_x = clipboard_intent.anchor_x;
        snapshot_.clipboard_anchor_y = clipboard_intent.anchor_y;
        snapshot_.clipboard_cursor_x = clipboard_intent.cursor_x;
        snapshot_.clipboard_cursor_y = clipboard_intent.cursor_y;
        snapshot_.screenshot_region_active = false;
        return;
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_.clipboard_active = false;
    }

    ScreenshotPose screenshot_pose;
    screenshot_pose.camera_frame = frame.hand_count == 2
        && camera_corner(frame.hands[0])
        && camera_corner(frame.hands[1]);
    for (uint32_t i = 0; i < std::min(frame.hand_count, 2u); ++i) {
        const auto &tracked = frame.hands[i];
        const auto &thumb = tracked.landmarks[4].screen;
        const auto &index = tracked.landmarks[8].screen;
        screenshot_pose.hands[i].present = tracked.present;
        screenshot_pose.hands[i].pinching = tracked.pinching;
        screenshot_pose.hands[i].x = (thumb.x + index.x) * 0.5f;
        screenshot_pose.hands[i].y = (thumb.y + index.y) * 0.5f;
        screenshot_pose.hands[i].pinch_strength = tracked.pinch_strength;
    }
    const ScreenshotIntent screenshot_intent = screenshot_gate_.update(
        screenshot_pose, now_ms,
        static_cast<int>(screen_width_), static_cast<int>(screen_height_));
    if (screenshot_intent.captured) {
        clipboard_gate_.reset();
        cancel_desktop_gesture();
        pointer_.release_buttons(now_ms);
        gesture_gate_.reset();
        std::lock_guard lock(mutex_);
        snapshot_.action = screenshot_intent.region_active
                || screenshot_intent.capture_region
            ? HandAction::ScreenshotRegion : HandAction::ScreenshotFull;
        snapshot_.paused = false;
        snapshot_.gesture_progress = screenshot_intent.progress;
        snapshot_.screenshot_region_active = screenshot_intent.region_active;
        snapshot_.screenshot_ax = screenshot_intent.ax;
        snapshot_.screenshot_ay = screenshot_intent.ay;
        snapshot_.screenshot_bx = screenshot_intent.bx;
        snapshot_.screenshot_by = screenshot_intent.by;
        if (screenshot_intent.capture_fullscreen
            || screenshot_intent.capture_region) {
            ++snapshot_.screenshot_serial;
            snapshot_.screenshot_fullscreen =
                screenshot_intent.capture_fullscreen;
        }
        return;
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_.screenshot_region_active = false;
    }

    if (frame.hand_count == 0) {
        if (hand_lost_since_ == 0) {
            hand_lost_since_ = now_ms;
        } else if (now_ms - hand_lost_since_ > 140) {
            cancel_desktop_gesture();
            pointer_.release_buttons(now_ms);
            gesture_gate_.reset();
            lock_gate_.reset();
            clipboard_gate_.reset();
            std::lock_guard lock(mutex_);
            snapshot_.action = HandAction::None;
            snapshot_.gesture_progress = 0.0f;
            snapshot_.screenshot_region_active = false;
            snapshot_.clipboard_active = false;
            snapshot_.lock_gesture_active = false;
        }
        return;
    }
    hand_lost_since_ = 0;
    const auto &hand = frame.hands[0];
    const auto &index_tip = hand.landmarks[8].screen;
    const auto &middle_tip = hand.landmarks[12].screen;
    const auto &wrist = hand.landmarks[0];
    GesturePose pose;
    pose.open_palm = hand.open_palm;
    pose.index = finger_extended(hand, 8, 6);
    pose.middle = finger_extended(hand, 12, 10);
    pose.ring = finger_extended(hand, 16, 14);
    pose.pinky = finger_extended(hand, 20, 18);
    pose.pinching = hand.pinching;
    pose.middle_pinching = pinch_ratio(hand, 12) < 0.25f &&
        pinch_ratio(hand, 8) > 0.48f;
    pose.pointer_x = (index_tip.x + middle_tip.x) * 0.5f;
    pose.pointer_y = (index_tip.y + middle_tip.y) * 0.5f;
    if (!pose.middle) {
        pose.pointer_x = index_tip.x;
        pose.pointer_y = index_tip.y;
    }
    pose.palm_x = wrist.screen.x;
    pose.palm_y = wrist.screen.y;
    pose.palm_velocity_x = wrist.velocity.x;
    pose.palm_velocity_y = wrist.velocity.y;
    const bool pointing_pose = pose.index && !pose.middle &&
        !pose.ring && !pose.pinky;
    if (pointing_pose) {
        last_point_pose_ms_ = now_ms;
    }

    DesktopGesturePose desktop_pose;
    desktop_pose.two_fingers_joined = pose.index && pose.middle
        && !pose.ring && !pose.pinky
        && fingertips_joined(hand, 8, 12);
    desktop_pose.three_fingers_joined = pose.index && pose.middle
        && pose.ring && !pose.pinky
        && fingertips_joined(hand, 8, 12)
        && fingertips_joined(hand, 12, 16);
    desktop_pose.openness = hand_openness(hand);
    desktop_pose.fist = !pose.pinching && !pose.middle_pinching
        && !pose.index && !pose.middle
        && !pose.ring && !pose.pinky && desktop_pose.openness < 0.28f
        && now_ms - last_point_pose_ms_ > 500;
    desktop_pose.open_palm = pose.open_palm;
    desktop_pose.x = pose.pointer_x;
    desktop_pose.y = pose.pointer_y;
    if (desktop_pose.three_fingers_joined) {
        const auto &ring_tip = hand.landmarks[16].screen;
        desktop_pose.x = (index_tip.x + middle_tip.x + ring_tip.x) / 3.0f;
        desktop_pose.y = (index_tip.y + middle_tip.y + ring_tip.y) / 3.0f;
    } else if (desktop_pose.fist || desktop_pose.open_palm) {
        desktop_pose.x = wrist.screen.x;
        desktop_pose.y = wrist.screen.y;
    }

    const DesktopGestureIntent desktop_intent = desktop_gesture_gate_.update(
        desktop_pose, now_ms, screen_width_, screen_height_);
    if (desktop_intent.captured) {
        pointer_.release_buttons(now_ms);
        gesture_gate_.reset();
        if (desktop_intent.begin) {
            pointer_.begin_desktop_gesture(
                desktop_intent.fingers, desktop_intent.direction);
        }
        if (desktop_intent.update) {
            pointer_.update_desktop_gesture(
                desktop_intent.dx, desktop_intent.dy);
        }
        if (desktop_intent.end) {
            pointer_.end_desktop_gesture(
                desktop_intent.cancelled, desktop_intent.committed);
        }

        HandAction action = HandAction::None;
        switch (desktop_intent.mode) {
        case DesktopGestureMode::Tiling: action = HandAction::Tiling; break;
        case DesktopGestureMode::Workspace: action = HandAction::Workspace; break;
        case DesktopGestureMode::Overview: action = HandAction::Overview; break;
        case DesktopGestureMode::None: break;
        }
        std::lock_guard lock(mutex_);
        snapshot_.action = action;
        snapshot_.paused = false;
        snapshot_.gesture_progress = desktop_intent.progress;
        return;
    }

    const GestureIntent intent = gesture_gate_.update(pose, now_ms);
    if (intent.left_up) {
        pointer_.button(BTN_LEFT, false, now_ms);
    }
    if (intent.move) {
        pointer_.move(intent.x, intent.y, now_ms);
    }
    if (intent.left_click) {
        pointer_.button(BTN_LEFT, true, now_ms);
        pointer_.button(BTN_LEFT, false, now_ms + 1);
    }
    if (intent.left_down) {
        pointer_.button(BTN_LEFT, true, now_ms);
    }
    if (intent.scroll_x != 0.0f || intent.scroll_y != 0.0f) {
        pointer_.scroll(intent.scroll_x, intent.scroll_y, now_ms);
    }
    if (intent.secondary_click) {
        pointer_.button(BTN_RIGHT, true, now_ms);
        pointer_.button(BTN_RIGHT, false, now_ms + 1);
    }
    if (intent.switch_next) {
        pointer_.switch_window(true, now_ms);
    } else if (intent.switch_previous) {
        pointer_.switch_window(false, now_ms);
    }

    HandAction action = HandAction::None;
    switch (intent.mode) {
    case GestureMode::Point: action = HandAction::Point; break;
    case GestureMode::Click: action = HandAction::Click; break;
    case GestureMode::Drag: action = HandAction::Drag; break;
    case GestureMode::Scroll: action = HandAction::Scroll; break;
    case GestureMode::SecondaryClick: action = HandAction::SecondaryClick; break;
    case GestureMode::SwitchNext: action = HandAction::SwitchNext; break;
    case GestureMode::SwitchPrevious: action = HandAction::SwitchPrevious; break;
    case GestureMode::Paused: action = HandAction::Rest; break;
    case GestureMode::None: break;
    }
    std::lock_guard lock(mutex_);
    snapshot_.action = action;
    snapshot_.paused = false;
    snapshot_.gesture_progress = 0.0f;
}

void HandController::cancel_desktop_gesture() {
    const DesktopGestureIntent intent = desktop_gesture_gate_.cancel();
    if (intent.end) {
        pointer_.end_desktop_gesture(true, false);
    }
}

void HandController::publish_calibration_view(int64_t now_ms) {
    CalibrationView view;
    view.phase = calibration_phase_;
    view.target_index = target_index_;
    view.pinch_count = pinch_count_;
    switch (calibration_phase_) {
    case CalibrationPhase::Welcome:
        view.title = "Show one open hand";
        view.instruction = "Face the palm toward the camera and keep every fingertip visible.";
        view.progress = hold_since_
            ? std::clamp((now_ms - hold_since_) / 650.0f, 0.0f, 1.0f)
            : 0.0f;
        break;
    case CalibrationPhase::Screen:
        view.title = "Map point " + std::to_string(target_index_ + 1) +
            " of " + std::to_string(SG_GESTURE_SCREEN_POINT_COUNT);
        view.instruction = "Move the index dot into the target, then pinch and hold.";
        view.target_x = screen_targets_[target_index_][0];
        view.target_y = screen_targets_[target_index_][1];
        view.progress = (target_index_ +
            (hold_since_
                ? std::clamp((now_ms - hold_since_) / 320.0f, 0.0f, 1.0f)
                : 0.0f)) / SG_GESTURE_SCREEN_POINT_COUNT;
        break;
    case CalibrationPhase::NearDepth:
        view.title = "Move the open hand closer";
        view.instruction = "Approach the camera at a comfortable distance.";
        view.progress = hold_since_
            ? std::clamp((now_ms - hold_since_) / 1500.0f, 0.0f, 1.0f)
            : 0.0f;
        break;
    case CalibrationPhase::FarDepth:
        view.title = "Move the open hand away";
        view.instruction = "Keep the full hand visible while moving toward the displays.";
        view.progress = hold_since_
            ? std::clamp((now_ms - hold_since_) / 1500.0f, 0.0f, 1.0f)
            : 0.0f;
        break;
    case CalibrationPhase::Pinch:
        view.title = "Pinch three times";
        view.instruction = "Touch thumb and index, then open them fully after each pinch.";
        view.progress = std::clamp(pinch_count_ / 3.0f, 0.0f, 1.0f);
        break;
    case CalibrationPhase::Complete:
        view.title = "Hand control active";
        view.instruction.clear();
        view.progress = 1.0f;
        break;
    }
    std::lock_guard lock(mutex_);
    snapshot_.calibration = std::move(view);
}

bool HandController::load_calibration(float screen_aspect) {
    const std::string current_path = calibration_path();
    bool loaded = sg_gesture_calibration_load(&calibration_,
                                               current_path.c_str());
    if (!loaded) {
        const char *config_home = std::getenv("XDG_CONFIG_HOME");
        const std::string legacy_path = config_home && *config_home
            ? std::string(config_home) + "/singularity/gesture-lab.calibration"
            : std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") +
                "/.config/singularity/gesture-lab.calibration";
        loaded = sg_gesture_calibration_load(&calibration_,
                                              legacy_path.c_str());
    }
    if (!loaded ||
        !calibration_.has_screen_mapping ||
        !calibration_.has_depth_range ||
        !calibration_.has_pinch_range) {
        return false;
    }
    calibration_.screen_aspect = std::max(screen_aspect, 0.1f);
    {
        std::lock_guard lock(engine_mutex_);
        sg_gesture_engine_set_calibration(engine_.get(), &calibration_);
    }
    calibration_phase_ = CalibrationPhase::Complete;
    save_calibration();
    return true;
}

void HandController::save_calibration() const {
    const auto path = std::filesystem::path(calibration_path());
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (!error) {
        sg_gesture_calibration_save(&calibration_, path.c_str());
    }
}

std::string HandController::calibration_path() const {
    const char *config_home = std::getenv("XDG_CONFIG_HOME");
    if (config_home && *config_home) {
        return std::string(config_home) + "/singularity/gestures/hand.calibration";
    }
    const char *user_home = std::getenv("HOME");
    return std::string(user_home ? user_home : ".") +
        "/.config/singularity/gestures/hand.calibration";
}

ControllerSnapshot HandController::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

const char *hand_action_name(HandAction action) {
    switch (action) {
    case HandAction::Point: return "Point";
    case HandAction::Click: return "Click";
    case HandAction::Drag: return "Pinch and drag";
    case HandAction::Scroll: return "Two-finger scroll";
    case HandAction::SecondaryClick: return "Secondary click";
    case HandAction::SwitchNext: return "Slap left: next window";
    case HandAction::SwitchPrevious: return "Slap right: previous window";
    case HandAction::Tiling: return "Tiling scroll";
    case HandAction::Workspace: return "Switch workspace";
    case HandAction::Overview: return "Workspace overview";
    case HandAction::ScreenshotFull: return "Capture all displays";
    case HandAction::ScreenshotRegion: return "Release both pinches to capture";
    case HandAction::Lock: return "Hold both fists together to lock";
    case HandAction::Paste: return "Pull from palm and release to paste";
    case HandAction::Guide: return "Hold: open left palm and point at it";
    case HandAction::Rest: return "Open palm: resting";
    case HandAction::Paused: return "Control paused";
    case HandAction::None:
    default: return "Show a hand";
    }
}

} // namespace sg::control
