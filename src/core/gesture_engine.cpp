#include <singularity/gesture.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <vector>

#include "core/gesture_math.hpp"
#include "core/mediapipe_backend.hpp"

struct SgGestureEngine {
    std::unique_ptr<sg::MediaPipeBackend> backend;
    SgGestureCalibration calibration {};
    sg::ScreenMapping mapping;
    SgGestureFrame previous {};
    SgGestureKind last_gesture = SG_GESTURE_NONE;
    int64_t last_timestamp_ms = 0;
    std::array<int64_t, SG_GESTURE_MAX_HANDS> fist_since {};
    int64_t open_pair_since = 0;
    int64_t last_reset_ms = 0;
    uint32_t cpu_threads = 4;
    uint64_t sequence = 0;
};

namespace {

class CpuAffinityGuard {
public:
    explicit CpuAffinityGuard(uint32_t limit) {
        CPU_ZERO(&original_);
        if (limit == 0 || pthread_getaffinity_np(pthread_self(),
                                                 sizeof(original_),
                                                 &original_) != 0) {
            return;
        }
        cpu_set_t limited;
        CPU_ZERO(&limited);
        uint32_t selected = 0;
        for (int cpu = 0; cpu < CPU_SETSIZE && selected < limit; ++cpu) {
            if (CPU_ISSET(cpu, &original_)) {
                CPU_SET(cpu, &limited);
                ++selected;
            }
        }
        active_ = selected > 0 && pthread_setaffinity_np(pthread_self(),
                                                          sizeof(limited),
                                                          &limited) == 0;
    }

    ~CpuAffinityGuard() {
        if (active_) {
            pthread_setaffinity_np(pthread_self(), sizeof(original_), &original_);
        }
    }

private:
    cpu_set_t original_ {};
    bool active_ = false;
};

void set_error(char *error, size_t error_size, const std::string &message) {
    if (!error || error_size == 0) {
        return;
    }
    std::snprintf(error, error_size, "%s", message.c_str());
}

const SgGestureHand *find_previous(const SgGestureFrame &frame,
                                   SgHandedness handedness) {
    for (uint32_t i = 0; i < frame.hand_count; ++i) {
        if (frame.hands[i].present && frame.hands[i].handedness == handedness) {
            return &frame.hands[i];
        }
    }
    return nullptr;
}

bool explosive_transition(SgGestureEngine &engine,
                          const SgGestureFrame &frame,
                          int64_t timestamp_ms) {
    bool explode = false;
    for (uint32_t i = 0; i < frame.hand_count; ++i) {
        const auto &hand = frame.hands[i];
        const int slot = hand.handedness == SG_HAND_RIGHT ? 1 : 0;
        if (hand.fist) {
            if (engine.fist_since[slot] == 0) {
                engine.fist_since[slot] = timestamp_ms;
            }
            continue;
        }
        const int64_t since = engine.fist_since[slot];
        if (hand.open_palm && since > 0 && timestamp_ms - since < 850) {
            explode = true;
        }
        if (!hand.fist) {
            engine.fist_since[slot] = 0;
        }
    }
    return explode;
}

SgGestureKind classify_gesture(SgGestureEngine &engine,
                               const SgGestureFrame &frame,
                               int64_t timestamp_ms) {
    int pinches = 0;
    int fists = 0;
    int palms = 0;
    for (uint32_t i = 0; i < frame.hand_count; ++i) {
        pinches += frame.hands[i].pinching;
        fists += frame.hands[i].fist;
        palms += frame.hands[i].open_palm;
    }

    if (explosive_transition(engine, frame, timestamp_ms)) {
        return SG_GESTURE_EXPLODE;
    }
    if (frame.hand_count == 2 && palms == 2) {
        if (engine.open_pair_since == 0) {
            engine.open_pair_since = timestamp_ms;
        }
        if (timestamp_ms - engine.open_pair_since > 1100 &&
            timestamp_ms - engine.last_reset_ms > 1800) {
            engine.last_reset_ms = timestamp_ms;
            return SG_GESTURE_RESET;
        }
    } else {
        engine.open_pair_since = 0;
    }
    if (pinches >= 2) {
        return SG_GESTURE_SCALE;
    }
    if (pinches == 1) {
        return SG_GESTURE_GRAB;
    }
    if (fists > 0) {
        return SG_GESTURE_FIST;
    }
    if (palms > 0) {
        return SG_GESTURE_OPEN_PALM;
    }
    return frame.hand_count > 0 ? SG_GESTURE_POINT : SG_GESTURE_NONE;
}

bool read_bool(std::istream &input, bool &value) {
    int parsed = 0;
    if (!(input >> parsed)) {
        return false;
    }
    value = parsed != 0;
    return true;
}

} // namespace

extern "C" {

SgGestureEngine *sg_gesture_engine_create(const SgGestureConfig *config,
                                           char *error,
                                           size_t error_size) {
    if (!config) {
        set_error(error, error_size, "Gesture configuration is missing");
        return nullptr;
    }

    auto engine = std::make_unique<SgGestureEngine>();
    engine->cpu_threads = config->cpu_threads ? config->cpu_threads : 4;
    CpuAffinityGuard affinity(engine->cpu_threads);
    engine->backend = std::make_unique<sg::MediaPipeBackend>(
        config->runtime_path,
        config->model_path,
        config->max_hands ? config->max_hands : 2,
        config->min_detection_confidence > 0.0f
            ? config->min_detection_confidence
            : 0.55f,
        config->min_presence_confidence > 0.0f
            ? config->min_presence_confidence
            : 0.50f,
        config->min_tracking_confidence > 0.0f
            ? config->min_tracking_confidence
            : 0.55f);
    if (!engine->backend->ready()) {
        set_error(error, error_size, engine->backend->error());
        return nullptr;
    }
    return engine.release();
}

void sg_gesture_engine_destroy(SgGestureEngine *engine) {
    delete engine;
}

bool sg_gesture_engine_process_rgb(SgGestureEngine *engine,
                                   const uint8_t *pixels,
                                   int width,
                                   int height,
                                   int stride,
                                   int64_t timestamp_ms,
                                   SgGestureFrame *frame,
                                   char *error,
                                   size_t error_size) {
    if (!engine || !frame) {
        set_error(error, error_size, "Gesture engine or output frame is missing");
        return false;
    }

    std::vector<sg::RawHand> raw_hands;
    CpuAffinityGuard affinity(engine->cpu_threads);
    if (!engine->backend->detect(pixels,
                                 width,
                                 height,
                                 stride,
                                 timestamp_ms,
                                 raw_hands)) {
        set_error(error, error_size, engine->backend->error());
        return false;
    }

    std::sort(raw_hands.begin(), raw_hands.end(), [](const auto &a, const auto &b) {
        return a.handedness < b.handedness;
    });
    std::memset(frame, 0, sizeof(*frame));
    frame->sequence = ++engine->sequence;
    frame->timestamp_ms = timestamp_ms;
    frame->hand_count = std::min<uint32_t>(raw_hands.size(), SG_GESTURE_MAX_HANDS);
    const float delta_seconds = engine->last_timestamp_ms > 0
        ? std::clamp((timestamp_ms - engine->last_timestamp_ms) / 1000.0f,
                     0.001f,
                     0.1f)
        : 1.0f / 30.0f;

    for (uint32_t i = 0; i < frame->hand_count; ++i) {
        const auto *previous = find_previous(engine->previous,
                                             raw_hands[i].handedness);
        sg::analyze_hand(raw_hands[i],
                         engine->calibration,
                         engine->mapping,
                         previous,
                         delta_seconds,
                         static_cast<float>(width) / std::max(height, 1),
                         frame->hands[i]);
    }
    frame->gesture = classify_gesture(*engine, *frame, timestamp_ms);
    frame->gesture_started = frame->gesture != engine->last_gesture ||
        frame->gesture == SG_GESTURE_EXPLODE ||
        frame->gesture == SG_GESTURE_RESET;
    frame->gesture_ended = engine->last_gesture != SG_GESTURE_NONE &&
        frame->gesture != engine->last_gesture;

    engine->previous = *frame;
    engine->last_gesture = frame->gesture;
    engine->last_timestamp_ms = timestamp_ms;
    return true;
}

void sg_gesture_engine_set_calibration(SgGestureEngine *engine,
                                       const SgGestureCalibration *calibration) {
    if (!engine || !calibration) {
        return;
    }
    engine->calibration = *calibration;
    engine->mapping = sg::solve_screen_mapping(engine->calibration);
    engine->previous = {};
    engine->last_timestamp_ms = 0;
}

void sg_gesture_engine_get_calibration(const SgGestureEngine *engine,
                                       SgGestureCalibration *calibration) {
    if (engine && calibration) {
        *calibration = engine->calibration;
    }
}

bool sg_gesture_calibration_save(const SgGestureCalibration *calibration,
                                 const char *path) {
    if (!calibration || !path) {
        return false;
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "SGCAL 3\n";
    output << calibration->has_screen_mapping << ' '
           << calibration->has_depth_range << ' '
           << calibration->has_pinch_range << '\n';
    output << calibration->screen_aspect << '\n';
    for (int i = 0; i < SG_GESTURE_SCREEN_POINT_COUNT; ++i) {
        output << calibration->camera_points[i][0] << ' '
               << calibration->camera_points[i][1] << ' '
               << calibration->screen_points[i][0] << ' '
               << calibration->screen_points[i][1] << '\n';
    }
    output << calibration->near_palm_span << ' '
           << calibration->far_palm_span << ' '
           << calibration->pinch_closed_ratio << ' '
           << calibration->pinch_open_ratio << '\n';
    return output.good();
}

bool sg_gesture_calibration_load(SgGestureCalibration *calibration,
                                 const char *path) {
    if (!calibration || !path) {
        return false;
    }
    std::ifstream input(path);
    std::string magic;
    int version = 0;
    if (!(input >> magic >> version) || magic != "SGCAL" || version != 3) {
        return false;
    }

    SgGestureCalibration parsed {};
    if (!read_bool(input, parsed.has_screen_mapping) ||
        !read_bool(input, parsed.has_depth_range) ||
        !read_bool(input, parsed.has_pinch_range)) {
        return false;
    }
    if (!(input >> parsed.screen_aspect)) {
        return false;
    }
    for (int i = 0; i < SG_GESTURE_SCREEN_POINT_COUNT; ++i) {
        if (!(input >> parsed.camera_points[i][0]
                    >> parsed.camera_points[i][1]
                    >> parsed.screen_points[i][0]
                    >> parsed.screen_points[i][1])) {
            return false;
        }
    }
    if (!(input >> parsed.near_palm_span
                >> parsed.far_palm_span
                >> parsed.pinch_closed_ratio
                >> parsed.pinch_open_ratio)) {
        return false;
    }
    *calibration = parsed;
    return true;
}

const char *sg_gesture_kind_name(SgGestureKind kind) {
    switch (kind) {
    case SG_GESTURE_POINT: return "Point";
    case SG_GESTURE_GRAB: return "Grab";
    case SG_GESTURE_SCALE: return "Scale";
    case SG_GESTURE_FIST: return "Fist";
    case SG_GESTURE_OPEN_PALM: return "Open palm";
    case SG_GESTURE_EXPLODE: return "Explode";
    case SG_GESTURE_RESET: return "Reset";
    case SG_GESTURE_NONE:
    default:
        return "No hands";
    }
}

} // extern "C"
