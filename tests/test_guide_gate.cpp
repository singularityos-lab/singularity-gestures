#include "control/guide_gate.hpp"

#include <cassert>

using sg::control::GuideGestureGate;
using sg::control::GuidePose;

int main() {
    GuideGestureGate gate;
    GuidePose pose;
    pose.left_present = true;
    pose.left_open_palm = true;
    pose.right_present = true;
    pose.right_pointing = true;
    pose.palm_x = 0.45f;
    pose.palm_y = 0.50f;
    pose.fingertip_x = 0.50f;
    pose.fingertip_y = 0.50f;
    pose.palm_span = 0.10f;

    auto intent = gate.update(pose, 1000);
    assert(intent.captured);
    assert(!intent.toggle);
    intent = gate.update(pose, 1500);
    assert(intent.toggle);
    assert(!gate.update(pose, 1600).toggle);

    pose.right_pointing = false;
    assert(!gate.update(pose, 1700).captured);
    pose.right_pointing = true;
    assert(!gate.update(pose, 2200).toggle);
    assert(gate.update(pose, 2700).toggle);

    gate.reset();
    pose.fingertip_x = 0.70f;
    intent = gate.update(pose, 3000);
    assert(!intent.captured);
    assert(!intent.toggle);

    return 0;
}
