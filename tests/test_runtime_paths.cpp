#include "core/runtime_paths.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

constexpr const char *assets[] {
    "libmediapipe.so",
    "hand_landmarker.task",
    "face_landmarker.task",
    "libonnxruntime.so",
    "mobileone_s0_gaze.onnx",
};

void create_runtime(const std::filesystem::path &directory) {
    std::filesystem::create_directories(directory);
    for (const char *asset : assets) {
        std::ofstream(directory / asset).put('\n');
    }
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("singularity-runtime-test-" + std::to_string(getpid()));
    const auto source = root / "source";
    const auto prefix = root / "prefix";
    const auto executable = prefix / "bin/singularity-hand-control";
    const auto installed = prefix / "share/singularity/gestures/runtime";
    const auto configured = root / "configured";
    const auto override = root / "override";

    create_runtime(installed);
    assert(sg::resolve_runtime_directory(
        source, configured, executable, nullptr) == installed);

    create_runtime(source / "runtime");
    assert(sg::resolve_runtime_directory(
        source, configured, executable, nullptr) == source / "runtime");

    std::filesystem::remove(source / "runtime/face_landmarker.task");
    assert(sg::resolve_runtime_directory(
        source, configured, executable, nullptr) == installed);
    assert(sg::resolve_runtime_directory(
        source, configured, executable, override.c_str()) == override);

    std::filesystem::remove_all(installed);
    assert(sg::resolve_runtime_directory(
        source, configured, executable, nullptr) == configured / "runtime");

    std::filesystem::remove_all(root);
    return 0;
}
