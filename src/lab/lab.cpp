#include "lab/lab.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include <SDL.h>

#include "core/gesture_math.hpp"
#include "lab/renderer.hpp"

namespace sg::lab {

namespace {

constexpr float kFieldOfView = 42.0f;
constexpr float kCameraZ = 4.8f;

int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

SgVec3 pinch_point(const SgGestureHand &hand) {
    const auto &thumb = hand.landmarks[4].screen;
    const auto &index = hand.landmarks[8].screen;
    return {
        (thumb.x + index.x) * 0.5f,
        (thumb.y + index.y) * 0.5f,
        (thumb.z + index.z) * 0.5f,
    };
}

float pinch_ratio(const SgGestureHand &hand) {
    const auto &thumb = hand.landmarks[4].camera;
    const auto &index = hand.landmarks[8].camera;
    return sg::distance_2d(thumb.x, thumb.y, index.x, index.y) /
        std::max(hand.palm_span, 0.025f);
}

SgVec3 screen_to_world(const SgVec3 &point, float aspect) {
    const float z = 0.78f - std::clamp(point.z, 0.0f, 1.0f) * 1.75f;
    const float distance = kCameraZ - z;
    const float half_height = std::tan(kFieldOfView * 0.5f * static_cast<float>(M_PI) / 180.0f) * distance;
    return {
        (point.x * 2.0f - 1.0f) * half_height * aspect,
        (1.0f - point.y * 2.0f) * half_height,
        z,
    };
}

SgVec3 world_to_screen(const SgVec3 &point, float aspect) {
    const float distance = std::max(kCameraZ - point.z, 0.1f);
    const float half_height = std::tan(kFieldOfView * 0.5f * static_cast<float>(M_PI) / 180.0f) * distance;
    return {
        0.5f + point.x / (2.0f * half_height * aspect),
        0.5f - point.y / (2.0f * half_height),
        (0.78f - point.z) / 1.75f,
    };
}

float pointer_angle(const SgVec3 &a, const SgVec3 &b) {
    return std::atan2(b.y - a.y, b.x - a.x);
}

float median(std::vector<float> values) {
    if (values.empty()) {
        return 0.0f;
    }
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(),
                     values.begin() + middle,
                     values.end());
    if (values.size() % 2 != 0) {
        return values[middle];
    }
    const float upper = values[middle];
    const float lower = *std::max_element(values.begin(),
                                          values.begin() + middle);
    return (lower + upper) * 0.5f;
}

float sample_range(const std::vector<float> &values) {
    if (values.empty()) {
        return 0.0f;
    }
    const auto bounds = std::minmax_element(values.begin(), values.end());
    return *bounds.second - *bounds.first;
}

void fill_hand(SgGestureHand &hand,
               SgHandedness handedness,
               float center_x,
               float center_y,
               float depth,
               bool pinching,
               bool fist,
               bool open_palm,
               float hand_scale = 1.0f) {
    static constexpr std::array<std::array<float, 2>, 21> shape {{
        {{0.00f, 0.15f}}, {{-0.08f, 0.08f}}, {{-0.13f, 0.02f}},
        {{-0.16f, -0.04f}}, {{-0.18f, -0.10f}},
        {{-0.07f, 0.00f}}, {{-0.08f, -0.10f}}, {{-0.08f, -0.19f}}, {{-0.08f, -0.27f}},
        {{0.00f, -0.01f}}, {{0.00f, -0.13f}}, {{0.00f, -0.23f}}, {{0.00f, -0.32f}},
        {{0.07f, 0.01f}}, {{0.08f, -0.10f}}, {{0.09f, -0.19f}}, {{0.10f, -0.27f}},
        {{0.13f, 0.04f}}, {{0.15f, -0.05f}}, {{0.17f, -0.13f}}, {{0.19f, -0.20f}},
    }};
    hand = {};
    hand.present = true;
    hand.handedness = handedness;
    hand.confidence = 0.98f;
    hand.palm_span = 0.20f * hand_scale;
    hand.pinching = pinching;
    hand.pinch_strength = pinching ? 0.96f : 0.05f;
    hand.fist = fist;
    hand.open_palm = open_palm;
    const float mirror = handedness == SG_HAND_LEFT ? -1.0f : 1.0f;
    for (int i = 0; i < SG_GESTURE_LANDMARK_COUNT; ++i) {
        float x = center_x + shape[i][0] * mirror * hand_scale;
        float y = center_y + shape[i][1] * hand_scale;
        if (fist && i > 3) {
            y = center_y + 0.03f + (i % 4) * 0.014f;
            x = center_x + ((i / 4) - 2) * 0.04f;
        }
        hand.landmarks[i].camera = {x, y, depth};
        hand.landmarks[i].screen = {x, y, depth};
        hand.landmarks[i].world = {(x - center_x) * 0.5f,
                                   (center_y - y) * 0.5f,
                                   -depth * 0.1f};
        hand.landmarks[i].confidence = 1.0f;
    }
    if (pinching) {
        const float pinch_x = center_x - 0.08f * mirror * hand_scale;
        const float pinch_y = center_y - 0.23f * hand_scale;
        hand.landmarks[4].camera = {pinch_x, pinch_y, depth};
        hand.landmarks[4].screen = {pinch_x, pinch_y, depth};
        hand.landmarks[8].camera = {pinch_x + 0.008f * mirror, pinch_y, depth};
        hand.landmarks[8].screen = {pinch_x + 0.008f * mirror, pinch_y, depth};
    }
}

} // namespace

Lab::Lab(std::string executable_dir,
         bool demo,
         bool calibration_demo,
         bool presentation,
         bool gaze_calibration,
         bool eyes_only)
    : executable_dir_(std::move(executable_dir)),
      demo_(demo),
      calibration_demo_(calibration_demo),
      presentation_(presentation),
      gaze_calibration_requested_(gaze_calibration),
      eye_only_requested_(eyes_only) {
    if (gaze_calibration || eyes_only) {
        control_mode_ = ControlMode::Eyes;
    } else if (demo && !presentation) {
        control_mode_ = ControlMode::Hands;
    } else {
        control_mode_ = ControlMode::Select;
        presentation_ = true;
    }
}

Lab::~Lab() {
    stop_inference();
}

void Lab::EngineDeleter::operator()(SgGestureEngine *engine) const {
    sg_gesture_engine_destroy(engine);
}

void Lab::GazeEngineDeleter::operator()(SgGazeEngine *engine) const {
    sg_gaze_engine_destroy(engine);
}

int Lab::run() {
    if (!initialize()) {
        return 1;
    }

    last_frame_ms_ = monotonic_ms();
    float fps = 60.0f;
    while (running_) {
        const int64_t now = monotonic_ms();
        const float delta = std::clamp((now - last_frame_ms_) / 1000.0f,
                                       0.001f,
                                       0.05f);
        last_frame_ms_ = now;
        fps += ((1.0f / delta) - fps) * 0.05f;
        handle_events();
        update(delta, now);

        RenderState state;
        state.camera = presentation_ ? nullptr : camera_.latest();
        {
            std::lock_guard lock(gesture_mutex_);
            state.gestures = demo_ ? synthetic_frame(now) : gesture_frame_;
            state.gaze = demo_ ? synthetic_gaze_frame(now) : gaze_frame_;
            state.status = inference_error_;
        }
        state.cube = cube_;
        state.control_mode = control_mode_.load(std::memory_order_relaxed);
        state.calibration = calibration_view(now);
        if (state.control_mode != ControlMode::Hands) {
            state.calibration.visible = false;
        }
        state.gaze_calibration = gaze_calibration_view(now,
                                                       state.gaze.present);
        state.eye_control = eye_control_view();
        if (state.calibration.visible || state.gaze_calibration.visible) {
            state.cube.visible = false;
            state.cube.fragments.clear();
        }
        state.fps = fps;
        state.demo = demo_;
        state.presentation = presentation_;
        if (state.control_mode == ControlMode::Hands) {
            state.gaze = {};
        } else if (state.control_mode == ControlMode::Select) {
            state.cube.visible = false;
            state.cube.fragments.clear();
        }
        renderer_->render(state);
    }
    return 0;
}

bool Lab::initialize() {
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->initialize()) {
        SDL_Log("Renderer initialization failed: %s", renderer_->error().c_str());
        return false;
    }
    if (demo_) {
        calibration_phase_ = calibration_demo_
            ? CalibrationPhase::Welcome
            : CalibrationPhase::Complete;
        if (gaze_calibration_requested_ || eye_only_requested_) {
            reset_gaze_calibration();
        }
        renderer_->set_fullscreen(calibration_demo_ || presentation_);
        return true;
    }

    const std::string runtime_path = executable_dir_ + "/runtime/libmediapipe.so";
    const std::string model_path = executable_dir_ + "/runtime/hand_landmarker.task";
    const std::string face_model_path = executable_dir_ + "/runtime/face_landmarker.task";
    const std::string gaze_runtime_path = executable_dir_ + "/runtime/libonnxruntime.so";
    const std::string gaze_model_path = executable_dir_ + "/runtime/mobileone_s0_gaze.onnx";
    SgGestureConfig config {};
    config.runtime_path = runtime_path.c_str();
    config.model_path = model_path.c_str();
    config.max_hands = 2;
    config.cpu_threads = 4;
    config.min_detection_confidence = 0.55f;
    config.min_presence_confidence = 0.50f;
    config.min_tracking_confidence = 0.55f;
    char error[512] {};
    engine_.reset(sg_gesture_engine_create(&config, error, sizeof(error)));
    if (!engine_) {
        SDL_Log("Gesture engine initialization failed: %s", error);
        return false;
    }
    SgGazeConfig gaze_config {};
    gaze_config.runtime_path = runtime_path.c_str();
    gaze_config.model_path = face_model_path.c_str();
    gaze_config.gaze_runtime_path = gaze_runtime_path.c_str();
    gaze_config.gaze_model_path = gaze_model_path.c_str();
    gaze_config.cpu_threads = 4;
    gaze_config.min_detection_confidence = 0.35f;
    gaze_config.min_presence_confidence = 0.35f;
    gaze_config.min_tracking_confidence = 0.35f;
    gaze_engine_.reset(sg_gaze_engine_create(&gaze_config,
                                              error,
                                              sizeof(error)));
    if (!gaze_engine_) {
        SDL_Log("Gaze engine initialization failed: %s", error);
        return false;
    }
    // FIXME: Let the lab choose a camera before starting capture.
    if (!camera_.start("/dev/video0", 1280, 720)) {
        SDL_Log("Camera initialization failed: %s", camera_.error().c_str());
        return false;
    }
    load_calibration();
    load_gaze_calibration();
    if (gaze_calibration_requested_ ||
        (eye_only_requested_ && !gaze_calibration_.valid)) {
        reset_gaze_calibration();
    } else if (gaze_calibration_.valid) {
        gaze_calibration_phase_ = GazeCalibrationPhase::Complete;
    }
    eye_only_ = control_mode_.load(std::memory_order_relaxed) ==
        ControlMode::Eyes && gaze_calibration_.valid;
    if (presentation_) {
        renderer_->set_fullscreen(true);
    }
    start_inference();
    return true;
}

void Lab::start_inference() {
    inference_running_ = true;
    inference_thread_ = std::thread(&Lab::inference_loop, this);
}

void Lab::stop_inference() {
    inference_running_ = false;
    if (inference_thread_.joinable()) {
        inference_thread_.join();
    }
}

void Lab::inference_loop() {
    uint64_t sequence = 0;
    int64_t last_inference_ms = 0;
    int64_t last_gaze_ms = 0;
    bool gaze_lock_reported = false;
    while (inference_running_) {
        auto camera_frame = camera_.latest();
        if (!camera_frame ||
            camera_frame->sequence == sequence ||
            camera_frame->timestamp_ms - last_inference_ms < 45) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        sequence = camera_frame->sequence;
        last_inference_ms = camera_frame->timestamp_ms;
        SgGestureFrame result {};
        SgGazeFrame gaze_result {};
        char error[512] {};
        char gaze_error[512] {};
        bool ok = false;
        bool gaze_ok = false;
        const bool gaze_due = control_mode_.load(std::memory_order_relaxed) !=
            ControlMode::Hands &&
            camera_frame->timestamp_ms - last_gaze_ms >= 65;
        {
            std::lock_guard lock(engine_mutex_);
            ok = sg_gesture_engine_process_rgb(engine_.get(),
                                               camera_frame->pixels.data(),
                                               camera_frame->width,
                                               camera_frame->height,
                                               camera_frame->stride,
                                               camera_frame->timestamp_ms,
                                               &result,
                                               error,
                                               sizeof(error));
            if (gaze_due) {
                gaze_ok = sg_gaze_engine_process_rgb(gaze_engine_.get(),
                                                      camera_frame->pixels.data(),
                                                      camera_frame->width,
                                                      camera_frame->height,
                                                      camera_frame->stride,
                                                      camera_frame->timestamp_ms,
                                                      &gaze_result,
                                                      gaze_error,
                                                      sizeof(gaze_error));
            }
        }
        std::lock_guard lock(gesture_mutex_);
        if (ok) {
            gesture_frame_ = result;
            inference_error_ = !gaze_due || gaze_ok ? "" : gaze_error;
        } else {
            inference_error_ = error;
        }
        if (gaze_due && gaze_ok) {
            gaze_frame_ = gaze_result;
            last_gaze_ms = camera_frame->timestamp_ms;
            if (gaze_result.present && !gaze_lock_reported) {
                SDL_Log("Eye gaze model active");
                gaze_lock_reported = true;
            } else if (!gaze_result.present) {
                gaze_lock_reported = false;
            }
        }
    }
}

void Lab::handle_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running_ = false;
        } else if (event.type == SDL_WINDOWEVENT &&
                   event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            int width = 0;
            int height = 0;
            SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
            renderer_->resize(width, height);
        } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                   event.button.button == SDL_BUTTON_LEFT) {
            const ControlMode mode = control_mode_.load(
                std::memory_order_relaxed);
            if (mode == ControlMode::Select) {
                handle_mode_click(event.button.x, event.button.y);
            } else if (mode == ControlMode::Eyes &&
                       gaze_target_since_ == 0 &&
                       (gaze_calibration_phase_ ==
                            GazeCalibrationPhase::Screen ||
                        gaze_calibration_phase_ ==
                            GazeCalibrationPhase::Validate)) {
                gaze_sequence_start_requested_ = true;
            }
        } else if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                running_ = false;
                break;
            case SDLK_F11:
                renderer_->toggle_fullscreen();
                break;
            case SDLK_SPACE:
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (!event.key.repeat &&
                    control_mode_.load(std::memory_order_relaxed) ==
                        ControlMode::Eyes &&
                    gaze_target_since_ == 0 &&
                    (gaze_calibration_phase_ ==
                         GazeCalibrationPhase::Screen ||
                     gaze_calibration_phase_ ==
                         GazeCalibrationPhase::Validate)) {
                    gaze_sequence_start_requested_ = true;
                }
                break;
            case SDLK_c:
                reset_calibration();
                break;
            case SDLK_g:
                gaze_calibration_requested_ = true;
                eye_only_requested_ = true;
                presentation_ = true;
                control_mode_ = ControlMode::Eyes;
                reset_gaze_calibration();
                break;
            case SDLK_e:
                if (control_mode_.load(std::memory_order_relaxed) ==
                    ControlMode::Select) {
                    select_control_mode(ControlMode::Eyes);
                }
                break;
            case SDLK_h:
            case SDLK_1:
                if (control_mode_.load(std::memory_order_relaxed) ==
                    ControlMode::Select) {
                    select_control_mode(ControlMode::Hands);
                }
                break;
            case SDLK_2:
                if (control_mode_.load(std::memory_order_relaxed) ==
                    ControlMode::Select) {
                    select_control_mode(ControlMode::Eyes);
                }
                break;
            case SDLK_m:
                select_control_mode(ControlMode::Select);
                break;
            case SDLK_r:
                reset_cube();
                break;
            default:
                break;
            }
        }
    }
}

void Lab::select_control_mode(ControlMode mode) {
    control_mode_ = mode;
    presentation_ = true;
    renderer_->set_fullscreen(true);
    gaze_grabbed_ = false;
    cube_.grabbed = false;
    cube_.scaling = false;
    cube_.velocity = {};
    eye_dwell_since_ = 0;
    eye_dwell_target_ = 0;
    eye_dwell_progress_ = 0.0f;

    if (mode == ControlMode::Select) {
        gaze_sequence_running_ = false;
        eye_only_ = false;
        eye_only_requested_ = false;
        gaze_calibration_requested_ = false;
        gaze_calibration_phase_ = gaze_calibration_.valid
            ? GazeCalibrationPhase::Complete
            : GazeCalibrationPhase::Inactive;
    } else if (mode == ControlMode::Hands) {
        gaze_sequence_running_ = false;
        eye_only_ = false;
        eye_only_requested_ = false;
        gaze_calibration_requested_ = false;
        gaze_calibration_phase_ = gaze_calibration_.valid
            ? GazeCalibrationPhase::Complete
            : GazeCalibrationPhase::Inactive;
        if (calibration_phase_ == CalibrationPhase::Complete) {
            reset_cube();
        } else {
            reset_calibration();
        }
    } else {
        eye_only_requested_ = true;
        if (gaze_calibration_acceptable()) {
            gaze_calibration_phase_ = GazeCalibrationPhase::Complete;
            eye_only_ = true;
            reset_cube();
        } else {
            gaze_calibration_requested_ = true;
            reset_gaze_calibration();
        }
    }
}

void Lab::handle_mode_click(int x, int y) {
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(renderer_->window(), &width, &height);
    const int card_width = std::min(420, static_cast<int>(width * 0.32f));
    constexpr int card_height = 220;
    constexpr int gap = 24;
    const int left = (width - card_width * 2 - gap) / 2;
    const int top = height / 2 - card_height / 2;
    if (y < top || y > top + card_height) {
        return;
    }
    if (x >= left && x <= left + card_width) {
        select_control_mode(ControlMode::Hands);
    } else if (x >= left + card_width + gap &&
               x <= left + card_width * 2 + gap) {
        select_control_mode(ControlMode::Eyes);
    }
}

void Lab::update(float delta_seconds, int64_t now_ms) {
    SgGestureFrame frame {};
    SgGazeFrame gaze {};
    if (demo_) {
        frame = synthetic_frame(now_ms);
        gaze = synthetic_gaze_frame(now_ms);
    } else {
        std::lock_guard lock(gesture_mutex_);
        frame = gesture_frame_;
        gaze = gaze_frame_;
    }
    const ControlMode mode = control_mode_.load(std::memory_order_relaxed);
    if (mode == ControlMode::Select) {
        cube_.rotation.y += delta_seconds * 12.0f;
        return;
    }
    if (gaze_calibration_phase_ == GazeCalibrationPhase::Acquire ||
        gaze_calibration_phase_ == GazeCalibrationPhase::Screen ||
        gaze_calibration_phase_ == GazeCalibrationPhase::Validate) {
        update_gaze_calibration(gaze, now_ms);
        cube_.rotation.y += delta_seconds * 12.0f;
        return;
    }
    if (mode == ControlMode::Hands &&
        calibration_phase_ != CalibrationPhase::Complete) {
        update_calibration(frame, now_ms);
        cube_.rotation.y += delta_seconds * 12.0f;
        return;
    }
    if (mode == ControlMode::Eyes) {
        update_eye_control(gaze, delta_seconds, now_ms);
    } else {
        update_cube(frame, delta_seconds);
    }
    update_physics(delta_seconds);
}

void Lab::update_calibration(const SgGestureFrame &frame, int64_t now_ms) {
    if (frame.hand_count == 0) {
        hold_since_ = 0;
        previous_pinch_ = false;
        return;
    }
    const auto &hand = frame.hands[0];

    if (calibration_phase_ == CalibrationPhase::Welcome) {
        if (hand.open_palm) {
            if (hold_since_ == 0) {
                hold_since_ = now_ms;
            } else if (now_ms - hold_since_ > 650) {
                calibration_phase_ = CalibrationPhase::Screen;
                calibration_phase_since_ = now_ms;
                hold_since_ = 0;
            }
        } else {
            hold_since_ = 0;
        }
        return;
    }

    if (calibration_phase_ == CalibrationPhase::Screen) {
        const auto &target = screen_targets_[target_index_];
        const auto &finger = hand.landmarks[8].camera;
        const float distance = sg::distance_2d(finger.x,
                                               finger.y,
                                               target[0],
                                               target[1]);
        if (distance < 0.10f && hand.pinching) {
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
            if (now_ms - hold_since_ > 320 && calibration_sample_count_ > 0) {
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
                    calibration_phase_since_ = now_ms;
                }
            }
        } else {
            hold_since_ = 0;
            calibration_sample_count_ = 0;
        }
        return;
    }

    if (calibration_phase_ == CalibrationPhase::NearDepth) {
        if (hand.open_palm) {
            if (hold_since_ == 0) {
                hold_since_ = now_ms;
            }
            sampled_near_span_ = std::max(sampled_near_span_, hand.palm_span);
            if (now_ms - hold_since_ > 1800) {
                calibration_phase_ = CalibrationPhase::FarDepth;
                calibration_phase_since_ = now_ms;
                hold_since_ = 0;
            }
        } else {
            hold_since_ = 0;
        }
        return;
    }

    if (calibration_phase_ == CalibrationPhase::FarDepth) {
        if (hand.open_palm) {
            if (hold_since_ == 0) {
                hold_since_ = now_ms;
            }
            sampled_far_span_ = std::min(sampled_far_span_, hand.palm_span);
            if (now_ms - hold_since_ > 1800) {
                calibration_phase_ = CalibrationPhase::Pinch;
                calibration_phase_since_ = now_ms;
                hold_since_ = 0;
            }
        } else {
            hold_since_ = 0;
        }
        return;
    }

    if (calibration_phase_ == CalibrationPhase::Pinch) {
        const float ratio = pinch_ratio(hand);
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
}

void Lab::reset_gaze_sample(int64_t now_ms) {
    gaze_target_since_ = now_ms;
    gaze_capture_since_ = 0;
    gaze_sample_sequence_ = 0;
    for (auto &samples : gaze_probe_samples_) {
        samples.clear();
    }
    for (auto &samples : gaze_feature_samples_) {
        samples.clear();
    }
    gaze_left_open_samples_.clear();
    gaze_right_open_samples_.clear();
    gaze_validation_error_samples_.clear();
    gaze_sample_count_ = 0;
    gaze_capture_drift_frames_ = 0;
}

bool Lab::gaze_target_fixated(const SgGazeFrame &frame) {
    constexpr size_t probe_count = 5;
    for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
        auto &samples = gaze_probe_samples_[feature];
        samples.push_back(frame.features[feature]);
        if (samples.size() > probe_count) {
            samples.erase(samples.begin());
        }
    }
    if (gaze_probe_samples_[0].size() < probe_count ||
        sample_range(gaze_probe_samples_[0]) > 0.10f ||
        sample_range(gaze_probe_samples_[1]) > 0.10f) {
        return false;
    }

    std::array<float, SG_GAZE_FEATURE_COUNT> candidate {};
    for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
        candidate[feature] = median(gaze_probe_samples_[feature]);
    }

    const bool validating =
        gaze_calibration_phase_ == GazeCalibrationPhase::Validate;
    if (!validating && gaze_calibration_.point_count > 0) {
        const uint32_t previous = gaze_calibration_.point_count - 1;
        const auto &previous_target = gaze_targets_[gaze_target_index_ - 1];
        const auto &target = gaze_targets_[gaze_target_index_];
        const float screen_dx = target[0] - previous_target[0];
        const float screen_dy = target[1] - previous_target[1];
        const bool horizontal = std::abs(screen_dx) >= std::abs(screen_dy);
        const int feature = horizontal ? 0 : 1;
        const float screen_delta = horizontal ? screen_dx : screen_dy;
        const float signal_delta = candidate[feature] -
            gaze_calibration_.features[previous][feature];
        const float threshold = std::max(0.018f,
                                         std::abs(screen_delta) * 0.10f);
        float &response_sign = horizontal
            ? gaze_horizontal_response_sign_
            : gaze_vertical_response_sign_;
        const float relation = signal_delta * std::copysign(1.0f,
                                                            screen_delta);
        if (response_sign == 0.0f) {
            if (std::abs(signal_delta) < threshold) {
                return false;
            }
            response_sign = std::copysign(1.0f, relation);
        } else if (relation * response_sign < threshold) {
            return false;
        }
    }

    if (frame.calibrated) {
        const auto &target = validating
            ? gaze_validation_targets_[gaze_target_index_]
            : gaze_targets_[gaze_target_index_];
        int width = 1;
        int height = 1;
        SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
        const float distance = std::hypot((frame.screen.x - target[0]) * width,
                                          (frame.screen.y - target[1]) * height);
        const float radius = std::max(300.0f,
                                      std::min(width, height) * 0.25f);
        if (distance > radius) {
            return false;
        }
    }

    return true;
}

bool Lab::gaze_capture_drifted(const SgGazeFrame &frame) const {
    if (!frame.calibrated) {
        return false;
    }

    const bool validating =
        gaze_calibration_phase_ == GazeCalibrationPhase::Validate;
    const auto &target = validating
        ? gaze_validation_targets_[gaze_target_index_]
        : gaze_targets_[gaze_target_index_];
    int width = 1;
    int height = 1;
    SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
    const float distance = std::hypot((frame.screen.x - target[0]) * width,
                                      (frame.screen.y - target[1]) * height);
    return distance > std::max(420.0f,
                               std::min(width, height) * 0.35f);
}

void Lab::update_gaze_calibration(const SgGazeFrame &frame,
                                  int64_t now_ms) {
    const int64_t acquire_ms = demo_ ? 160 : 900;
    const int64_t transition_ms = demo_ ? 90 : 650;
    const int64_t capture_ms = demo_ ? 180 : 900;
    const int minimum_samples = demo_ ? 2 : 10;

    if (gaze_calibration_phase_ == GazeCalibrationPhase::Acquire) {
        if (!frame.present) {
            gaze_target_since_ = 0;
            gaze_mouth_armed_ = false;
            gaze_mouth_open_since_ = 0;
            return;
        }
        gaze_mouth_reference_ = gaze_mouth_reference_ > 0.0f
            ? std::min(gaze_mouth_reference_, frame.mouth_openness)
            : frame.mouth_openness;
        gaze_mouth_armed_ = true;
        gaze_calibration_.left_open_reference = std::max(
            gaze_calibration_.left_open_reference,
            frame.left_eye_openness);
        gaze_calibration_.right_open_reference = std::max(
            gaze_calibration_.right_open_reference,
            frame.right_eye_openness);
        if (gaze_target_since_ == 0) {
            gaze_target_since_ = now_ms;
        }
        if (now_ms - gaze_target_since_ >= acquire_ms) {
            gaze_calibration_phase_ = GazeCalibrationPhase::Screen;
            gaze_target_index_ = 0;
            gaze_sequence_running_ = demo_;
            gaze_sequence_start_requested_ = false;
            gaze_mouth_open_since_ = 0;
            reset_gaze_sample(0);
        }
        return;
    }

    if (!frame.present) {
        gaze_mouth_open_since_ = 0;
        if (gaze_sequence_running_ && gaze_target_since_ != 0) {
            reset_gaze_sample(0);
        }
        return;
    }
    const float mouth_closed_limit = gaze_mouth_reference_ +
        std::max(0.025f, gaze_mouth_reference_ * 0.25f);
    const float mouth_open_limit = gaze_mouth_reference_ +
        std::max(0.08f, gaze_mouth_reference_ * 0.70f);
    if (frame.mouth_openness < mouth_closed_limit) {
        gaze_mouth_armed_ = true;
        gaze_mouth_open_since_ = 0;
    } else if (!gaze_sequence_running_ && gaze_mouth_armed_ &&
               frame.mouth_openness > mouth_open_limit) {
        if (gaze_mouth_open_since_ == 0) {
            gaze_mouth_open_since_ = now_ms;
        }
        if (now_ms - gaze_mouth_open_since_ >= 240) {
            gaze_sequence_start_requested_ = true;
            gaze_mouth_armed_ = false;
        }
    }
    if (!demo_ && !gaze_sequence_running_ &&
        gaze_sequence_start_requested_) {
        gaze_sequence_running_ = true;
        gaze_sequence_start_requested_ = false;
        gaze_mouth_open_since_ = 0;
        reset_gaze_sample(0);
        SDL_Log("Gaze calibration started");
    }
    if (!gaze_sequence_running_) {
        return;
    }
    const float left_reference = gaze_calibration_.left_open_reference > 0.0f
        ? gaze_calibration_.left_open_reference
        : 0.23f;
    const float right_reference = gaze_calibration_.right_open_reference > 0.0f
        ? gaze_calibration_.right_open_reference
        : 0.23f;
    const bool eyes_closed =
        frame.left_eye_openness < left_reference * 0.68f &&
        frame.right_eye_openness < right_reference * 0.68f;

    if (gaze_target_since_ == 0) {
        reset_gaze_sample(now_ms);
    } else if (!demo_ && eyes_closed) {
        reset_gaze_sample(0);
        return;
    }

    const bool validating =
        gaze_calibration_phase_ == GazeCalibrationPhase::Validate;
    const auto &target = validating
        ? gaze_validation_targets_[gaze_target_index_]
        : gaze_targets_[gaze_target_index_];
    if (gaze_capture_since_ == 0) {
        if (now_ms - gaze_target_since_ < transition_ms) {
            return;
        }
        if (!demo_ && !gaze_target_fixated(frame)) {
            return;
        }
        gaze_capture_since_ = now_ms;
        gaze_sample_sequence_ = 0;
        for (auto &samples : gaze_feature_samples_) {
            samples.clear();
        }
        gaze_left_open_samples_.clear();
        gaze_right_open_samples_.clear();
        gaze_validation_error_samples_.clear();
        gaze_sample_count_ = 0;
        SDL_Log("Gaze point %d locked", gaze_target_index_ + 1);
    }

    if (frame.sequence != gaze_sample_sequence_) {
        for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
            gaze_feature_samples_[feature].push_back(frame.features[feature]);
        }
        gaze_left_open_samples_.push_back(frame.left_eye_openness);
        gaze_right_open_samples_.push_back(frame.right_eye_openness);
        ++gaze_sample_count_;
        gaze_sample_sequence_ = frame.sequence;

        if (validating) {
            int width = 1;
            int height = 1;
            SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
            const double dx = (frame.screen.x - target[0]) * width;
            const double dy = (frame.screen.y - target[1]) * height;
            gaze_validation_error_samples_.push_back(
                static_cast<float>(std::hypot(dx, dy)));
        }
    }

    if (!demo_ && validating && gaze_capture_drifted(frame)) {
        ++gaze_capture_drift_frames_;
        if (gaze_capture_drift_frames_ >= 3) {
            gaze_capture_since_ = 0;
            gaze_sample_sequence_ = 0;
            for (auto &samples : gaze_probe_samples_) {
                samples.clear();
            }
            for (auto &samples : gaze_feature_samples_) {
                samples.clear();
            }
            gaze_left_open_samples_.clear();
            gaze_right_open_samples_.clear();
            gaze_validation_error_samples_.clear();
            gaze_sample_count_ = 0;
            gaze_capture_drift_frames_ = 0;
            SDL_Log("Gaze point %d capture cancelled",
                    gaze_target_index_ + 1);
        }
        return;
    }
    gaze_capture_drift_frames_ = 0;

    if (now_ms - gaze_capture_since_ < capture_ms ||
        gaze_sample_count_ < minimum_samples) {
        return;
    }

    const uint32_t point = gaze_calibration_.point_count;
    if (validating) {
        for (const float error : gaze_validation_error_samples_) {
            gaze_validation_error_sum_ += error;
        }
        gaze_validation_sample_count_ +=
            static_cast<int>(gaze_validation_error_samples_.size());
    }
    if (!validating && point < SG_GAZE_MAX_CALIBRATION_POINTS) {
        for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
            gaze_calibration_.features[point][feature] =
                median(gaze_feature_samples_[feature]);
        }
        gaze_calibration_.screen_points[point][0] = target[0];
        gaze_calibration_.screen_points[point][1] = target[1];
        const float left_open = median(gaze_left_open_samples_);
        const float right_open = median(gaze_right_open_samples_);
        const float previous_points = static_cast<float>(point);
        gaze_calibration_.left_open_reference =
            (gaze_calibration_.left_open_reference * previous_points +
             left_open) / (previous_points + 1.0f);
        gaze_calibration_.right_open_reference =
            (gaze_calibration_.right_open_reference * previous_points +
             right_open) / (previous_points + 1.0f);
        ++gaze_calibration_.point_count;
    }
    SDL_Log("Gaze point %d captured", gaze_target_index_ + 1);

    ++gaze_target_index_;
    if (!validating &&
        gaze_target_index_ == static_cast<int>(gaze_targets_.size())) {
        int width = 1;
        int height = 1;
        SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
        gaze_calibration_.screen_aspect = static_cast<float>(width) /
            std::max(height, 1);
        if (!sg_gaze_calibration_fit(&gaze_calibration_)) {
            reset_gaze_calibration();
            return;
        }
        if (gaze_engine_) {
            std::lock_guard lock(engine_mutex_);
            sg_gaze_engine_set_calibration(gaze_engine_.get(),
                                           &gaze_calibration_);
        }
        if (!demo_) {
            save_gaze_calibration();
        }
        gaze_calibration_phase_ = GazeCalibrationPhase::Validate;
        gaze_target_index_ = 0;
        reset_gaze_sample(0);
        return;
    }

    if (validating &&
        gaze_target_index_ ==
            static_cast<int>(gaze_validation_targets_.size())) {
        finish_gaze_calibration();
        return;
    }
    reset_gaze_sample(0);
}

void Lab::update_eye_control(const SgGazeFrame &frame,
                             float delta_seconds,
                             int64_t now_ms) {
    if (!frame.present || !frame.calibrated) {
        eye_dwell_since_ = 0;
        eye_dwell_target_ = 0;
        eye_dwell_progress_ = 0.0f;
        return;
    }

    int width = 1;
    int height = 1;
    SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
    const float aspect = static_cast<float>(width) / std::max(height, 1);
    constexpr float release_x = 0.5f;
    constexpr float release_y = 0.91f;

    int target_kind = 0;
    if (gaze_grabbed_) {
        const float release_distance = std::hypot(
            (frame.screen.x - release_x) * width,
            (frame.screen.y - release_y) * height);
        if (release_distance < 82.0f) {
            target_kind = 2;
        }
    } else {
        const auto cube_screen = world_to_screen(cube_.position, aspect);
        const float cube_distance = std::hypot(
            (frame.screen.x - cube_screen.x) * width,
            (frame.screen.y - cube_screen.y) * height);
        if (cube_distance < std::max(90.0f, cube_.scale * 150.0f)) {
            target_kind = 1;
        }
    }

    if (target_kind == 0) {
        eye_dwell_since_ = 0;
        eye_dwell_target_ = 0;
        eye_dwell_progress_ = 0.0f;
    } else {
        if (eye_dwell_target_ != target_kind || eye_dwell_since_ == 0) {
            eye_dwell_target_ = target_kind;
            eye_dwell_since_ = now_ms;
        }
        eye_dwell_progress_ = std::clamp((now_ms - eye_dwell_since_) /
                                         950.0f,
                                         0.0f,
                                         1.0f);
        if (eye_dwell_progress_ >= 1.0f) {
            if (target_kind == 1) {
                const auto target = screen_to_world(frame.screen, aspect);
                gaze_grab_offset_ = {
                    cube_.position.x - target.x,
                    cube_.position.y - target.y,
                    cube_.position.z - target.z,
                };
                gaze_grabbed_ = true;
                cube_.grabbed = true;
                cube_.scaling = false;
            } else {
                gaze_grabbed_ = false;
                cube_.grabbed = false;
                cube_.velocity = {};
            }
            eye_dwell_since_ = 0;
            eye_dwell_target_ = 0;
            eye_dwell_progress_ = 0.0f;
        }
    }

    if (gaze_grabbed_) {
        const auto target = screen_to_world(frame.screen, aspect);
        const SgVec3 destination {
            target.x + gaze_grab_offset_.x,
            target.y + gaze_grab_offset_.y,
            target.z + gaze_grab_offset_.z,
        };
        cube_.velocity = {
            (destination.x - cube_.position.x) /
                std::max(delta_seconds, 0.001f),
            (destination.y - cube_.position.y) /
                std::max(delta_seconds, 0.001f),
            (destination.z - cube_.position.z) /
                std::max(delta_seconds, 0.001f),
        };
        cube_.position = destination;
    }
}

void Lab::update_cube(const SgGestureFrame &frame,
                      float delta_seconds) {
    if (frame.gesture == SG_GESTURE_EXPLODE && cube_.fragments.empty()) {
        explode_cube();
        return;
    }
    if (frame.gesture == SG_GESTURE_RESET) {
        reset_cube();
        return;
    }

    int width = 1;
    int height = 1;
    SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
    const float aspect = static_cast<float>(width) / std::max(height, 1);
    std::array<const SgGestureHand *, 2> pinching {};
    int pinch_count = 0;
    for (uint32_t i = 0; i < frame.hand_count && pinch_count < 2; ++i) {
        if (frame.hands[i].pinching) {
            pinching[pinch_count++] = &frame.hands[i];
        }
    }

    if (pinch_count == 2) {
        const auto a = pinch_point(*pinching[0]);
        const auto b = pinch_point(*pinching[1]);
        const float distance = sg::distance_3d(a, b);
        const float angle = pointer_angle(a, b);
        const float depth_delta = b.z - a.z;
        const SgVec3 midpoint {(a.x + b.x) * 0.5f,
                              (a.y + b.y) * 0.5f,
                              (a.z + b.z) * 0.5f};
        if (!cube_.scaling) {
            cube_.scaling = true;
            cube_.grabbed = true;
            scale_start_distance_ = std::max(distance, 0.05f);
            scale_start_value_ = cube_.scale;
            scale_start_angle_ = angle;
            scale_start_rotation_ = cube_.rotation.z;
            scale_start_depth_delta_ = depth_delta;
            scale_start_rotation_y_ = cube_.rotation.y;
        }
        cube_.scale = std::clamp(scale_start_value_ * distance / scale_start_distance_,
                                 0.24f,
                                 0.82f);
        cube_.rotation.z = scale_start_rotation_ +
            (angle - scale_start_angle_) * 180.0f / static_cast<float>(M_PI);
        cube_.rotation.y = scale_start_rotation_y_ +
            (depth_delta - scale_start_depth_delta_) * 150.0f;
        const auto target = screen_to_world(midpoint, aspect);
        cube_.velocity = {
            (target.x - cube_.position.x) / std::max(delta_seconds, 0.001f),
            (target.y - cube_.position.y) / std::max(delta_seconds, 0.001f),
            (target.z - cube_.position.z) / std::max(delta_seconds, 0.001f),
        };
        cube_.position = target;
        return;
    }

    if (pinch_count == 1) {
        const auto point = pinch_point(*pinching[0]);
        const auto target = screen_to_world(point, aspect);
        if (!cube_.grabbed) {
            const auto cube_screen = world_to_screen(cube_.position, aspect);
            const float hit_distance = sg::distance_2d(point.x,
                                                        point.y,
                                                        cube_screen.x,
                                                        cube_screen.y);
            if (hit_distance < 0.12f + cube_.scale * 0.09f) {
                cube_.grabbed = true;
                cube_.scaling = false;
                grab_offset_ = {
                    cube_.position.x - target.x,
                    cube_.position.y - target.y,
                    cube_.position.z - target.z,
                };
            }
        }
        if (cube_.grabbed) {
            const SgVec3 destination {
                target.x + grab_offset_.x,
                target.y + grab_offset_.y,
                target.z + grab_offset_.z,
            };
            cube_.velocity = {
                (destination.x - cube_.position.x) / std::max(delta_seconds, 0.001f),
                (destination.y - cube_.position.y) / std::max(delta_seconds, 0.001f),
                (destination.z - cube_.position.z) / std::max(delta_seconds, 0.001f),
            };
            cube_.position = destination;
            const auto &wrist = pinching[0]->landmarks[0].screen;
            const auto &middle = pinching[0]->landmarks[12].screen;
            cube_.rotation.z = pointer_angle(wrist, middle) * 180.0f /
                static_cast<float>(M_PI) + 90.0f;
        }
        return;
    }

    if (cube_.grabbed) {
        cube_.grabbed = false;
        cube_.scaling = false;
        cube_.velocity.x = std::clamp(cube_.velocity.x, -7.0f, 7.0f);
        cube_.velocity.y = std::clamp(cube_.velocity.y, -7.0f, 7.0f);
        cube_.velocity.z = std::clamp(cube_.velocity.z, -5.0f, 5.0f);
    }
}

void Lab::update_physics(float delta_seconds) {
    if (!cube_.fragments.empty()) {
        for (auto &fragment : cube_.fragments) {
            fragment.velocity.y -= 2.8f * delta_seconds;
            fragment.position.x += fragment.velocity.x * delta_seconds;
            fragment.position.y += fragment.velocity.y * delta_seconds;
            fragment.position.z += fragment.velocity.z * delta_seconds;
            fragment.rotation.x += 110.0f * delta_seconds;
            fragment.rotation.y += 150.0f * delta_seconds;
            fragment.life -= delta_seconds;
        }
        std::erase_if(cube_.fragments, [](const Fragment &fragment) {
            return fragment.life <= 0.0f;
        });
        if (cube_.fragments.empty()) {
            reset_cube();
        }
        return;
    }
    if (cube_.grabbed) {
        return;
    }

    cube_.velocity.y -= 2.6f * delta_seconds;
    cube_.position.x += cube_.velocity.x * delta_seconds;
    cube_.position.y += cube_.velocity.y * delta_seconds;
    cube_.position.z += cube_.velocity.z * delta_seconds;
    cube_.rotation.x += (18.0f + std::abs(cube_.velocity.y) * 16.0f) * delta_seconds;
    cube_.rotation.y += (24.0f + std::abs(cube_.velocity.x) * 14.0f) * delta_seconds;
    cube_.velocity.x *= std::pow(0.30f, delta_seconds);
    cube_.velocity.z *= std::pow(0.30f, delta_seconds);

    const float floor = -1.65f + cube_.scale;
    if (cube_.position.y < floor) {
        cube_.position.y = floor;
        cube_.velocity.y = std::abs(cube_.velocity.y) * 0.58f;
        if (cube_.velocity.y < 0.25f) {
            cube_.velocity.y = 0.0f;
        }
    }
    const float wall = 3.0f - cube_.scale;
    if (std::abs(cube_.position.x) > wall) {
        cube_.position.x = std::copysign(wall, cube_.position.x);
        cube_.velocity.x *= -0.62f;
    }
    if (cube_.position.z < -1.8f || cube_.position.z > 0.85f) {
        cube_.position.z = std::clamp(cube_.position.z, -1.8f, 0.85f);
        cube_.velocity.z *= -0.60f;
    }
}

void Lab::reset_cube() {
    cube_ = CubeState {};
}

void Lab::explode_cube() {
    cube_.visible = false;
    cube_.grabbed = false;
    cube_.scaling = false;
    cube_.fragments.clear();
    const float piece = cube_.scale / 3.0f;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                Fragment fragment;
                fragment.position = {
                    cube_.position.x + x * piece * 1.6f,
                    cube_.position.y + y * piece * 1.6f,
                    cube_.position.z + z * piece * 1.6f,
                };
                fragment.velocity = {
                    x * 1.5f + z * 0.22f,
                    y * 1.35f + 1.15f,
                    z * 1.5f - x * 0.18f,
                };
                fragment.rotation = cube_.rotation;
                fragment.size = piece * 0.72f;
                fragment.life = 1.9f + (x + y + z + 3) * 0.045f;
                cube_.fragments.push_back(fragment);
            }
        }
    }
}

void Lab::reset_calibration() {
    calibration_ = {};
    target_index_ = 0;
    calibration_sample_sequence_ = 0;
    calibration_sample_x_ = 0.0f;
    calibration_sample_y_ = 0.0f;
    calibration_sample_count_ = 0;
    pinch_count_ = 0;
    hold_since_ = 0;
    sampled_near_span_ = 0.0f;
    sampled_far_span_ = 1.0f;
    sampled_closed_ratio_ = 1.0f;
    sampled_open_ratio_ = 0.0f;
    previous_pinch_ = false;
    calibration_phase_ = CalibrationPhase::Welcome;
    calibration_phase_since_ = monotonic_ms();
    renderer_->set_fullscreen(true);
    int width = 1;
    int height = 1;
    SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
    calibration_.screen_aspect = static_cast<float>(width) / std::max(height, 1);
    std::lock_guard lock(engine_mutex_);
    sg_gesture_engine_set_calibration(engine_.get(), &calibration_);
}

void Lab::finish_calibration() {
    calibration_.near_palm_span = std::max(sampled_near_span_, 0.20f);
    calibration_.far_palm_span = std::min(sampled_far_span_,
                                          calibration_.near_palm_span - 0.06f);
    calibration_.far_palm_span = std::max(calibration_.far_palm_span, 0.05f);
    calibration_.pinch_closed_ratio = std::min(sampled_closed_ratio_, 0.32f);
    calibration_.pinch_open_ratio = std::max(sampled_open_ratio_,
                                             calibration_.pinch_closed_ratio + 0.18f);
    calibration_.has_depth_range = true;
    calibration_.has_pinch_range = true;
    {
        std::lock_guard lock(engine_mutex_);
        sg_gesture_engine_set_calibration(engine_.get(), &calibration_);
    }
    if (!demo_) {
        save_calibration();
    }
    calibration_phase_ = CalibrationPhase::Complete;
    renderer_->set_fullscreen(presentation_);
    reset_cube();
}

void Lab::load_calibration() {
    if (sg_gesture_calibration_load(&calibration_, calibration_path().c_str()) &&
        calibration_.has_screen_mapping &&
        calibration_.has_depth_range &&
        calibration_.has_pinch_range) {
        std::lock_guard lock(engine_mutex_);
        sg_gesture_engine_set_calibration(engine_.get(), &calibration_);
        calibration_phase_ = CalibrationPhase::Complete;
    } else {
        calibration_ = {};
        calibration_phase_ = CalibrationPhase::Welcome;
    }
}

void Lab::load_gaze_calibration() {
    if (!sg_gaze_calibration_load(&gaze_calibration_,
                                  gaze_calibration_path().c_str()) ||
        !gaze_calibration_acceptable()) {
        gaze_calibration_ = {};
        return;
    }
    std::lock_guard lock(engine_mutex_);
    sg_gaze_engine_set_calibration(gaze_engine_.get(), &gaze_calibration_);
    gaze_calibration_phase_ = GazeCalibrationPhase::Complete;
}

void Lab::save_calibration() {
    const auto path = std::filesystem::path(calibration_path());
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (!error) {
        sg_gesture_calibration_save(&calibration_, path.c_str());
    }
}

void Lab::save_gaze_calibration() {
    const auto path = std::filesystem::path(gaze_calibration_path());
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (!error &&
        sg_gaze_calibration_save(&gaze_calibration_, path.c_str())) {
        SDL_Log("Gaze calibration checkpoint saved");
    }
}

void Lab::configure_gaze_displays() {
    static constexpr std::array<std::array<float, 2>, 9> points {{
        {{0.50f, 0.50f}}, {{0.12f, 0.18f}}, {{0.50f, 0.18f}},
        {{0.88f, 0.18f}}, {{0.88f, 0.50f}}, {{0.88f, 0.82f}},
        {{0.50f, 0.82f}}, {{0.12f, 0.82f}}, {{0.12f, 0.50f}},
    }};
    static constexpr std::array<std::array<float, 2>, 3> extra_points {{
        {{0.28f, 0.34f}}, {{0.72f, 0.34f}}, {{0.50f, 0.66f}},
    }};
    static constexpr std::array<std::array<float, 2>, 2> validation_points {{
        {{0.28f, 0.34f}}, {{0.72f, 0.66f}},
    }};

    std::vector<SDL_Rect> displays;
    const int display_count = SDL_GetNumVideoDisplays();
    for (int display = 0;
         display < display_count &&
         displays.size() < SG_GAZE_MAX_DISPLAYS;
         ++display) {
        SDL_Rect bounds {};
        if (SDL_GetDisplayBounds(display, &bounds) == 0 &&
            bounds.w > 0 && bounds.h > 0) {
            displays.push_back(bounds);
        }
    }
    if (displays.empty()) {
        int width = 1;
        int height = 1;
        SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
        displays.push_back({0, 0, width, height});
    }
    std::sort(displays.begin(), displays.end(),
              [](const SDL_Rect &left, const SDL_Rect &right) {
                  const int left_x = left.x + left.w / 2;
                  const int right_x = right.x + right.w / 2;
                  return left_x == right_x
                      ? left.y + left.h / 2 < right.y + right.h / 2
                      : left_x < right_x;
              });

    int span_left = displays.front().x;
    int span_top = displays.front().y;
    int span_right = displays.front().x + displays.front().w;
    int span_bottom = displays.front().y + displays.front().h;
    for (const auto &display : displays) {
        span_left = std::min(span_left, display.x);
        span_top = std::min(span_top, display.y);
        span_right = std::max(span_right, display.x + display.w);
        span_bottom = std::max(span_bottom, display.y + display.h);
    }
    const float span_width = std::max(span_right - span_left, 1);
    const float span_height = std::max(span_bottom - span_top, 1);
    gaze_calibration_.display_count = static_cast<uint32_t>(displays.size());
    for (size_t display = 0; display < displays.size(); ++display) {
        const auto &bounds = displays[display];
        gaze_calibration_.display_bounds[display][0] =
            (bounds.x - span_left) / span_width;
        gaze_calibration_.display_bounds[display][1] =
            (bounds.y - span_top) / span_height;
        gaze_calibration_.display_bounds[display][2] = bounds.w / span_width;
        gaze_calibration_.display_bounds[display][3] = bounds.h / span_height;
    }

    size_t center_display = 0;
    float center_distance = 10.0f;
    for (size_t display = 0; display < displays.size(); ++display) {
        const auto &bounds = gaze_calibration_.display_bounds[display];
        const float dx = bounds[0] + bounds[2] * 0.5f - 0.5f;
        const float dy = bounds[1] + bounds[3] * 0.5f - 0.5f;
        const float distance = dx * dx + dy * dy;
        if (distance < center_distance) {
            center_display = display;
            center_distance = distance;
        }
    }
    std::vector<size_t> order {center_display};
    for (size_t offset = 1; order.size() < displays.size(); ++offset) {
        if (center_display >= offset) {
            order.push_back(center_display - offset);
        }
        if (center_display + offset < displays.size()) {
            order.push_back(center_display + offset);
        }
    }

    gaze_targets_.clear();
    gaze_validation_targets_.clear();
    const auto add_target = [this](size_t display,
                                   const std::array<float, 2> &point,
                                   auto &targets) {
        const auto &bounds = gaze_calibration_.display_bounds[display];
        targets.push_back({
            bounds[0] + point[0] * bounds[2],
            bounds[1] + point[1] * bounds[3],
        });
    };
    for (const size_t display : order) {
        for (const auto &point : points) {
            add_target(display, point, gaze_targets_);
        }
        if (displays.size() == 1) {
            for (const auto &point : extra_points) {
                add_target(display, point, gaze_targets_);
            }
        }
        for (const auto &point : validation_points) {
            add_target(display, point, gaze_validation_targets_);
        }
    }
    SDL_Log("Gaze calibration mapped %zu displays and %zu points",
            displays.size(),
            gaze_targets_.size());
}

void Lab::reset_gaze_calibration() {
    gaze_calibration_ = {};
    gaze_calibration_phase_ = GazeCalibrationPhase::Acquire;
    gaze_target_index_ = 0;
    gaze_validation_error_sum_ = 0.0;
    gaze_validation_sample_count_ = 0;
    gaze_validation_error_px_ = 0.0f;
    gaze_sequence_running_ = demo_;
    gaze_sequence_start_requested_ = false;
    gaze_mouth_armed_ = false;
    gaze_mouth_reference_ = 0.0f;
    gaze_mouth_open_since_ = 0;
    gaze_horizontal_response_sign_ = 0.0f;
    gaze_vertical_response_sign_ = 0.0f;
    gaze_grabbed_ = false;
    eye_only_ = false;
    eye_dwell_since_ = 0;
    eye_dwell_target_ = 0;
    eye_dwell_progress_ = 0.0f;
    reset_gaze_sample(0);
    renderer_->set_fullscreen(true);
    configure_gaze_displays();
    int width = 1;
    int height = 1;
    SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
    gaze_calibration_.screen_aspect = static_cast<float>(width) /
        std::max(height, 1);
    if (gaze_engine_) {
        std::lock_guard lock(engine_mutex_);
        sg_gaze_engine_set_calibration(gaze_engine_.get(),
                                       &gaze_calibration_);
    }
}

void Lab::finish_gaze_calibration() {
    if (!sg_gaze_calibration_fit(&gaze_calibration_)) {
        reset_gaze_calibration();
        return;
    }
    gaze_validation_error_px_ = gaze_validation_sample_count_ > 0
        ? static_cast<float>(gaze_validation_error_sum_ /
                             gaze_validation_sample_count_)
        : 0.0f;
    gaze_calibration_.mean_error = gaze_validation_error_px_;
    SDL_Log("Gaze calibration complete: %.0f px mean error",
            gaze_validation_error_px_);
    if (!gaze_calibration_acceptable()) {
        gaze_calibration_.valid = false;
        if (gaze_engine_) {
            std::lock_guard lock(engine_mutex_);
            sg_gaze_engine_set_calibration(gaze_engine_.get(),
                                           &gaze_calibration_);
        }
        gaze_calibration_phase_ = GazeCalibrationPhase::Failed;
        gaze_sequence_running_ = false;
        gaze_calibration_requested_ = false;
        eye_only_requested_ = false;
        eye_only_ = false;
        control_mode_ = ControlMode::Select;
        gaze_grabbed_ = false;
        reset_cube();
        return;
    }
    if (gaze_engine_) {
        std::lock_guard lock(engine_mutex_);
        sg_gaze_engine_set_calibration(gaze_engine_.get(),
                                       &gaze_calibration_);
    }
    if (!demo_) {
        save_gaze_calibration();
    }
    gaze_calibration_phase_ = GazeCalibrationPhase::Complete;
    gaze_sequence_running_ = false;
    gaze_calibration_requested_ = false;
    eye_only_ = control_mode_.load(std::memory_order_relaxed) ==
        ControlMode::Eyes;
    reset_cube();
}

bool Lab::gaze_calibration_acceptable() const {
    if (!gaze_calibration_.valid) {
        return false;
    }
    if (demo_) {
        return true;
    }
    if (gaze_calibration_.mean_error <= 0.0f) {
        return false;
    }
    SDL_Rect span {};
    bool has_span = false;
    const int displays = SDL_GetNumVideoDisplays();
    for (int display = 0; display < displays; ++display) {
        SDL_Rect bounds {};
        if (SDL_GetDisplayBounds(display, &bounds) != 0) {
            continue;
        }
        if (!has_span) {
            span = bounds;
            has_span = true;
            continue;
        }
        const int right = std::max(span.x + span.w, bounds.x + bounds.w);
        const int bottom = std::max(span.y + span.h, bounds.y + bounds.h);
        span.x = std::min(span.x, bounds.x);
        span.y = std::min(span.y, bounds.y);
        span.w = right - span.x;
        span.h = bottom - span.y;
    }
    int width = 1;
    int height = 1;
    if (has_span) {
        width = span.w;
        height = span.h;
    } else {
        SDL_GL_GetDrawableSize(renderer_->window(), &width, &height);
    }
    const float limit = std::max(120.0f,
                                 std::min(width, height) * 0.18f);
    return gaze_calibration_.mean_error <= limit;
}

CalibrationView Lab::calibration_view(int64_t now_ms) const {
    CalibrationView view;
    view.visible = calibration_phase_ != CalibrationPhase::Complete;
    view.phase = calibration_phase_;
    view.target_index = target_index_;
    view.pinch_count = pinch_count_;
    view.target_x = 0.5f;
    view.target_y = 0.5f;

    switch (calibration_phase_) {
    case CalibrationPhase::Welcome:
        view.title = "Show one open hand";
        view.instruction = "Keep the palm facing the camera. Every joint and fingertip should appear on screen.";
        view.progress = hold_since_ ? std::clamp((now_ms - hold_since_) / 650.0f, 0.0f, 1.0f) : 0.0f;
        break;
    case CalibrationPhase::Screen:
        view.title = "Map point " + std::to_string(target_index_ + 1) +
            " of " + std::to_string(SG_GESTURE_SCREEN_POINT_COUNT);
        view.instruction = "Move the index dot into the target, then pinch and hold. The grid spans every connected display.";
        view.target_x = screen_targets_[target_index_][0];
        view.target_y = screen_targets_[target_index_][1];
        view.progress = (target_index_ +
            (hold_since_ ? std::clamp((now_ms - hold_since_) / 320.0f, 0.0f, 1.0f) : 0.0f)) /
            SG_GESTURE_SCREEN_POINT_COUNT;
        break;
    case CalibrationPhase::NearDepth:
        view.title = "Bring the open hand closer";
        view.instruction = "Move toward the camera until the fingertip depth stalks reach their shortest position.";
        view.progress = hold_since_ ? std::clamp((now_ms - hold_since_) / 1800.0f, 0.0f, 1.0f) : 0.0f;
        break;
    case CalibrationPhase::FarDepth:
        view.title = "Push the open hand toward the screen";
        view.instruction = "Move away from the camera. The dots stay projected while their depth stalks extend.";
        view.progress = hold_since_ ? std::clamp((now_ms - hold_since_) / 1800.0f, 0.0f, 1.0f) : 0.0f;
        break;
    case CalibrationPhase::Pinch:
        view.title = "Pinch three times";
        view.instruction = "Touch thumb and index, open them fully, then repeat. Watch the line lock between both fingertips.";
        view.progress = std::clamp(pinch_count_ / 3.0f, 0.0f, 1.0f);
        break;
    case CalibrationPhase::Complete:
        view.visible = false;
        break;
    }
    return view;
}

GazeCalibrationView Lab::gaze_calibration_view(int64_t now_ms,
                                                bool face_present) const {
    GazeCalibrationView view;
    view.phase = gaze_calibration_phase_;
    view.visible = gaze_calibration_phase_ == GazeCalibrationPhase::Acquire ||
        gaze_calibration_phase_ == GazeCalibrationPhase::Screen ||
        gaze_calibration_phase_ == GazeCalibrationPhase::Validate;
    view.face_present = face_present;
    view.model_active = face_present;
    view.error_px = gaze_validation_sample_count_ > 0
        ? static_cast<float>(gaze_validation_error_sum_ /
                             gaze_validation_sample_count_)
        : gaze_validation_error_px_;
    if (!view.visible) {
        return view;
    }

    const int64_t capture_ms = demo_ ? 180 : 900;
    const float target_progress = gaze_capture_since_ > 0
        ? std::clamp((now_ms - gaze_capture_since_) /
                     static_cast<float>(capture_ms),
                     0.0f,
                     1.0f)
        : 0.0f;
    view.sampling = gaze_capture_since_ > 0;
    view.paused = !demo_ && !gaze_sequence_running_ &&
        (gaze_calibration_phase_ == GazeCalibrationPhase::Screen ||
         gaze_calibration_phase_ == GazeCalibrationPhase::Validate);

    switch (gaze_calibration_phase_) {
    case GazeCalibrationPhase::Acquire:
        view.title = face_present ? "Eyes detected" : "Look toward the camera";
        view.instruction = face_present
            ? "Dedicated gaze model locked. Keep your head in a natural position."
            : "Keep both eyes visible, including through glasses. No image leaves the camera pipeline.";
        view.target_x = 0.5f;
        view.target_y = 0.5f;
        view.target_count = 1;
        view.progress = gaze_target_since_ > 0
            ? std::clamp((now_ms - gaze_target_since_) /
                         static_cast<float>(demo_ ? 160 : 900),
                         0.0f,
                         1.0f)
            : 0.0f;
        break;
    case GazeCalibrationPhase::Screen:
        if (!face_present && !demo_) {
            view.title = "Eyes not detected";
            view.instruction = "Face the camera until both eyes are visible.";
        } else if (view.paused) {
            view.title = gaze_target_index_ == 0
                ? "Ready to start"
                : "Waiting for eye tracking";
            view.instruction = "Open your mouth once, then follow the blue point. The sequence will not stop or jump ahead.";
        } else if (view.sampling) {
            view.title = "Capturing this point";
            view.instruction = "Keep looking at it. Looking away cancels this green sample.";
        } else {
            view.title = "Look at the blue point";
            view.instruction = "It stays blue until your eye signal reaches it and becomes stable.";
        }
        view.target_index = gaze_target_index_;
        view.target_count = static_cast<int>(gaze_targets_.size());
        view.target_x = gaze_targets_[gaze_target_index_][0];
        view.target_y = gaze_targets_[gaze_target_index_][1];
        view.progress = (gaze_target_index_ + target_progress) /
            gaze_targets_.size();
        break;
    case GazeCalibrationPhase::Validate:
        if (!face_present && !demo_) {
            view.title = "Eyes not detected";
            view.instruction = "Face the camera until both eyes are visible.";
        } else if (view.paused) {
            view.title = "Accuracy check paused";
            view.instruction = "Open your mouth once to continue the hands-free check.";
        } else if (view.sampling) {
            view.title = "Checking gaze accuracy";
            view.instruction = "Keep looking at it. Looking away cancels this green sample.";
        } else {
            view.title = "Reach the blue check point";
            view.instruction = "It remains blue until the purple gaze estimate reaches it and settles.";
        }
        view.target_index = gaze_target_index_;
        view.target_count = static_cast<int>(gaze_validation_targets_.size());
        view.target_x = gaze_validation_targets_[gaze_target_index_][0];
        view.target_y = gaze_validation_targets_[gaze_target_index_][1];
        view.progress = (gaze_target_index_ + target_progress) /
            gaze_validation_targets_.size();
        break;
    case GazeCalibrationPhase::Failed:
    case GazeCalibrationPhase::Inactive:
    case GazeCalibrationPhase::Complete:
        break;
    }
    return view;
}

EyeControlView Lab::eye_control_view() const {
    EyeControlView view;
    view.enabled = eye_only_;
    view.grabbed = gaze_grabbed_;
    view.dwell_progress = eye_dwell_progress_;
    return view;
}

std::string Lab::calibration_path() const {
    const char *config_home = std::getenv("XDG_CONFIG_HOME");
    if (config_home && *config_home) {
        return std::string(config_home) + "/singularity/gesture-lab.calibration";
    }
    const char *user_home = std::getenv("HOME");
    return std::string(user_home ? user_home : ".") +
        "/.config/singularity/gesture-lab.calibration";
}

std::string Lab::gaze_calibration_path() const {
    const char *config_home = std::getenv("XDG_CONFIG_HOME");
    if (config_home && *config_home) {
        return std::string(config_home) + "/singularity/gesture-lab.gaze";
    }
    const char *user_home = std::getenv("HOME");
    return std::string(user_home ? user_home : ".") +
        "/.config/singularity/gesture-lab.gaze";
}

SgGestureFrame Lab::synthetic_frame(int64_t now_ms) const {
    SgGestureFrame frame {};
    frame.sequence = static_cast<uint64_t>(now_ms / 16);
    frame.timestamp_ms = now_ms;
    const float cycle = std::fmod(now_ms / 1000.0f, 12.0f);

    if (calibration_demo_ && calibration_phase_ != CalibrationPhase::Complete) {
        frame.hand_count = 1;
        bool pinching = false;
        float center_x = 0.50f;
        float center_y = 0.58f;
        float hand_scale = 1.0f;
        if (calibration_phase_ == CalibrationPhase::Screen) {
            const auto &target = screen_targets_[target_index_];
            center_x = target[0] + 0.072f;
            center_y = target[1] + 0.23f;
            pinching = true;
        } else if (calibration_phase_ == CalibrationPhase::NearDepth) {
            hand_scale = 1.42f;
        } else if (calibration_phase_ == CalibrationPhase::FarDepth) {
            hand_scale = 0.68f;
        } else if (calibration_phase_ == CalibrationPhase::Pinch) {
            const float elapsed = (now_ms - calibration_phase_since_) / 1000.0f;
            pinching = std::fmod(elapsed, 0.78f) < 0.32f;
        }
        fill_hand(frame.hands[0],
                  SG_HAND_RIGHT,
                  center_x,
                  center_y,
                  0.48f,
                  pinching,
                  false,
                  !pinching,
                  hand_scale);
        frame.gesture = pinching ? SG_GESTURE_GRAB : SG_GESTURE_OPEN_PALM;
        return frame;
    }

    if (cycle < 3.0f) {
        const float t = cycle / 3.0f;
        frame.hand_count = 1;
        fill_hand(frame.hands[0],
                  SG_HAND_RIGHT,
                  0.36f + t * 0.28f,
                  0.62f - std::sin(t * static_cast<float>(M_PI)) * 0.20f,
                  0.42f,
                  cycle > 0.45f && cycle < 2.7f,
                  false,
                  false);
        frame.gesture = frame.hands[0].pinching ? SG_GESTURE_GRAB : SG_GESTURE_POINT;
    } else if (cycle < 7.0f) {
        const float t = (cycle - 3.0f) / 4.0f;
        const float separation = 0.10f + t * 0.28f;
        frame.hand_count = 2;
        fill_hand(frame.hands[0], SG_HAND_LEFT,
                  0.50f - separation, 0.52f, 0.35f + t * 0.18f,
                  true, false, false);
        fill_hand(frame.hands[1], SG_HAND_RIGHT,
                  0.50f + separation, 0.48f, 0.52f - t * 0.18f,
                  true, false, false);
        frame.gesture = SG_GESTURE_SCALE;
    } else if (cycle < 8.0f) {
        frame.hand_count = 1;
        fill_hand(frame.hands[0], SG_HAND_RIGHT, 0.50f, 0.56f, 0.40f,
                  false, true, false);
        frame.gesture = SG_GESTURE_FIST;
    } else if (cycle < 8.15f) {
        frame.hand_count = 1;
        fill_hand(frame.hands[0], SG_HAND_RIGHT, 0.50f, 0.56f, 0.40f,
                  false, false, true);
        frame.gesture = SG_GESTURE_EXPLODE;
        frame.gesture_started = true;
    } else if (cycle < 10.0f) {
        frame.hand_count = 1;
        fill_hand(frame.hands[0], SG_HAND_RIGHT, 0.50f, 0.56f, 0.40f,
                  false, false, true);
        frame.gesture = SG_GESTURE_OPEN_PALM;
    } else {
        frame.hand_count = 2;
        fill_hand(frame.hands[0], SG_HAND_LEFT, 0.34f, 0.58f, 0.42f,
                  false, false, true);
        fill_hand(frame.hands[1], SG_HAND_RIGHT, 0.66f, 0.58f, 0.42f,
                  false, false, true);
        frame.gesture = SG_GESTURE_RESET;
    }
    return frame;
}

SgGazeFrame Lab::synthetic_gaze_frame(int64_t now_ms) const {
    SgGazeFrame frame {};
    frame.sequence = static_cast<uint64_t>(now_ms / 16);
    frame.timestamp_ms = now_ms;
    frame.present = true;
    frame.confidence = 0.98f;
    frame.model_active = true;
    frame.model_confidence = 0.92f;
    frame.left_eye_openness = 0.24f;
    frame.right_eye_openness = 0.25f;

    float x = 0.50f + std::sin(now_ms / 1400.0f) * 0.22f;
    float y = 0.48f + std::cos(now_ms / 1700.0f) * 0.16f;
    if (gaze_calibration_phase_ == GazeCalibrationPhase::Acquire) {
        x = 0.50f;
        y = 0.50f;
    } else if (gaze_calibration_phase_ == GazeCalibrationPhase::Screen) {
        x = gaze_targets_[gaze_target_index_][0];
        y = gaze_targets_[gaze_target_index_][1];
    } else if (gaze_calibration_phase_ == GazeCalibrationPhase::Validate) {
        x = gaze_validation_targets_[gaze_target_index_][0];
        y = gaze_validation_targets_[gaze_target_index_][1];
    }

    const float horizontal = (x - 0.5f) / 0.44f;
    const float vertical = (y - 0.5f) / 0.38f;
    frame.features[0] = horizontal;
    frame.features[1] = vertical;
    frame.gaze_yaw = horizontal;
    frame.gaze_pitch = vertical;
    frame.features[2] = horizontal * 0.96f + vertical * 0.02f;
    frame.features[3] = vertical * 1.02f - horizontal * 0.01f;
    frame.features[4] = horizontal * 1.03f - vertical * 0.02f;
    frame.features[5] = vertical * 0.97f + horizontal * 0.01f;
    frame.features[6] = horizontal * 0.08f;
    frame.features[7] = 0.56f + vertical * 0.05f;
    frame.features[8] = horizontal * 0.015f;
    frame.features[9] = 3.1f + vertical * 0.03f;
    frame.left_iris = {0.56f + horizontal * 0.012f,
                       0.43f + vertical * 0.008f,
                       0.0f};
    frame.right_iris = {0.44f + horizontal * 0.012f,
                        0.43f + vertical * 0.008f,
                        0.0f};
    frame.calibrated = gaze_calibration_.valid;
    if (!sg_gaze_calibration_predict(&gaze_calibration_,
                                     frame.features,
                                     &frame.screen)) {
        frame.screen = {x, y, 0.0f};
    }
    return frame;
}

} // namespace sg::lab
