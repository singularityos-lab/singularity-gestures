#include "lab/lab.hpp"
#include "core/runtime_paths.hpp"

#include <string>

int main(int argc, char **argv) {
    bool demo = false;
    bool calibration_demo = false;
    bool presentation = false;
    bool gaze_calibration = false;
    bool eyes_only = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--demo") {
            demo = true;
        } else if (std::string(argv[i]) == "--calibration-demo") {
            demo = true;
            calibration_demo = true;
        } else if (std::string(argv[i]) == "--presentation") {
            presentation = true;
        } else if (std::string(argv[i]) == "--gaze-calibration") {
            gaze_calibration = true;
            presentation = true;
        } else if (std::string(argv[i]) == "--eyes-only") {
            eyes_only = true;
            presentation = true;
        }
    }

    sg::lab::Lab lab(sg::runtime_directory().parent_path().string(),
                     demo,
                     calibration_demo,
                     presentation,
                     gaze_calibration,
                     eyes_only);
    return lab.run();
}
