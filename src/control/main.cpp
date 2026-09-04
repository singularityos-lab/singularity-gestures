#include "control/overlay.hpp"
#include "core/runtime_paths.hpp"

#include <string>

int main(int argc, char **argv) {
    sg::control::OverlayApplication application(
        sg::runtime_directory().string());
    return application.run(argc, argv);
}
