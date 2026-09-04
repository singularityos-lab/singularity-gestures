#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <singularity/gesture.h>
#include <singularity/gaze.h>

#include "lab/camera.hpp"

namespace sg::lab {

enum class CalibrationPhase {
    Welcome,
    Screen,
    NearDepth,
    FarDepth,
    Pinch,
    Complete,
};

enum class GazeCalibrationPhase {
    Inactive,
    Acquire,
    Screen,
    Validate,
    Failed,
    Complete,
};

enum class ControlMode {
    Select,
    Hands,
    Eyes,
};

struct CalibrationView {
    bool visible = false;
    CalibrationPhase phase = CalibrationPhase::Welcome;
    int target_index = 0;
    int pinch_count = 0;
    float progress = 0.0f;
    float target_x = 0.5f;
    float target_y = 0.5f;
    std::string title;
    std::string instruction;
};

struct GazeCalibrationView {
    bool visible = false;
    GazeCalibrationPhase phase = GazeCalibrationPhase::Inactive;
    int target_index = 0;
    int target_count = 0;
    float progress = 0.0f;
    float target_x = 0.5f;
    float target_y = 0.5f;
    float error_px = 0.0f;
    bool face_present = false;
    bool model_active = false;
    bool paused = false;
    bool sampling = false;
    std::string title;
    std::string instruction;
};

struct EyeControlView {
    bool enabled = false;
    bool grabbed = false;
    float dwell_progress = 0.0f;
    float release_x = 0.5f;
    float release_y = 0.91f;
};

struct Fragment {
    SgVec3 position {};
    SgVec3 velocity {};
    SgVec3 rotation {};
    float size = 0.2f;
    float life = 0.0f;
};

struct CubeState {
    SgVec3 position {0.0f, 0.0f, 0.0f};
    SgVec3 velocity {};
    SgVec3 rotation {18.0f, -26.0f, 0.0f};
    float scale = 0.58f;
    bool grabbed = false;
    bool scaling = false;
    bool visible = true;
    std::vector<Fragment> fragments;
};

struct RenderState {
    std::shared_ptr<const CameraFrame> camera;
    SgGestureFrame gestures {};
    SgGazeFrame gaze {};
    CubeState cube;
    CalibrationView calibration;
    GazeCalibrationView gaze_calibration;
    EyeControlView eye_control;
    std::string status;
    float fps = 0.0f;
    bool demo = false;
    bool presentation = false;
    ControlMode control_mode = ControlMode::Hands;
};

class Renderer;

class Lab {
public:
    Lab(std::string executable_dir,
        bool demo,
        bool calibration_demo,
        bool presentation,
        bool gaze_calibration,
        bool eyes_only);
    ~Lab();

    int run();

private:
    struct EngineDeleter {
        void operator()(SgGestureEngine *engine) const;
    };

    struct GazeEngineDeleter {
        void operator()(SgGazeEngine *engine) const;
    };

    std::string executable_dir_;
    bool demo_ = false;
    bool calibration_demo_ = false;
    bool presentation_ = false;
    bool gaze_calibration_requested_ = false;
    bool eye_only_requested_ = false;
    bool eye_only_ = false;
    std::atomic<ControlMode> control_mode_ {ControlMode::Select};
    bool running_ = true;
    Camera camera_;
    std::unique_ptr<SgGestureEngine, EngineDeleter> engine_;
    std::unique_ptr<SgGazeEngine, GazeEngineDeleter> gaze_engine_;
    std::mutex engine_mutex_;
    std::unique_ptr<Renderer> renderer_;
    std::thread inference_thread_;
    std::atomic<bool> inference_running_ {false};
    std::mutex gesture_mutex_;
    SgGestureFrame gesture_frame_ {};
    SgGazeFrame gaze_frame_ {};
    std::string inference_error_;
    CubeState cube_;
    CalibrationPhase calibration_phase_ = CalibrationPhase::Welcome;
    SgGestureCalibration calibration_ {};
    SgGazeCalibration gaze_calibration_ {};
    GazeCalibrationPhase gaze_calibration_phase_ = GazeCalibrationPhase::Inactive;
    std::vector<std::array<float, 2>> gaze_targets_;
    std::vector<std::array<float, 2>> gaze_validation_targets_;
    int gaze_target_index_ = 0;
    int64_t gaze_target_since_ = 0;
    int64_t gaze_capture_since_ = 0;
    uint64_t gaze_sample_sequence_ = 0;
    std::array<std::vector<float>, SG_GAZE_FEATURE_COUNT> gaze_probe_samples_;
    std::array<std::vector<float>, SG_GAZE_FEATURE_COUNT> gaze_feature_samples_;
    std::vector<float> gaze_left_open_samples_;
    std::vector<float> gaze_right_open_samples_;
    std::vector<float> gaze_validation_error_samples_;
    double gaze_validation_error_sum_ = 0.0;
    int gaze_sample_count_ = 0;
    int gaze_validation_sample_count_ = 0;
    float gaze_validation_error_px_ = 0.0f;
    bool gaze_sequence_running_ = false;
    bool gaze_sequence_start_requested_ = false;
    bool gaze_mouth_armed_ = false;
    float gaze_mouth_reference_ = 0.0f;
    int64_t gaze_mouth_open_since_ = 0;
    int gaze_capture_drift_frames_ = 0;
    float gaze_horizontal_response_sign_ = 0.0f;
    float gaze_vertical_response_sign_ = 0.0f;
    std::array<std::array<float, 2>, SG_GESTURE_SCREEN_POINT_COUNT> screen_targets_ {{
        {{0.08f, 0.20f}}, {{0.28f, 0.20f}}, {{0.50f, 0.20f}},
        {{0.72f, 0.20f}}, {{0.92f, 0.20f}},
        {{0.08f, 0.50f}}, {{0.28f, 0.50f}}, {{0.50f, 0.50f}},
        {{0.72f, 0.50f}}, {{0.92f, 0.50f}},
        {{0.08f, 0.78f}}, {{0.28f, 0.78f}}, {{0.50f, 0.78f}},
        {{0.72f, 0.78f}}, {{0.92f, 0.78f}},
    }};
    int target_index_ = 0;
    uint64_t calibration_sample_sequence_ = 0;
    float calibration_sample_x_ = 0.0f;
    float calibration_sample_y_ = 0.0f;
    int calibration_sample_count_ = 0;
    int pinch_count_ = 0;
    int64_t hold_since_ = 0;
    float sampled_near_span_ = 0.0f;
    float sampled_far_span_ = 1.0f;
    float sampled_closed_ratio_ = 1.0f;
    float sampled_open_ratio_ = 0.0f;
    bool previous_pinch_ = false;
    int64_t calibration_phase_since_ = 0;
    int64_t last_frame_ms_ = 0;
    SgVec3 grab_offset_ {};
    float scale_start_distance_ = 0.0f;
    float scale_start_value_ = 1.0f;
    float scale_start_angle_ = 0.0f;
    float scale_start_rotation_ = 0.0f;
    float scale_start_depth_delta_ = 0.0f;
    float scale_start_rotation_y_ = 0.0f;
    int64_t eye_dwell_since_ = 0;
    int eye_dwell_target_ = 0;
    float eye_dwell_progress_ = 0.0f;
    bool gaze_grabbed_ = false;
    SgVec3 gaze_grab_offset_ {};

    bool initialize();
    void start_inference();
    void stop_inference();
    void inference_loop();
    void handle_events();
    void select_control_mode(ControlMode mode);
    void handle_mode_click(int x, int y);
    void update(float delta_seconds, int64_t now_ms);
    void update_calibration(const SgGestureFrame &frame, int64_t now_ms);
    void update_gaze_calibration(const SgGazeFrame &frame, int64_t now_ms);
    void update_eye_control(const SgGazeFrame &frame,
                            float delta_seconds,
                            int64_t now_ms);
    void update_cube(const SgGestureFrame &frame, float delta_seconds);
    void update_physics(float delta_seconds);
    void reset_cube();
    void explode_cube();
    void reset_calibration();
    void finish_calibration();
    void load_calibration();
    void load_gaze_calibration();
    void save_calibration();
    void save_gaze_calibration();
    void configure_gaze_displays();
    void reset_gaze_calibration();
    void finish_gaze_calibration();
    void reset_gaze_sample(int64_t now_ms);
    bool gaze_target_fixated(const SgGazeFrame &frame);
    bool gaze_capture_drifted(const SgGazeFrame &frame) const;
    bool gaze_calibration_acceptable() const;
    CalibrationView calibration_view(int64_t now_ms) const;
    GazeCalibrationView gaze_calibration_view(int64_t now_ms,
                                               bool face_present) const;
    EyeControlView eye_control_view() const;
    std::string calibration_path() const;
    std::string gaze_calibration_path() const;
    SgGestureFrame synthetic_frame(int64_t now_ms) const;
    SgGazeFrame synthetic_gaze_frame(int64_t now_ms) const;
};

} // namespace sg::lab
