#include "control/screenshot_gate.hpp"

#include <cassert>

using sg::control::ScreenshotGestureGate;
using sg::control::ScreenshotPose;

int main() {
    ScreenshotGestureGate gate;
    int64_t time = 1000;

    ScreenshotPose frame;
    frame.camera_frame = true;
    assert(!gate.update(frame, time, 3000, 1000).capture_fullscreen);
    auto intent = gate.update(frame, time + 420, 3000, 1000);
    assert(intent.capture_fullscreen);
    assert(!gate.update(frame, time + 900, 3000, 1000).capture_fullscreen);
    frame.camera_frame = false;
    gate.update(frame, time + 933, 3000, 1000);

    ScreenshotPose region;
    region.hands[0] = {true, true, 0.2f, 0.2f, 1.0f};
    region.hands[1] = {true, true, 0.8f, 0.7f, 1.0f};
    assert(!gate.update(region, time + 1000, 3000, 1000).region_active);
    intent = gate.update(region, time + 1150, 3000, 1000);
    assert(intent.region_active);
    region.hands[0].pinching = false;
    region.hands[0].pinch_strength = 0.0f;
    gate.update(region, time + 1183, 3000, 1000);
    gate.update(region, time + 1216, 3000, 1000);
    intent = gate.update(region, time + 1249, 3000, 1000);
    assert(intent.region_active);
    assert(!intent.capture_region);
    region.hands[1].pinch_strength = 0.50f;
    gate.update(region, time + 1282, 3000, 1000);
    intent = gate.update(region, time + 1315, 3000, 1000);
    assert(intent.capture_region);
    assert(intent.ax == 0.2f);
    assert(intent.by == 0.7f);

    gate.reset();
    region.hands[0] = {true, true, 0.5f, 0.5f, 1.0f};
    region.hands[1] = {true, true, 0.505f, 0.51f, 1.0f};
    gate.update(region, time + 2000, 3000, 1000);
    gate.update(region, time + 2150, 3000, 1000);
    region.hands[0].pinching = false;
    region.hands[0].pinch_strength = 0.0f;
    region.hands[1].pinching = false;
    region.hands[1].pinch_strength = 0.0f;
    gate.update(region, time + 2183, 3000, 1000);
    intent = gate.update(region, time + 2216, 3000, 1000);
    assert(!intent.capture_region);

    return 0;
}
