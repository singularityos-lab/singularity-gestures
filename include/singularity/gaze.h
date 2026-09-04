#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <singularity/gesture.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SG_GAZE_FEATURE_COUNT 10
#define SG_GAZE_COEFFICIENT_COUNT 11
#define SG_GAZE_DISPLAY_FEATURE_COUNT 4
#define SG_GAZE_DISPLAY_COEFFICIENT_COUNT 5
#define SG_GAZE_MAX_DISPLAYS 8
#define SG_GAZE_MAX_CALIBRATION_POINTS 72

typedef struct {
    uint64_t sequence;
    int64_t timestamp_ms;
    bool present;
    bool calibrated;
    float confidence;
    SgVec3 screen;
    SgVec3 left_iris;
    SgVec3 right_iris;
    float left_eye_openness;
    float right_eye_openness;
    float mouth_openness;
    float gaze_yaw;
    float gaze_pitch;
    float model_confidence;
    bool model_active;
    bool left_eye_closed;
    bool right_eye_closed;
    float features[SG_GAZE_FEATURE_COUNT];
} SgGazeFrame;

typedef struct {
    uint32_t point_count;
    float features[SG_GAZE_MAX_CALIBRATION_POINTS][SG_GAZE_FEATURE_COUNT];
    float screen_points[SG_GAZE_MAX_CALIBRATION_POINTS][2];
    float feature_mean[SG_GAZE_FEATURE_COUNT];
    float feature_scale[SG_GAZE_FEATURE_COUNT];
    float coefficients_x[SG_GAZE_COEFFICIENT_COUNT];
    float coefficients_y[SG_GAZE_COEFFICIENT_COUNT];
    uint32_t display_count;
    float display_bounds[SG_GAZE_MAX_DISPLAYS][4];
    float display_feature_center[SG_GAZE_MAX_DISPLAYS][SG_GAZE_DISPLAY_FEATURE_COUNT];
    float display_feature_mean[SG_GAZE_MAX_DISPLAYS][SG_GAZE_DISPLAY_FEATURE_COUNT];
    float display_feature_scale[SG_GAZE_MAX_DISPLAYS][SG_GAZE_DISPLAY_FEATURE_COUNT];
    float display_coefficients_x[SG_GAZE_MAX_DISPLAYS][SG_GAZE_DISPLAY_COEFFICIENT_COUNT];
    float display_coefficients_y[SG_GAZE_MAX_DISPLAYS][SG_GAZE_DISPLAY_COEFFICIENT_COUNT];
    float left_open_reference;
    float right_open_reference;
    float screen_aspect;
    float mean_error;
    bool valid;
} SgGazeCalibration;

typedef struct {
    const char *runtime_path;
    const char *model_path;
    const char *gaze_runtime_path;
    const char *gaze_model_path;
    uint32_t cpu_threads;
    float min_detection_confidence;
    float min_presence_confidence;
    float min_tracking_confidence;
} SgGazeConfig;

typedef struct SgGazeEngine SgGazeEngine;

SgGazeEngine *sg_gaze_engine_create(const SgGazeConfig *config,
                                    char *error,
                                    size_t error_size);
void sg_gaze_engine_destroy(SgGazeEngine *engine);

bool sg_gaze_engine_process_rgb(SgGazeEngine *engine,
                                const uint8_t *pixels,
                                int width,
                                int height,
                                int stride,
                                int64_t timestamp_ms,
                                SgGazeFrame *frame,
                                char *error,
                                size_t error_size);

void sg_gaze_engine_set_calibration(SgGazeEngine *engine,
                                    const SgGazeCalibration *calibration);
void sg_gaze_engine_get_calibration(const SgGazeEngine *engine,
                                    SgGazeCalibration *calibration);
bool sg_gaze_calibration_fit(SgGazeCalibration *calibration);
bool sg_gaze_calibration_predict(const SgGazeCalibration *calibration,
                                 const float features[SG_GAZE_FEATURE_COUNT],
                                 SgVec3 *screen);
bool sg_gaze_calibration_save(const SgGazeCalibration *calibration,
                              const char *path);
bool sg_gaze_calibration_load(SgGazeCalibration *calibration,
                              const char *path);

#ifdef __cplusplus
}
#endif
