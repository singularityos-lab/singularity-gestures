#pragma once

#include <array>
#include <cstdint>

#include <singularity/gesture.h>

namespace sg {

struct RawLandmark {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float confidence = 1.0f;
};

struct RawHand {
    std::array<RawLandmark, SG_GESTURE_LANDMARK_COUNT> normalized;
    std::array<RawLandmark, SG_GESTURE_LANDMARK_COUNT> world;
    SgHandedness handedness = SG_HAND_UNKNOWN;
    float confidence = 0.0f;
};

struct ScreenMapping {
    std::array<float, 12> values {};
    bool valid = false;
};

float clamp(float value, float low, float high);
float distance_2d(float ax, float ay, float bx, float by);
float distance_3d(const SgVec3 &a, const SgVec3 &b);
ScreenMapping solve_screen_mapping(const SgGestureCalibration &calibration);
SgVec3 map_screen(const ScreenMapping &mapping, float x, float y);
float estimate_depth(const SgGestureCalibration &calibration, float palm_span);
float pinch_strength(const SgGestureCalibration &calibration,
                     float pinch_ratio);
void analyze_hand(const RawHand &raw,
                  const SgGestureCalibration &calibration,
                  const ScreenMapping &mapping,
                  const SgGestureHand *previous,
                  float delta_seconds,
                  float image_aspect,
                  SgGestureHand &hand);

} // namespace sg
