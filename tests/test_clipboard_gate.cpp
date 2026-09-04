#include "control/clipboard_gate.hpp"

#include <cassert>

using sg::control::ClipboardGestureGate;
using sg::control::ClipboardPose;

namespace {

ClipboardPose preload_pose() {
    ClipboardPose pose;
    pose.left_present = true;
    pose.left_open_palm = true;
    pose.right_present = true;
    pose.right_pinching = true;
    pose.palm_span = 0.10f;
    pose.anchor_camera_x = 0.40f;
    pose.anchor_camera_y = 0.50f;
    pose.pinch_camera_x = 0.45f;
    pose.pinch_camera_y = 0.50f;
    pose.anchor_screen_x = 0.35f;
    pose.anchor_screen_y = 0.55f;
    pose.pinch_screen_x = 0.40f;
    pose.pinch_screen_y = 0.55f;
    return pose;
}

void prime(ClipboardGestureGate &gate,
           const ClipboardPose &pose,
           int64_t &time) {
    for (int i = 0; i < 4; ++i) {
        gate.update(pose, time);
        time += 33;
    }
}

} // namespace

int main() {
    ClipboardGestureGate gate;
    int64_t time = 1000;
    auto pose = preload_pose();
    prime(gate, pose, time);
    pose.pinch_camera_x = 0.60f;
    pose.pinch_screen_x = 0.65f;
    auto intent = gate.update(pose, time);
    assert(intent.active);
    assert(intent.progress == 1.0f);
    pose.right_pinching = false;
    gate.update(pose, time + 33);
    gate.update(pose, time + 66);
    intent = gate.update(pose, time + 99);
    assert(intent.captured);
    assert(intent.paste);
    assert(!intent.active);

    gate.reset();
    pose = preload_pose();
    pose.left_open_palm = false;
    prime(gate, pose, time);
    assert(!gate.update(pose, time).captured);

    gate.reset();
    pose = preload_pose();
    prime(gate, pose, time);
    pose.right_pinching = false;
    gate.update(pose, time + 33);
    gate.update(pose, time + 66);
    intent = gate.update(pose, time + 99);
    assert(!intent.paste);

    return 0;
}
