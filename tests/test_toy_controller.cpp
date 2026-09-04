#include "control/toy_controller.hpp"

#include <cassert>

using sg::control::ToyController;
using sg::control::ToyFrame;

int main() {
    ToyController toy;
    ToyFrame frame;
    frame.paused = true;
    frame.hands[0] = {true, 0.9f, 0.20f, 0.30f};

    toy.update(frame, 1000, 1000, 800);
    assert(toy.state().active);
    assert(toy.state().held);
    assert(toy.state().radius == 28.0f);

    frame.hands[0].x = 0.35f;
    toy.update(frame, 1033, 1000, 800);
    assert(toy.state().x == 0.35f);

    frame.hands[0].pinch_strength = 0.2f;
    toy.update(frame, 1066, 1000, 800);
    assert(!toy.state().held);
    assert(toy.state().x > 0.35f);

    frame.hands[0] = {true, 0.9f, 0.90f, 0.30f};
    toy.update(frame, 1099, 1000, 800);
    assert(!toy.state().held);

    frame.hands[0] = {true, 0.9f, 0.40f, 0.40f};
    frame.hands[1] = {true, 0.9f, 0.60f, 0.40f};
    toy.update(frame, 1132, 1000, 800);
    const float initial_radius = toy.state().radius;
    assert(toy.state().stretching);

    frame.hands[0].x = 0.20f;
    frame.hands[1].x = 0.80f;
    toy.update(frame, 1165, 1000, 800);
    assert(toy.state().radius > initial_radius);
    assert(toy.state().string_ax == 0.20f);
    assert(toy.state().string_bx == 0.80f);

    frame.hands[0].x = 0.10f;
    frame.hands[1].pinch_strength = 0.2f;
    toy.update(frame, 1198, 1000, 800);
    assert(!toy.state().stretching);
    assert(!toy.state().held);

    frame.hands[0].x = 0.90f;
    toy.update(frame, 1231, 1000, 800);
    assert(!toy.state().held);

    frame.hands[0].pinch_strength = 0.2f;
    toy.update(frame, 1264, 1000, 800);
    frame.hands[0] = {
        true, 0.9f, toy.state().x, toy.state().y,
    };
    toy.update(frame, 1297, 1000, 800);
    assert(toy.state().held);

    frame.paused = false;
    toy.update(frame, 1330, 1000, 800);
    assert(!toy.state().active);

    return 0;
}
