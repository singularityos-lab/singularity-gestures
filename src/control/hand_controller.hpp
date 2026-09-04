#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <singularity/gesture.h>

#include "control/clipboard_gate.hpp"
#include "control/gesture_gate.hpp"
#include "control/guide_gate.hpp"
#include "control/lock_gate.hpp"
#include "control/screenshot_gate.hpp"
#include "control/virtual_pointer.hpp"
#include "lab/camera.hpp"

namespace sg::control {

enum class CalibrationPhase {
    Welcome,
    Screen,
    NearDepth,
    FarDepth,
    Pinch,
    Complete,
};

enum class HandAction {
    None,
    Point,
    Click,
    Drag,
    Scroll,
    SecondaryClick,
    SwitchNext,
    SwitchPrevious,
    Tiling,
    Workspace,
    Overview,
    ScreenshotFull,
    ScreenshotRegion,
    Lock,
    Paste,
    Guide,
    Rest,
    Paused,
};

struct CalibrationView {
    CalibrationPhase phase = CalibrationPhase::Welcome;
    int target_index = 0;
    int pinch_count = 0;
    float progress = 0.0f;
    float target_x = 0.5f;
    float target_y = 0.5f;
    std::string title;
    std::string instruction;
};

struct ControllerSnapshot {
    SgGestureFrame frame {};
    CalibrationView calibration;
    HandAction action = HandAction::None;
    bool calibrated = false;
    bool input_available = false;
    bool paused = false;
    float pause_progress = 0.0f;
    bool pause_target_enabled = true;
    float gesture_progress = 0.0f;
    bool screenshot_region_active = false;
    float screenshot_ax = 0.0f;
    float screenshot_ay = 0.0f;
    float screenshot_bx = 0.0f;
    float screenshot_by = 0.0f;
    uint64_t screenshot_serial = 0;
    bool screenshot_fullscreen = false;
    bool clipboard_active = false;
    float clipboard_anchor_x = 0.0f;
    float clipboard_anchor_y = 0.0f;
    float clipboard_cursor_x = 0.0f;
    float clipboard_cursor_y = 0.0f;
    bool lock_gesture_active = false;
    float lock_ax = 0.0f;
    float lock_ay = 0.0f;
    float lock_bx = 0.0f;
    float lock_by = 0.0f;
    bool guide_visible = false;
    std::string status;
};

class HandController {
public:
    explicit HandController(std::string runtime_dir);
    ~HandController();

    HandController(const HandController &) = delete;
    HandController &operator=(const HandController &) = delete;

    bool start(int screen_width, int screen_height, std::string &error);
    void stop();
    void tick(int64_t now_ms);
    void reset_calibration(float screen_aspect);
    ControllerSnapshot snapshot() const;

private:
    struct EngineDeleter {
        void operator()(SgGestureEngine *engine) const;
    };

    std::string runtime_dir_;
    sg::lab::Camera camera_;
    std::unique_ptr<SgGestureEngine, EngineDeleter> engine_;
    VirtualPointer pointer_;
    GestureGate gesture_gate_;
    DesktopGestureGate desktop_gesture_gate_;
    ScreenshotGestureGate screenshot_gate_;
    LockGestureGate lock_gate_;
    ClipboardGestureGate clipboard_gate_;
    GuideGestureGate guide_gate_;
    std::thread inference_thread_;
    std::atomic<bool> running_ {false};
    mutable std::mutex mutex_;
    std::mutex engine_mutex_;
    ControllerSnapshot snapshot_;
    SgGestureCalibration calibration_ {};
    uint64_t handled_sequence_ = 0;
    uint64_t calibration_sample_sequence_ = 0;
    CalibrationPhase calibration_phase_ = CalibrationPhase::Welcome;
    int target_index_ = 0;
    int pinch_count_ = 0;
    int64_t hold_since_ = 0;
    int64_t hand_lost_since_ = 0;
    int64_t pause_hold_since_ = 0;
    int64_t last_frame_received_ms_ = 0;
    int64_t last_point_pose_ms_ = 0;
    float calibration_sample_x_ = 0.0f;
    float calibration_sample_y_ = 0.0f;
    int calibration_sample_count_ = 0;
    float sampled_near_span_ = 0.0f;
    float sampled_far_span_ = 1.0f;
    float sampled_closed_ratio_ = 1.0f;
    float sampled_open_ratio_ = 0.0f;
    bool previous_pinch_ = false;
    bool pause_toggle_latched_ = false;
    bool pause_started_paused_ = false;
    bool paused_ = false;
    bool guide_visible_ = false;
    float screen_width_ = 1.0f;
    float screen_height_ = 1.0f;

    static constexpr std::array<std::array<float, 2>,
                                SG_GESTURE_SCREEN_POINT_COUNT> screen_targets_ {{
        {{0.08f, 0.20f}}, {{0.28f, 0.20f}}, {{0.50f, 0.20f}},
        {{0.72f, 0.20f}}, {{0.92f, 0.20f}},
        {{0.08f, 0.50f}}, {{0.28f, 0.50f}}, {{0.50f, 0.50f}},
        {{0.72f, 0.50f}}, {{0.92f, 0.50f}},
        {{0.08f, 0.78f}}, {{0.28f, 0.78f}}, {{0.50f, 0.78f}},
        {{0.72f, 0.78f}}, {{0.92f, 0.78f}},
    }};

    void inference_loop();
    void update_calibration(const SgGestureFrame &frame, int64_t now_ms);
    void finish_calibration();
    void update_input(const SgGestureFrame &frame, int64_t now_ms);
    void cancel_desktop_gesture();
    void publish_calibration_view(int64_t now_ms);
    bool load_calibration(float screen_aspect);
    void save_calibration() const;
    std::string calibration_path() const;
};

const char *hand_action_name(HandAction action);

} // namespace sg::control
