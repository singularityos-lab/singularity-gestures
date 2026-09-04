#include <singularity/gesture.h>
#include <singularity/gaze.h>

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>

int main(int argc, char **argv) {
    assert(argc == 6);
    SgGestureConfig config {};
    config.runtime_path = argv[1];
    config.model_path = argv[2];
    config.max_hands = 2;
    config.cpu_threads = 2;
    config.min_detection_confidence = 0.55f;
    config.min_presence_confidence = 0.50f;
    config.min_tracking_confidence = 0.55f;

    char error[512] {};
    SgGestureEngine *engine = sg_gesture_engine_create(&config,
                                                        error,
                                                        sizeof(error));
    assert(engine != nullptr);
    std::vector<uint8_t> pixels(320 * 240 * 3, 0);
    SgGestureFrame frame {};
    const bool ok = sg_gesture_engine_process_rgb(engine,
                                                   pixels.data(),
                                                   320,
                                                   240,
                                                   320 * 3,
                                                   1,
                                                   &frame,
                                                   error,
                                                   sizeof(error));
    if (!ok) {
        std::fprintf(stderr, "%s\n", error);
    }
    assert(ok);
    assert(frame.hand_count == 0);
    sg_gesture_engine_destroy(engine);

    SgGazeConfig gaze_config {};
    gaze_config.runtime_path = argv[1];
    gaze_config.model_path = argv[3];
    gaze_config.gaze_runtime_path = argv[4];
    gaze_config.gaze_model_path = argv[5];
    gaze_config.cpu_threads = 2;
    gaze_config.min_detection_confidence = 0.50f;
    gaze_config.min_presence_confidence = 0.50f;
    gaze_config.min_tracking_confidence = 0.50f;
    SgGazeEngine *gaze = sg_gaze_engine_create(&gaze_config,
                                                error,
                                                sizeof(error));
    assert(gaze != nullptr);
    SgGazeFrame gaze_frame {};
    const bool gaze_ok = sg_gaze_engine_process_rgb(gaze,
                                                     pixels.data(),
                                                     320,
                                                     240,
                                                     320 * 3,
                                                     2,
                                                     &gaze_frame,
                                                     error,
                                                     sizeof(error));
    if (!gaze_ok) {
        std::fprintf(stderr, "%s\n", error);
    }
    assert(gaze_ok);
    assert(!gaze_frame.present);
    sg_gaze_engine_destroy(gaze);
    return 0;
}
