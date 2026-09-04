#include "control/lock_gate.hpp"

#include <cassert>

using sg::control::LockGestureGate;
using sg::control::LockPose;

namespace {

LockPose two_fists(float distance) {
    LockPose pose;
    pose.hands[0] = {true, true, 0.40f, 0.50f, 0.40f, 0.50f, 0.10f};
    pose.hands[1] = {
        true, true, 0.40f + distance, 0.50f, 0.55f, 0.50f, 0.10f,
    };
    return pose;
}

} // namespace

int main() {
    LockGestureGate gate;
    auto pose = two_fists(0.18f);
    auto intent = gate.update(pose, 1000);
    assert(intent.captured);
    assert(intent.active);
    assert(!intent.lock_screen);
    intent = gate.update(pose, 1600);
    assert(intent.lock_screen);
    assert(intent.progress == 1.0f);
    assert(!gate.update(pose, 1700).lock_screen);

    gate.reset();
    pose = two_fists(0.30f);
    intent = gate.update(pose, 2000);
    assert(intent.captured);
    assert(intent.progress == 0.0f);
    assert(!gate.update(pose, 3000).lock_screen);

    gate.reset();
    pose = two_fists(0.18f);
    pose.hands[1].fist = false;
    intent = gate.update(pose, 4000);
    assert(!intent.captured);
    assert(!intent.lock_screen);

    return 0;
}
