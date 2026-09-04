#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace sg {

inline bool runtime_ready(const std::filesystem::path &directory) {
    static constexpr const char *assets[] {
        "libmediapipe.so",
        "hand_landmarker.task",
        "face_landmarker.task",
        "libonnxruntime.so",
        "mobileone_s0_gaze.onnx",
    };
    for (const char *asset : assets) {
        if (!std::filesystem::exists(directory / asset)) {
            return false;
        }
    }
    return true;
}

inline std::filesystem::path resolve_runtime_directory(
    const std::filesystem::path &source_directory,
    const std::filesystem::path &install_directory,
    const std::filesystem::path &executable,
    const char *override_directory) {
    if (override_directory && *override_directory) {
        return override_directory;
    }
    const std::filesystem::path source_runtime =
        source_directory / "runtime";
    if (runtime_ready(source_runtime)) {
        return source_runtime;
    }
    if (!executable.empty()) {
        const std::filesystem::path installed_runtime =
            executable.parent_path().parent_path() /
            "share/singularity/gestures/runtime";
        if (runtime_ready(installed_runtime)) {
            return installed_runtime;
        }
    }
    return install_directory / "runtime";
}

inline std::filesystem::path runtime_directory() {
    std::error_code error;
    const std::filesystem::path executable =
        std::filesystem::read_symlink("/proc/self/exe", error);
    return resolve_runtime_directory(
        GESTURE_SOURCE_DIR,
        GESTURE_INSTALL_DIR,
        error ? std::filesystem::path() : executable,
        std::getenv("SINGULARITY_GESTURE_RUNTIME"));
}

} // namespace sg
