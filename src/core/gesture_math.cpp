#include "core/gesture_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sg {

namespace {

constexpr int kThumbTip = 4;
constexpr int kIndexMcp = 5;
constexpr int kIndexPip = 6;
constexpr int kIndexTip = 8;
constexpr int kMiddlePip = 10;
constexpr int kMiddleTip = 12;
constexpr int kRingPip = 14;
constexpr int kRingTip = 16;
constexpr int kPinkyMcp = 17;
constexpr int kPinkyPip = 18;
constexpr int kPinkyTip = 20;

bool solve_linear_6(double matrix[6][7], float out[6]) {
    for (int column = 0; column < 6; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 6; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(matrix[pivot][column]) < 1e-10) {
            return false;
        }
        if (pivot != column) {
            for (int i = column; i < 7; ++i) {
                std::swap(matrix[column][i], matrix[pivot][i]);
            }
        }

        const double divisor = matrix[column][column];
        for (int i = column; i < 7; ++i) {
            matrix[column][i] /= divisor;
        }
        for (int row = 0; row < 6; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = matrix[row][column];
            for (int i = column; i < 7; ++i) {
                matrix[row][i] -= factor * matrix[column][i];
            }
        }
    }

    for (int row = 0; row < 6; ++row) {
        out[row] = static_cast<float>(matrix[row][6]);
    }
    return true;
}

void mapping_basis(float x, float y, double out[6]) {
    out[0] = 1.0;
    out[1] = x;
    out[2] = y;
    out[3] = x * y;
    out[4] = x * x;
    out[5] = y * y;
}

bool fit_mapping_axis(const SgGestureCalibration &calibration,
                      int axis,
                      float out[6]) {
    double system[6][7] {};
    for (int point = 0; point < SG_GESTURE_SCREEN_POINT_COUNT; ++point) {
        double basis[6] {};
        mapping_basis(calibration.camera_points[point][0],
                      calibration.camera_points[point][1],
                      basis);
        for (int row = 0; row < 6; ++row) {
            for (int column = 0; column < 6; ++column) {
                system[row][column] += basis[row] * basis[column];
            }
            system[row][6] += basis[row] * calibration.screen_points[point][axis];
        }
    }
    for (int i = 0; i < 6; ++i) {
        system[i][i] += 1e-8;
    }
    return solve_linear_6(system, out);
}

float filter_alpha(float cutoff, float delta_seconds) {
    return 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * cutoff * delta_seconds);
}

float horizontal_pointer_gain(const RawLandmark &current,
                              const SgGesturePoint &previous,
                              float delta_seconds,
                              float image_aspect,
                              float screen_aspect) {
    const float base = std::min(image_aspect / screen_aspect, 1.0f);
    const float dx = (current.x - previous.camera.x) * image_aspect;
    const float dy = current.y - previous.camera.y;
    const float speed = std::hypot(dx, dy) /
        std::max(delta_seconds, 0.001f);
    const float boost = clamp((speed - 0.45f) / 1.20f, 0.0f, 1.0f);
    return base + (1.0f - base) * boost;
}

bool finger_extended(const RawHand &hand, int tip, int pip) {
    const auto &wrist = hand.normalized[0];
    const auto &tip_point = hand.normalized[tip];
    const auto &pip_point = hand.normalized[pip];
    return distance_2d(tip_point.x, tip_point.y, wrist.x, wrist.y) >
           distance_2d(pip_point.x, pip_point.y, wrist.x, wrist.y) * 1.12f;
}

} // namespace

float clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

float distance_2d(float ax, float ay, float bx, float by) {
    return std::hypot(ax - bx, ay - by);
}

float distance_3d(const SgVec3 &a, const SgVec3 &b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return static_cast<float>(std::sqrt(
        static_cast<double>(dx * dx + dy * dy + dz * dz)));
}

ScreenMapping solve_screen_mapping(const SgGestureCalibration &calibration) {
    ScreenMapping result;
    if (!calibration.has_screen_mapping) {
        return result;
    }
    if (!fit_mapping_axis(calibration, 0, result.values.data()) ||
        !fit_mapping_axis(calibration, 1, result.values.data() + 6)) {
        return result;
    }
    result.valid = true;
    return result;
}

SgVec3 map_screen(const ScreenMapping &mapping, float x, float y) {
    if (!mapping.valid) {
        return {x, y, 0.0f};
    }
    double basis[6] {};
    mapping_basis(x, y, basis);
    SgVec3 result {};
    for (int i = 0; i < 6; ++i) {
        result.x += mapping.values[i] * static_cast<float>(basis[i]);
        result.y += mapping.values[i + 6] * static_cast<float>(basis[i]);
    }
    return result;
}

float estimate_depth(const SgGestureCalibration &calibration, float palm_span) {
    const float near_span = calibration.has_depth_range
        ? calibration.near_palm_span
        : 0.32f;
    const float far_span = calibration.has_depth_range
        ? calibration.far_palm_span
        : 0.12f;
    const float range = std::max(near_span - far_span, 0.02f);
    return clamp((near_span - palm_span) / range, 0.0f, 1.0f);
}

float pinch_strength(const SgGestureCalibration &calibration,
                     float pinch_ratio) {
    const float closed = calibration.has_pinch_range
        ? calibration.pinch_closed_ratio
        : 0.20f;
    const float open = calibration.has_pinch_range
        ? calibration.pinch_open_ratio
        : 0.58f;
    const float t = clamp((open - pinch_ratio) / std::max(open - closed, 0.08f),
                          0.0f,
                          1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void analyze_hand(const RawHand &raw,
                  const SgGestureCalibration &calibration,
                  const ScreenMapping &mapping,
                  const SgGestureHand *previous,
                  float delta_seconds,
                  float image_aspect,
                  SgGestureHand &hand) {
    std::memset(&hand, 0, sizeof(hand));
    hand.present = true;
    hand.handedness = raw.handedness;
    hand.confidence = raw.confidence;
    hand.palm_span = distance_2d(raw.normalized[kIndexMcp].x,
                                 raw.normalized[kIndexMcp].y,
                                 raw.normalized[kPinkyMcp].x,
                                 raw.normalized[kPinkyMcp].y);
    const float depth = estimate_depth(calibration, hand.palm_span);
    const auto &pointer = raw.normalized[kIndexTip];
    auto mapped_pointer = map_screen(mapping, pointer.x, pointer.y);
    const float screen_aspect = calibration.screen_aspect > 0.1f
        ? calibration.screen_aspect
        : image_aspect;
    const bool compact_projection = mapping.valid ||
        screen_aspect > image_aspect * 1.25f;
    if (previous && previous->present &&
        screen_aspect > image_aspect * 1.25f) {
        const auto &old_pointer = previous->landmarks[kIndexTip];
        const auto previous_target = map_screen(
            mapping, old_pointer.camera.x, old_pointer.camera.y);
        const float gain = horizontal_pointer_gain(
            pointer, old_pointer, delta_seconds, image_aspect, screen_aspect);
        mapped_pointer.x = old_pointer.screen.x +
            (mapped_pointer.x - previous_target.x) * gain;
    }

    for (int i = 0; i < SG_GESTURE_LANDMARK_COUNT; ++i) {
        const auto &source = raw.normalized[i];
        auto measured = map_screen(mapping, source.x, source.y);
        if (compact_projection) {
            measured.x = mapped_pointer.x +
                (source.x - pointer.x) * image_aspect / screen_aspect;
            measured.y = mapped_pointer.y + source.y - pointer.y;
        }
        measured.x = clamp(measured.x, -0.25f, 1.25f);
        measured.y = clamp(measured.y, -0.25f, 1.25f);
        measured.z = clamp(depth + source.z * 1.6f, -0.25f, 1.25f);

        auto &point = hand.landmarks[i];
        point.camera = {source.x, source.y, source.z};
        point.screen = measured;
        point.world = {
            raw.world[i].x,
            -raw.world[i].y,
            -raw.world[i].z,
        };
        point.confidence = source.confidence;
        if (previous && previous->present && delta_seconds > 0.0001f) {
            const auto &old = previous->landmarks[i].screen;
            const float speed = distance_2d(measured.x,
                                            measured.y,
                                            old.x,
                                            old.y) / delta_seconds;
            const float xy_alpha = filter_alpha(2.2f + speed * 7.0f,
                                                delta_seconds);
            const float z_speed = std::abs(measured.z - old.z) / delta_seconds;
            const float z_alpha = filter_alpha(1.6f + z_speed * 4.0f,
                                               delta_seconds);
            point.screen = {
                old.x + (measured.x - old.x) * xy_alpha,
                old.y + (measured.y - old.y) * xy_alpha,
                old.z + (measured.z - old.z) * z_alpha,
            };
            point.velocity = {
                (point.screen.x - old.x) / delta_seconds,
                (point.screen.y - old.y) / delta_seconds,
                (point.screen.z - old.z) / delta_seconds,
            };
        }
    }

    const float pinch_distance = distance_2d(raw.normalized[kThumbTip].x,
                                              raw.normalized[kThumbTip].y,
                                              raw.normalized[kIndexTip].x,
                                              raw.normalized[kIndexTip].y);
    const float ratio = pinch_distance / std::max(hand.palm_span, 0.025f);
    const float measured_pinch = pinch_strength(calibration, ratio);
    hand.pinch_strength = previous && previous->present
        ? previous->pinch_strength + (measured_pinch - previous->pinch_strength) * 0.68f
        : measured_pinch;
    const bool was_pinching = previous && previous->pinching;
    hand.pinching = was_pinching
        ? hand.pinch_strength > 0.42f
        : hand.pinch_strength > 0.72f;

    int extended = 0;
    extended += finger_extended(raw, kIndexTip, kIndexPip);
    extended += finger_extended(raw, kMiddleTip, kMiddlePip);
    extended += finger_extended(raw, kRingTip, kRingPip);
    extended += finger_extended(raw, kPinkyTip, kPinkyPip);
    hand.open_palm = extended == 4 && hand.pinch_strength < 0.35f;
    hand.fist = extended == 0 && hand.pinch_strength < 0.65f;
}

} // namespace sg
