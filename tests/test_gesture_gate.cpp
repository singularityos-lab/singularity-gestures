#include "control/gesture_gate.hpp"

#include <cassert>

using sg::control::GestureGate;
using sg::control::GesturePose;
using sg::control::DesktopGestureGate;
using sg::control::DesktopGesturePose;

namespace {

GesturePose point_pose() {
    GesturePose pose;
    pose.index = true;
    pose.pointer_x = 0.4f;
    pose.pointer_y = 0.4f;
    return pose;
}

void update_frames(GestureGate &gate,
                   const GesturePose &pose,
                   int count,
                   int64_t &time) {
    for (int i = 0; i < count; ++i) {
        gate.update(pose, time);
        time += 33;
    }
}

void update_frames(DesktopGestureGate &gate,
                   const DesktopGesturePose &pose,
                   int count,
                   int64_t &time) {
    for (int i = 0; i < count; ++i) {
        gate.update(pose, time, 3000.0f, 1000.0f);
        time += 33;
    }
}

} // namespace

int main() {
    int64_t time = 1000;
    GestureGate gate;

    GesturePose pinch;
    pinch.pinching = true;
    update_frames(gate, pinch, 12, time);
    assert(!gate.update(pinch, time).left_down);

    gate.reset();
    GesturePose point = point_pose();
    update_frames(gate, point, 5, time);
    pinch = point;
    pinch.pinching = true;
    update_frames(gate, pinch, 2, time);
    pinch.pinching = false;
    const auto released_pinch = gate.update(pinch, time);
    assert(!released_pinch.left_click);
    assert(!released_pinch.left_down);
    assert(!released_pinch.left_up);

    gate.reset();
    point.palm_x = 0.4f;
    point.palm_y = 0.5f;
    update_frames(gate, point, 5, time);
    GesturePose tap;
    tap.palm_x = point.palm_x;
    tap.palm_y = point.palm_y;
    update_frames(gate, tap, 2, time);
    const auto click = gate.update(point, time);
    assert(click.left_click);
    assert(!click.left_down);
    assert(!click.left_up);

    gate.reset();
    update_frames(gate, point, 5, time);
    update_frames(gate, tap, 1, time);
    assert(!gate.update(point, time).left_click);

    gate.reset();
    update_frames(gate, point, 5, time);
    update_frames(gate, tap, 2, time);
    tap.palm_x += 0.08f;
    gate.update(tap, time);
    assert(!gate.update(point, time).left_click);
    tap.palm_x = point.palm_x;

    gate.reset();
    update_frames(gate, point, 5, time);
    GesturePose secondary = point;
    secondary.middle_pinching = true;
    update_frames(gate, secondary, 4, time);
    assert(gate.update(secondary, time).secondary_click);

    gate.reset();
    update_frames(gate, point, 5, time);
    pinch = point;
    pinch.pinching = true;
    update_frames(gate, pinch, 2, time);
    pinch.pointer_x += 0.02f;
    assert(gate.update(pinch, time).left_down);
    time += 33;
    pinch.pinching = false;
    assert(!gate.update(pinch, time).left_up);
    time += 33;
    assert(!gate.update(pinch, time).left_up);
    time += 33;
    assert(gate.update(pinch, time).left_up);

    gate.reset();
    update_frames(gate, point, 5, time);
    pinch = point;
    pinch.pinching = true;
    update_frames(gate, pinch, 1, time);
    pinch.pinching = false;
    const auto rejected_click = gate.update(pinch, time);
    assert(!rejected_click.left_click);
    assert(!rejected_click.left_down);
    assert(!rejected_click.left_up);

    gate.reset();
    update_frames(gate, point, 5, time);
    pinch = point;
    pinch.pinching = true;
    update_frames(gate, pinch, 2, time);
    pinch.pointer_x += 0.02f;
    assert(gate.update(pinch, time).left_down);
    time += 33;
    GesturePose open_during_drag;
    open_during_drag.open_palm = true;
    assert(gate.update(open_during_drag, time).left_up);

    gate.reset();
    GesturePose scroll;
    scroll.index = true;
    scroll.middle = true;
    scroll.pointer_x = 0.5f;
    scroll.pointer_y = 0.5f;
    update_frames(gate, scroll, 7, time);
    scroll.pointer_x += 0.003f;
    assert(gate.update(scroll, time).scroll_x == 0.0f);
    time += 33;
    scroll.pointer_x += 0.020f;
    assert(gate.update(scroll, time).scroll_x != 0.0f);

    gate.reset();
    GesturePose palm;
    palm.open_palm = true;
    palm.palm_x = 0.60f;
    palm.palm_y = 0.45f;
    update_frames(gate, palm, 4, time);
    palm.palm_x = 0.47f;
    palm.palm_velocity_x = -0.9f;
    assert(gate.update(palm, time).switch_next);
    time += 33;
    palm.palm_x = 0.30f;
    assert(!gate.update(palm, time).switch_next);

    palm.open_palm = false;
    update_frames(gate, palm, 4, time);
    time += 1000;
    palm.open_palm = true;
    palm.palm_x = 0.40f;
    palm.palm_velocity_x = 0.0f;
    update_frames(gate, palm, 4, time);
    palm.palm_x = 0.54f;
    palm.palm_velocity_x = 0.9f;
    assert(gate.update(palm, time).switch_previous);

    DesktopGestureGate desktop_gate;
    DesktopGesturePose joined;
    joined.two_fingers_joined = true;
    joined.x = 0.65f;
    joined.y = 0.50f;
    update_frames(desktop_gate, joined, 5, time);
    joined.x = 0.25f;
    auto desktop_intent = desktop_gate.update(
        joined, time, 3000.0f, 1000.0f);
    assert(desktop_intent.begin);
    assert(desktop_intent.update);
    assert(desktop_intent.fingers == 3);
    assert(desktop_intent.direction == 1);
    DesktopGesturePose neutral;
    update_frames(desktop_gate, neutral, 3, time);
    desktop_intent = desktop_gate.update(neutral, time, 3000.0f, 1000.0f);
    assert(desktop_intent.end);
    assert(desktop_intent.committed);

    DesktopGestureGate wide_tiling_gate;
    joined.x = 0.50f;
    joined.y = 0.50f;
    for (int i = 0; i < 5; ++i) {
        wide_tiling_gate.update(joined, time, 5760.0f, 1200.0f);
        time += 33;
    }
    joined.x = 0.46f;
    desktop_intent = wide_tiling_gate.update(
        joined, time, 5760.0f, 1200.0f);
    assert(desktop_intent.begin);
    assert(desktop_intent.progress > 0.50f);
    assert(desktop_intent.dx < -350.0f);
    joined.x = 0.42f;
    desktop_intent = wide_tiling_gate.update(
        joined, time, 5760.0f, 1200.0f);
    assert(desktop_intent.progress == 1.0f);
    for (int i = 0; i < 3; ++i) {
        wide_tiling_gate.update(neutral, time, 5760.0f, 1200.0f);
        time += 33;
    }
    desktop_intent = wide_tiling_gate.update(
        neutral, time, 5760.0f, 1200.0f);
    assert(desktop_intent.end);
    assert(desktop_intent.committed);

    desktop_gate.cancel();
    DesktopGesturePose three_joined;
    three_joined.three_fingers_joined = true;
    three_joined.x = 0.35f;
    three_joined.y = 0.50f;
    update_frames(desktop_gate, three_joined, 5, time);
    three_joined.x = 0.39f;
    desktop_intent = desktop_gate.update(
        three_joined, time, 3000.0f, 1000.0f);
    assert(desktop_intent.begin);
    assert(desktop_intent.fingers == 4);
    assert(desktop_intent.direction == 2);
    desktop_intent = desktop_gate.cancel();
    assert(desktop_intent.end);
    assert(desktop_intent.cancelled);

    DesktopGesturePose fist;
    fist.fist = true;
    fist.openness = 0.08f;
    update_frames(desktop_gate, fist, 5, time);
    fist.fist = false;
    fist.openness = 0.55f;
    desktop_intent = desktop_gate.update(
        fist, time, 3000.0f, 1000.0f);
    assert(desktop_intent.begin);
    assert(desktop_intent.direction == 4);
    fist.openness = 1.0f;
    update_frames(desktop_gate, fist, 3, time);
    desktop_intent = desktop_gate.update(fist, time, 3000.0f, 1000.0f);
    assert(desktop_intent.end);
    assert(desktop_intent.committed);

    return 0;
}
