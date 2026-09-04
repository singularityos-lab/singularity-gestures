#include "core/gesture_math.hpp"

#include <cassert>
#include <cmath>

namespace {

bool close_to(float actual, float expected, float epsilon = 0.002f) {
    return std::abs(actual - expected) < epsilon;
}

sg::RawHand make_open_hand(float pinch_distance) {
    sg::RawHand hand;
    hand.confidence = 0.95f;
    hand.handedness = SG_HAND_RIGHT;
    hand.normalized[0] = {0.50f, 0.78f, 0.0f};
    hand.normalized[5] = {0.43f, 0.58f, 0.0f};
    hand.normalized[6] = {0.41f, 0.45f, 0.0f};
    hand.normalized[8] = {0.40f, 0.22f, 0.0f};
    hand.normalized[9] = {0.50f, 0.56f, 0.0f};
    hand.normalized[10] = {0.50f, 0.41f, 0.0f};
    hand.normalized[12] = {0.50f, 0.17f, 0.0f};
    hand.normalized[13] = {0.57f, 0.58f, 0.0f};
    hand.normalized[14] = {0.59f, 0.45f, 0.0f};
    hand.normalized[16] = {0.60f, 0.24f, 0.0f};
    hand.normalized[17] = {0.65f, 0.61f, 0.0f};
    hand.normalized[18] = {0.68f, 0.50f, 0.0f};
    hand.normalized[20] = {0.70f, 0.32f, 0.0f};
    hand.normalized[4] = {0.40f + pinch_distance, 0.22f, 0.0f};
    return hand;
}

void test_screen_mapping() {
    SgGestureCalibration calibration {};
    calibration.has_screen_mapping = true;
    constexpr float columns[5] {0.08f, 0.28f, 0.50f, 0.72f, 0.92f};
    constexpr float rows[3] {0.20f, 0.50f, 0.78f};
    int point = 0;
    for (float y : rows) {
        for (float x : columns) {
            calibration.camera_points[point][0] = x;
            calibration.camera_points[point][1] = y;
            calibration.screen_points[point][0] =
                0.02f + x * 0.94f + y * 0.03f + x * y * 0.05f + x * x * 0.02f;
            calibration.screen_points[point][1] =
                0.01f + y * 0.96f - x * 0.02f + x * y * 0.04f + y * y * 0.02f;
            ++point;
        }
    }

    const auto mapping = sg::solve_screen_mapping(calibration);
    assert(mapping.valid);
    const auto mapped = sg::map_screen(mapping, 0.63f, 0.37f);
    const float expected_x = 0.02f + 0.63f * 0.94f + 0.37f * 0.03f +
        0.63f * 0.37f * 0.05f + 0.63f * 0.63f * 0.02f;
    const float expected_y = 0.01f + 0.37f * 0.96f - 0.63f * 0.02f +
        0.63f * 0.37f * 0.04f + 0.37f * 0.37f * 0.02f;
    assert(close_to(mapped.x, expected_x));
    assert(close_to(mapped.y, expected_y));
}

void test_depth() {
    SgGestureCalibration calibration {};
    calibration.has_depth_range = true;
    calibration.near_palm_span = 0.34f;
    calibration.far_palm_span = 0.14f;
    assert(close_to(sg::estimate_depth(calibration, 0.34f), 0.0f));
    assert(close_to(sg::estimate_depth(calibration, 0.24f), 0.5f));
    assert(close_to(sg::estimate_depth(calibration, 0.14f), 1.0f));
}

void test_pinch_hysteresis() {
    SgGestureCalibration calibration {};
    const auto mapping = sg::ScreenMapping {};
    auto raw = make_open_hand(0.01f);
    SgGestureHand hand {};
    sg::analyze_hand(raw, calibration, mapping, nullptr, 1.0f / 60.0f, 16.0f / 9.0f, hand);
    assert(hand.pinching);

    raw = make_open_hand(0.14f);
    SgGestureHand released {};
    sg::analyze_hand(raw, calibration, mapping, &hand, 1.0f / 60.0f, 16.0f / 9.0f, released);
    assert(!released.pinching);
}

void test_adaptive_filter() {
    SgGestureCalibration calibration {};
    const auto mapping = sg::ScreenMapping {};
    auto raw = make_open_hand(0.08f);
    SgGestureHand previous {};
    sg::analyze_hand(raw, calibration, mapping, nullptr, 1.0f / 60.0f, 16.0f / 9.0f, previous);

    raw.normalized[8].x += 0.008f;
    SgGestureHand jittered {};
    sg::analyze_hand(raw, calibration, mapping, &previous, 1.0f / 60.0f, 16.0f / 9.0f, jittered);
    assert(jittered.landmarks[8].screen.x > previous.landmarks[8].screen.x);
    assert(jittered.landmarks[8].screen.x < raw.normalized[8].x);

    raw.normalized[8].x += 0.25f;
    SgGestureHand moving {};
    sg::analyze_hand(raw, calibration, mapping, &jittered, 1.0f / 60.0f, 16.0f / 9.0f, moving);
    assert(std::abs(moving.landmarks[8].screen.x - raw.normalized[8].x) < 0.01f);
}

void test_wide_screen_hand_scale() {
    SgGestureCalibration calibration {};
    calibration.has_screen_mapping = true;
    calibration.screen_aspect = 4.8f;
    constexpr float columns[5] {0.08f, 0.28f, 0.50f, 0.72f, 0.92f};
    constexpr float rows[3] {0.20f, 0.50f, 0.78f};
    int point = 0;
    for (float y : rows) {
        for (float x : columns) {
            calibration.camera_points[point][0] = x;
            calibration.camera_points[point][1] = y;
            calibration.screen_points[point][0] = x;
            calibration.screen_points[point][1] = y;
            ++point;
        }
    }
    const auto mapping = sg::solve_screen_mapping(calibration);
    const auto raw = make_open_hand(0.08f);
    SgGestureHand hand {};
    sg::analyze_hand(raw,
                     calibration,
                     mapping,
                     nullptr,
                     1.0f / 60.0f,
                     16.0f / 9.0f,
                     hand);
    const float hand_width = std::abs(hand.landmarks[20].screen.x -
                                      hand.landmarks[8].screen.x) * 5760.0f;
    assert(hand_width > 500.0f);
    assert(hand_width < 700.0f);
    assert(close_to(hand.landmarks[8].screen.x, raw.normalized[8].x));

    SgGestureHand calibrating {};
    sg::analyze_hand(raw,
                     calibration,
                     sg::ScreenMapping {},
                     nullptr,
                     1.0f / 60.0f,
                     16.0f / 9.0f,
                     calibrating);
    const float calibration_width = std::abs(calibrating.landmarks[20].screen.x -
                                             calibrating.landmarks[8].screen.x) * 5760.0f;
    assert(calibration_width > 500.0f);
    assert(calibration_width < 700.0f);
}

void test_wide_screen_pointer_gain() {
    SgGestureCalibration calibration {};
    calibration.has_screen_mapping = true;
    constexpr float columns[5] {0.08f, 0.28f, 0.50f, 0.72f, 0.92f};
    constexpr float rows[3] {0.20f, 0.50f, 0.78f};
    int point = 0;
    for (float y : rows) {
        for (float x : columns) {
            calibration.camera_points[point][0] = x;
            calibration.camera_points[point][1] = y;
            calibration.screen_points[point][0] = x;
            calibration.screen_points[point][1] = y;
            ++point;
        }
    }
    const auto mapping = sg::solve_screen_mapping(calibration);
    const auto raw = make_open_hand(0.08f);
    auto shifted = raw;
    for (auto &landmark : shifted.normalized) {
        landmark.x += 0.006f;
    }

    calibration.screen_aspect = 4.8f;
    SgGestureHand wide_start {};
    SgGestureHand wide_moved {};
    sg::analyze_hand(raw, calibration, mapping, nullptr,
                     1.0f / 30.0f, 16.0f / 9.0f, wide_start);
    sg::analyze_hand(shifted, calibration, mapping, &wide_start,
                     1.0f / 30.0f, 16.0f / 9.0f, wide_moved);
    const float wide_pixels = std::abs(
        wide_moved.landmarks[8].screen.x -
        wide_start.landmarks[8].screen.x) * 5760.0f;

    calibration.screen_aspect = 1.6f;
    SgGestureHand single_start {};
    SgGestureHand single_moved {};
    sg::analyze_hand(raw, calibration, mapping, nullptr,
                     1.0f / 30.0f, 16.0f / 9.0f, single_start);
    sg::analyze_hand(shifted, calibration, mapping, &single_start,
                     1.0f / 30.0f, 16.0f / 9.0f, single_moved);
    const float single_pixels = std::abs(
        single_moved.landmarks[8].screen.x -
        single_start.landmarks[8].screen.x) * 1920.0f;

    assert(wide_pixels > single_pixels * 0.80f);
    assert(wide_pixels < single_pixels * 1.20f);

    auto swept = raw;
    for (auto &landmark : swept.normalized) {
        landmark.x += 0.060f;
    }
    SgGestureHand wide_swept {};
    calibration.screen_aspect = 4.8f;
    sg::analyze_hand(swept, calibration, mapping, &wide_start,
                     1.0f / 30.0f, 16.0f / 9.0f, wide_swept);
    const float sweep_pixels = std::abs(
        wide_swept.landmarks[8].screen.x -
        wide_start.landmarks[8].screen.x) * 5760.0f;
    assert(sweep_pixels > 250.0f);
}

} // namespace

int main() {
    test_screen_mapping();
    test_depth();
    test_pinch_hysteresis();
    test_adaptive_filter();
    test_wide_screen_hand_scale();
    test_wide_screen_pointer_gain();
    return 0;
}
