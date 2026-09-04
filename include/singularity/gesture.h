#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SG_GESTURE_MAX_HANDS 2
#define SG_GESTURE_LANDMARK_COUNT 21
#define SG_GESTURE_SCREEN_POINT_COUNT 15

typedef enum {
    SG_HAND_UNKNOWN = 0,
    SG_HAND_LEFT,
    SG_HAND_RIGHT,
} SgHandedness;

typedef enum {
    SG_GESTURE_NONE = 0,
    SG_GESTURE_POINT,
    SG_GESTURE_GRAB,
    SG_GESTURE_SCALE,
    SG_GESTURE_FIST,
    SG_GESTURE_OPEN_PALM,
    SG_GESTURE_EXPLODE,
    SG_GESTURE_RESET,
} SgGestureKind;

typedef struct {
    float x;
    float y;
    float z;
} SgVec3;

typedef struct {
    SgVec3 camera;
    SgVec3 screen;
    SgVec3 world;
    SgVec3 velocity;
    float confidence;
} SgGesturePoint;

typedef struct {
    bool present;
    SgHandedness handedness;
    float confidence;
    float pinch_strength;
    float palm_span;
    bool pinching;
    bool fist;
    bool open_palm;
    SgGesturePoint landmarks[SG_GESTURE_LANDMARK_COUNT];
} SgGestureHand;

typedef struct {
    uint64_t sequence;
    int64_t timestamp_ms;
    uint32_t hand_count;
    SgGestureKind gesture;
    bool gesture_started;
    bool gesture_ended;
    SgGestureHand hands[SG_GESTURE_MAX_HANDS];
} SgGestureFrame;

typedef struct {
    float camera_points[SG_GESTURE_SCREEN_POINT_COUNT][2];
    float screen_points[SG_GESTURE_SCREEN_POINT_COUNT][2];
    float screen_aspect;
    float near_palm_span;
    float far_palm_span;
    float pinch_closed_ratio;
    float pinch_open_ratio;
    bool has_screen_mapping;
    bool has_depth_range;
    bool has_pinch_range;
} SgGestureCalibration;

typedef struct {
    const char *runtime_path;
    const char *model_path;
    uint32_t max_hands;
    uint32_t cpu_threads;
    float min_detection_confidence;
    float min_presence_confidence;
    float min_tracking_confidence;
} SgGestureConfig;

typedef struct SgGestureEngine SgGestureEngine;

SgGestureEngine *sg_gesture_engine_create(const SgGestureConfig *config,
                                           char *error,
                                           size_t error_size);
void sg_gesture_engine_destroy(SgGestureEngine *engine);

bool sg_gesture_engine_process_rgb(SgGestureEngine *engine,
                                   const uint8_t *pixels,
                                   int width,
                                   int height,
                                   int stride,
                                   int64_t timestamp_ms,
                                   SgGestureFrame *frame,
                                   char *error,
                                   size_t error_size);

void sg_gesture_engine_set_calibration(SgGestureEngine *engine,
                                       const SgGestureCalibration *calibration);
void sg_gesture_engine_get_calibration(const SgGestureEngine *engine,
                                       SgGestureCalibration *calibration);
bool sg_gesture_calibration_save(const SgGestureCalibration *calibration,
                                 const char *path);
bool sg_gesture_calibration_load(SgGestureCalibration *calibration,
                                 const char *path);

const char *sg_gesture_kind_name(SgGestureKind kind);

#ifdef __cplusplus
}
#endif
