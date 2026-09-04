#include <singularity/gaze.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/face_backend.hpp"
#include "core/gaze_model.hpp"

struct SgGazeEngine {
    std::unique_ptr<sg::FaceBackend> backend;
    std::unique_ptr<sg::GazeModel> model;
    SgGazeCalibration calibration {};
    SgGazeFrame previous {};
    std::array<SgVec3, 5> recent_screen {};
    size_t recent_screen_count = 0;
    size_t recent_screen_next = 0;
    int active_display = -1;
    int64_t last_timestamp_ms = 0;
    uint64_t sequence = 0;
};

namespace {

constexpr int kRightEyeOuter = 33;
constexpr int kRightEyeInner = 133;
constexpr int kRightEyeUpper = 159;
constexpr int kRightEyeLower = 145;
constexpr int kRightIris = 468;
constexpr int kLeftEyeInner = 362;
constexpr int kLeftEyeOuter = 263;
constexpr int kLeftEyeUpper = 386;
constexpr int kLeftEyeLower = 374;
constexpr int kLeftIris = 473;
constexpr int kNose = 1;
constexpr int kUpperLip = 13;
constexpr int kLowerLip = 14;
constexpr int kLeftMouthCorner = 78;
constexpr int kRightMouthCorner = 308;
constexpr std::array<int, SG_GAZE_DISPLAY_FEATURE_COUNT> kDisplayFeatures {{
    0, 1, 6, 7,
}};
constexpr int kDisplayFitFeatureCount = 2;
constexpr std::array<float, SG_GAZE_DISPLAY_FEATURE_COUNT>
    kDisplayFeatureWeights {{1.0f, 1.0f, 0.25f, 0.25f}};

void set_error(char *error, size_t error_size, const std::string &message) {
    if (error && error_size > 0) {
        std::snprintf(error, error_size, "%s", message.c_str());
    }
}

float distance(const sg::RawFaceLandmark &a,
               const sg::RawFaceLandmark &b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

float normalized_axis(float value, float a, float b) {
    const float low = std::min(a, b);
    const float high = std::max(a, b);
    return ((value - low) / std::max(high - low, 0.001f)) * 2.0f - 1.0f;
}

void extract_features(const sg::RawFace &face,
                      const sg::GazeDirection &direction,
                      SgGazeFrame &frame) {
    const auto &right_outer = face.landmarks[kRightEyeOuter];
    const auto &right_inner = face.landmarks[kRightEyeInner];
    const auto &right_upper = face.landmarks[kRightEyeUpper];
    const auto &right_lower = face.landmarks[kRightEyeLower];
    const auto &right_iris = face.landmarks[kRightIris];
    const auto &left_inner = face.landmarks[kLeftEyeInner];
    const auto &left_outer = face.landmarks[kLeftEyeOuter];
    const auto &left_upper = face.landmarks[kLeftEyeUpper];
    const auto &left_lower = face.landmarks[kLeftEyeLower];
    const auto &left_iris = face.landmarks[kLeftIris];
    const auto &nose = face.landmarks[kNose];

    const float right_x = normalized_axis(right_iris.x,
                                          right_outer.x,
                                          right_inner.x);
    const float right_y = normalized_axis(right_iris.y,
                                          right_upper.y,
                                          right_lower.y);
    const float left_x = normalized_axis(left_iris.x,
                                         left_inner.x,
                                         left_outer.x);
    const float left_y = normalized_axis(left_iris.y,
                                         left_upper.y,
                                         left_lower.y);
    const float eye_mid_x = (right_outer.x + left_outer.x) * 0.5f;
    const float eye_mid_y = (right_outer.y + left_outer.y) * 0.5f;
    const float eye_distance = std::max(distance(right_outer, left_outer), 0.02f);
    const float roll = static_cast<float>(std::atan2(
        static_cast<double>(left_outer.y - right_outer.y),
        static_cast<double>(left_outer.x - right_outer.x)));

    frame.left_iris = {left_iris.x, left_iris.y, left_iris.z};
    frame.right_iris = {right_iris.x, right_iris.y, right_iris.z};
    frame.left_eye_openness = distance(left_upper, left_lower) /
        std::max(distance(left_inner, left_outer), 0.01f);
    frame.right_eye_openness = distance(right_upper, right_lower) /
        std::max(distance(right_inner, right_outer), 0.01f);
    frame.mouth_openness = distance(face.landmarks[kUpperLip],
                                    face.landmarks[kLowerLip]) /
        std::max(distance(face.landmarks[kLeftMouthCorner],
                          face.landmarks[kRightMouthCorner]),
                 0.01f);
    frame.gaze_yaw = direction.yaw;
    frame.gaze_pitch = direction.pitch;
    frame.model_confidence = direction.confidence;
    frame.model_active = true;
    frame.features[0] = direction.yaw;
    frame.features[1] = direction.pitch;
    frame.features[2] = left_x;
    frame.features[3] = left_y;
    frame.features[4] = right_x;
    frame.features[5] = right_y;
    frame.features[6] = (nose.x - eye_mid_x) / eye_distance;
    frame.features[7] = (nose.y - eye_mid_y) / eye_distance;
    frame.features[8] = roll;
    frame.features[9] = 1.0f / eye_distance;
}

template<size_t Size>
bool solve_system(std::array<std::array<double, Size + 1>, Size> matrix,
                  float coefficients[Size]) {
    for (size_t column = 0; column < Size; ++column) {
        size_t pivot = column;
        for (size_t row = column + 1; row < Size; ++row) {
            if (std::abs(matrix[row][column]) >
                std::abs(matrix[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(matrix[pivot][column]) < 1e-10) {
            return false;
        }
        std::swap(matrix[column], matrix[pivot]);
        const double divisor = matrix[column][column];
        for (size_t item = column; item <= Size; ++item) {
            matrix[column][item] /= divisor;
        }
        for (size_t row = 0; row < Size; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = matrix[row][column];
            for (size_t item = column; item <= Size; ++item) {
                matrix[row][item] -= factor * matrix[column][item];
            }
        }
    }
    for (size_t i = 0; i < Size; ++i) {
        coefficients[i] = static_cast<float>(matrix[i][Size]);
    }
    return true;
}

bool fit_axis(const SgGazeCalibration &calibration,
              int axis,
              float coefficients[SG_GAZE_COEFFICIENT_COUNT]) {
    std::array<std::array<double, SG_GAZE_COEFFICIENT_COUNT + 1>,
               SG_GAZE_COEFFICIENT_COUNT> matrix {};
    for (uint32_t point = 0; point < calibration.point_count; ++point) {
        std::array<double, SG_GAZE_COEFFICIENT_COUNT> row {};
        row[0] = 1.0;
        for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
            row[feature + 1] =
                (calibration.features[point][feature] -
                 calibration.feature_mean[feature]) /
                calibration.feature_scale[feature];
        }
        for (int y = 0; y < SG_GAZE_COEFFICIENT_COUNT; ++y) {
            for (int x = 0; x < SG_GAZE_COEFFICIENT_COUNT; ++x) {
                matrix[y][x] += row[y] * row[x];
            }
            matrix[y][SG_GAZE_COEFFICIENT_COUNT] +=
                row[y] * calibration.screen_points[point][axis];
        }
    }
    for (int i = 1; i < SG_GAZE_COEFFICIENT_COUNT; ++i) {
        matrix[i][i] += 0.015;
    }
    matrix[0][0] += 1e-6;
    return solve_system<SG_GAZE_COEFFICIENT_COUNT>(matrix, coefficients);
}

int point_display(const SgGazeCalibration &calibration, uint32_t point) {
    const float x = calibration.screen_points[point][0];
    const float y = calibration.screen_points[point][1];
    for (uint32_t display = 0; display < calibration.display_count; ++display) {
        const auto &bounds = calibration.display_bounds[display];
        if (x >= bounds[0] && x <= bounds[0] + bounds[2] &&
            y >= bounds[1] && y <= bounds[1] + bounds[3]) {
            return static_cast<int>(display);
        }
    }
    return -1;
}

bool fit_display_axis(const SgGazeCalibration &calibration,
                      uint32_t display,
                      int axis,
                      float coefficients[SG_GAZE_DISPLAY_COEFFICIENT_COUNT]) {
    std::array<std::array<double, SG_GAZE_DISPLAY_COEFFICIENT_COUNT + 1>,
               SG_GAZE_DISPLAY_COEFFICIENT_COUNT> matrix {};
    int point_count = 0;
    for (uint32_t point = 0; point < calibration.point_count; ++point) {
        if (point_display(calibration, point) != static_cast<int>(display)) {
            continue;
        }
        std::array<double, SG_GAZE_DISPLAY_COEFFICIENT_COUNT> row {};
        row[0] = 1.0;
        for (int feature = 0; feature < kDisplayFitFeatureCount; ++feature) {
            const int source = kDisplayFeatures[feature];
            row[feature + 1] =
                (calibration.features[point][source] -
                 calibration.display_feature_mean[display][feature]) /
                calibration.display_feature_scale[display][feature];
        }
        const auto &bounds = calibration.display_bounds[display];
        const double target =
            (calibration.screen_points[point][axis] - bounds[axis]) /
            std::max(bounds[axis + 2], 1e-4f);
        for (int y = 0; y < SG_GAZE_DISPLAY_COEFFICIENT_COUNT; ++y) {
            for (int x = 0; x < SG_GAZE_DISPLAY_COEFFICIENT_COUNT; ++x) {
                matrix[y][x] += row[y] * row[x];
            }
            matrix[y][SG_GAZE_DISPLAY_COEFFICIENT_COUNT] += row[y] * target;
        }
        ++point_count;
    }
    if (point_count < SG_GAZE_DISPLAY_COEFFICIENT_COUNT) {
        return false;
    }
    for (int i = 1; i < SG_GAZE_DISPLAY_COEFFICIENT_COUNT; ++i) {
        matrix[i][i] += 0.025;
    }
    matrix[0][0] += 1e-6;
    return solve_system<SG_GAZE_DISPLAY_COEFFICIENT_COUNT>(matrix,
                                                            coefficients);
}

bool fit_displays(SgGazeCalibration &calibration) {
    if (calibration.display_count == 0 ||
        calibration.display_count > SG_GAZE_MAX_DISPLAYS) {
        return calibration.display_count == 0;
    }
    for (uint32_t display = 0; display < calibration.display_count; ++display) {
        int point_count = 0;
        for (int feature = 0; feature < SG_GAZE_DISPLAY_FEATURE_COUNT; ++feature) {
            double total = 0.0;
            for (uint32_t point = 0; point < calibration.point_count; ++point) {
                if (point_display(calibration, point) ==
                    static_cast<int>(display)) {
                    total += calibration.features[point][kDisplayFeatures[feature]];
                    if (feature == 0) {
                        ++point_count;
                    }
                }
            }
            if (point_count == 0) {
                return false;
            }
            const float mean = static_cast<float>(total / point_count);
            calibration.display_feature_center[display][feature] = mean;
            calibration.display_feature_mean[display][feature] = mean;
            double variance = 0.0;
            for (uint32_t point = 0; point < calibration.point_count; ++point) {
                if (point_display(calibration, point) !=
                    static_cast<int>(display)) {
                    continue;
                }
                const double delta =
                    calibration.features[point][kDisplayFeatures[feature]] - mean;
                variance += delta * delta;
            }
            calibration.display_feature_scale[display][feature] =
                static_cast<float>(std::max(std::sqrt(variance / point_count),
                                            1e-4));
        }
        if (point_count < SG_GAZE_DISPLAY_COEFFICIENT_COUNT ||
            !fit_display_axis(calibration,
                              display,
                              0,
                              calibration.display_coefficients_x[display]) ||
            !fit_display_axis(calibration,
                              display,
                              1,
                              calibration.display_coefficients_y[display])) {
            return false;
        }
    }
    return true;
}

float display_distance(const SgGazeCalibration &calibration,
                       const float features[SG_GAZE_FEATURE_COUNT],
                       uint32_t display) {
    float distance = 0.0f;
    for (int feature = 0; feature < SG_GAZE_DISPLAY_FEATURE_COUNT; ++feature) {
        const int source = kDisplayFeatures[feature];
        const float scale = std::max(calibration.feature_scale[source], 1e-4f);
        const float delta = (features[source] -
                             calibration.display_feature_center[display][feature]) /
            scale;
        distance += delta * delta * kDisplayFeatureWeights[feature];
    }
    return distance;
}

int select_display(const SgGazeCalibration &calibration,
                   const float features[SG_GAZE_FEATURE_COUNT],
                   int preferred) {
    int selected = 0;
    float selected_distance = std::numeric_limits<float>::infinity();
    for (uint32_t display = 0; display < calibration.display_count; ++display) {
        const float distance = display_distance(calibration, features, display);
        if (distance < selected_distance) {
            selected = static_cast<int>(display);
            selected_distance = distance;
        }
    }
    if (preferred >= 0 && preferred < static_cast<int>(calibration.display_count) &&
        selected != preferred) {
        const float preferred_distance = display_distance(calibration,
                                                          features,
                                                          preferred);
        if (selected_distance > preferred_distance * 0.50f) {
            return preferred;
        }
    }
    return selected;
}

void predict_display_linear(const SgGazeCalibration &calibration,
                            const float features[SG_GAZE_FEATURE_COUNT],
                            int display,
                            float &x,
                            float &y) {
    float local_x = calibration.display_coefficients_x[display][0];
    float local_y = calibration.display_coefficients_y[display][0];
    for (int feature = 0; feature < kDisplayFitFeatureCount; ++feature) {
        const int source = kDisplayFeatures[feature];
        const float value = std::clamp(
            (features[source] -
             calibration.display_feature_mean[display][feature]) /
                calibration.display_feature_scale[display][feature],
            -2.5f,
            2.5f);
        local_x += calibration.display_coefficients_x[display][feature + 1] *
            value;
        local_y += calibration.display_coefficients_y[display][feature + 1] *
            value;
    }
    const auto &bounds = calibration.display_bounds[display];
    x = bounds[0] + local_x * bounds[2];
    y = bounds[1] + local_y * bounds[3];
}

void predict_linear(const SgGazeCalibration &calibration,
                    const float features[SG_GAZE_FEATURE_COUNT],
                    float &x,
                    float &y);
void correct_local_residual(const SgGazeCalibration &calibration,
                            const float features[SG_GAZE_FEATURE_COUNT],
                            float &x,
                            float &y);

bool predict_calibration(const SgGazeCalibration &calibration,
                         const float features[SG_GAZE_FEATURE_COUNT],
                         int preferred_display,
                         SgVec3 &screen,
                         int &selected_display) {
    if (!calibration.valid) {
        return false;
    }
    if (calibration.display_count == 0) {
        float x = 0.0f;
        float y = 0.0f;
        predict_linear(calibration, features, x, y);
        correct_local_residual(calibration, features, x, y);
        screen = {
            std::clamp(x, 0.0f, 1.0f),
            std::clamp(y, 0.0f, 1.0f),
            0.0f,
        };
        selected_display = -1;
        return true;
    }

    selected_display = select_display(calibration,
                                      features,
                                      preferred_display);
    float x = 0.0f;
    float y = 0.0f;
    predict_display_linear(calibration,
                           features,
                           selected_display,
                           x,
                           y);
    const auto &bounds = calibration.display_bounds[selected_display];
    screen = {
        std::clamp(x, bounds[0], bounds[0] + bounds[2]),
        std::clamp(y, bounds[1], bounds[1] + bounds[3]),
        0.0f,
    };
    return true;
}

void predict_linear(const SgGazeCalibration &calibration,
                    const float features[SG_GAZE_FEATURE_COUNT],
                    float &x,
                    float &y) {
    x = calibration.coefficients_x[0];
    y = calibration.coefficients_y[0];
    for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
        const float value = (features[feature] -
                             calibration.feature_mean[feature]) /
            std::max(calibration.feature_scale[feature], 1e-4f);
        x += calibration.coefficients_x[feature + 1] * value;
        y += calibration.coefficients_y[feature + 1] * value;
    }
}

float feature_distance(const SgGazeCalibration &calibration,
                       const float a[SG_GAZE_FEATURE_COUNT],
                       const float b[SG_GAZE_FEATURE_COUNT]) {
    constexpr std::array<int, 4> selected {{0, 1, 6, 7}};
    constexpr std::array<float, 4> weights {{1.0f, 1.0f, 0.72f, 0.72f}};
    float result = 0.0f;
    for (size_t item = 0; item < selected.size(); ++item) {
        const int feature = selected[item];
        const float delta = (a[feature] - b[feature]) /
            std::max(calibration.feature_scale[feature], 1e-4f);
        result += delta * delta * weights[item];
    }
    return result;
}

void correct_local_residual(const SgGazeCalibration &calibration,
                            const float features[SG_GAZE_FEATURE_COUNT],
                            float &x,
                            float &y) {
    constexpr size_t neighbor_count = 6;
    std::array<std::pair<float, uint32_t>, neighbor_count> nearest;
    nearest.fill({std::numeric_limits<float>::infinity(), 0});
    for (uint32_t point = 0; point < calibration.point_count; ++point) {
        const float distance = feature_distance(calibration,
                                                features,
                                                calibration.features[point]);
        for (size_t neighbor = 0; neighbor < nearest.size(); ++neighbor) {
            if (distance >= nearest[neighbor].first) {
                continue;
            }
            for (size_t move = nearest.size() - 1; move > neighbor; --move) {
                nearest[move] = nearest[move - 1];
            }
            nearest[neighbor] = {distance, point};
            break;
        }
    }

    const size_t count = std::min(neighbor_count,
                                  static_cast<size_t>(calibration.point_count));
    double weight_sum = 0.0;
    double correction_x = 0.0;
    double correction_y = 0.0;
    for (size_t neighbor = 0; neighbor < count; ++neighbor) {
        const uint32_t point = nearest[neighbor].second;
        float predicted_x = 0.0f;
        float predicted_y = 0.0f;
        predict_linear(calibration,
                       calibration.features[point],
                       predicted_x,
                       predicted_y);
        const double weight = 1.0 / (0.002 + nearest[neighbor].first);
        weight_sum += weight;
        correction_x += weight *
            (calibration.screen_points[point][0] - predicted_x);
        correction_y += weight *
            (calibration.screen_points[point][1] - predicted_y);
    }
    if (weight_sum > 0.0) {
        x += static_cast<float>(correction_x / weight_sum);
        y += static_cast<float>(correction_y / weight_sum);
    }
}

bool read_bool(std::istream &input, bool &value) {
    int parsed = 0;
    if (!(input >> parsed)) {
        return false;
    }
    value = parsed != 0;
    return true;
}

float median_axis(const std::array<SgVec3, 5> &points,
                  size_t count,
                  bool horizontal) {
    const auto axis = [horizontal](const SgVec3 &point) {
        return horizontal ? point.x : point.y;
    };
    if (count <= 1) {
        return axis(points[0]);
    }
    if (count == 2) {
        return (axis(points[0]) + axis(points[1])) * 0.5f;
    }
    if (count == 3) {
        std::array<float, 3> values {{
            axis(points[0]), axis(points[1]), axis(points[2]),
        }};
        std::sort(values.begin(), values.end());
        return values[1];
    }
    if (count == 4) {
        std::array<float, 4> values {{
            axis(points[0]), axis(points[1]), axis(points[2]), axis(points[3]),
        }};
        std::sort(values.begin(), values.end());
        return (values[1] + values[2]) * 0.5f;
    }
    std::array<float, 5> values {{
        axis(points[0]), axis(points[1]), axis(points[2]), axis(points[3]),
        axis(points[4]),
    }};
    std::sort(values.begin(), values.end());
    return values[2];
}

} // namespace

extern "C" {

SgGazeEngine *sg_gaze_engine_create(const SgGazeConfig *config,
                                     char *error,
                                     size_t error_size) {
    if (!config) {
        set_error(error, error_size, "Gaze configuration is missing");
        return nullptr;
    }
    auto engine = std::make_unique<SgGazeEngine>();
    engine->backend = std::make_unique<sg::FaceBackend>(
        config->runtime_path,
        config->model_path,
        config->min_detection_confidence > 0.0f
            ? config->min_detection_confidence
            : 0.50f,
        config->min_presence_confidence > 0.0f
            ? config->min_presence_confidence
            : 0.50f,
        config->min_tracking_confidence > 0.0f
            ? config->min_tracking_confidence
            : 0.50f);
    if (!engine->backend->ready()) {
        set_error(error, error_size, engine->backend->error());
        return nullptr;
    }
    engine->model = std::make_unique<sg::GazeModel>(
        config->gaze_runtime_path,
        config->gaze_model_path,
        config->cpu_threads > 0 ? config->cpu_threads : 2);
    if (!engine->model->ready()) {
        set_error(error, error_size, engine->model->error());
        return nullptr;
    }
    return engine.release();
}

void sg_gaze_engine_destroy(SgGazeEngine *engine) {
    delete engine;
}

bool sg_gaze_engine_process_rgb(SgGazeEngine *engine,
                                const uint8_t *pixels,
                                int width,
                                int height,
                                int stride,
                                int64_t timestamp_ms,
                                SgGazeFrame *frame,
                                char *error,
                                size_t error_size) {
    if (!engine || !frame) {
        set_error(error, error_size, "Gaze engine or output frame is missing");
        return false;
    }

    sg::RawFace face;
    bool present = false;
    if (!engine->backend->detect(pixels,
                                 width,
                                 height,
                                 stride,
                                 timestamp_ms,
                                 face,
                                 present)) {
        set_error(error, error_size, engine->backend->error());
        return false;
    }

    std::memset(frame, 0, sizeof(*frame));
    frame->sequence = ++engine->sequence;
    frame->timestamp_ms = timestamp_ms;
    frame->present = present;
    if (!present) {
        engine->previous = *frame;
        engine->last_timestamp_ms = timestamp_ms;
        engine->recent_screen_count = 0;
        engine->recent_screen_next = 0;
        engine->active_display = -1;
        return true;
    }

    sg::GazeDirection direction;
    if (!engine->model->infer(pixels,
                              width,
                              height,
                              stride,
                              face,
                              direction)) {
        frame->present = false;
        engine->previous = *frame;
        engine->last_timestamp_ms = timestamp_ms;
        engine->recent_screen_count = 0;
        engine->recent_screen_next = 0;
        return true;
    }
    frame->confidence = face.confidence;
    extract_features(face, direction, *frame);
    frame->calibrated = engine->calibration.valid;
    const float left_reference = engine->calibration.left_open_reference > 0.0f
        ? engine->calibration.left_open_reference
        : 0.23f;
    const float right_reference = engine->calibration.right_open_reference > 0.0f
        ? engine->calibration.right_open_reference
        : 0.23f;
    frame->left_eye_closed = frame->left_eye_openness < left_reference * 0.52f;
    frame->right_eye_closed = frame->right_eye_openness < right_reference * 0.52f;

    int selected_display = engine->active_display;
    if (!predict_calibration(engine->calibration,
                             frame->features,
                             engine->active_display,
                             frame->screen,
                             selected_display)) {
        frame->screen = {
            std::clamp(0.5f - frame->features[0] * 0.72f +
                       frame->features[6] * 0.08f,
                       0.0f,
                       1.0f),
            std::clamp(0.5f - frame->features[1] * 0.62f +
                       (frame->features[7] - 0.55f) * 0.08f,
                       0.0f,
                       1.0f),
            0.0f,
        };
    } else {
        engine->active_display = selected_display;
    }

    engine->recent_screen[engine->recent_screen_next] = frame->screen;
    engine->recent_screen_next = (engine->recent_screen_next + 1) %
        engine->recent_screen.size();
    engine->recent_screen_count = std::min(engine->recent_screen_count + 1,
                                           engine->recent_screen.size());
    frame->screen.x = median_axis(engine->recent_screen,
                                  engine->recent_screen_count,
                                  true);
    frame->screen.y = median_axis(engine->recent_screen,
                                  engine->recent_screen_count,
                                  false);

    if (engine->previous.present && engine->last_timestamp_ms > 0) {
        const float delta = std::clamp((timestamp_ms - engine->last_timestamp_ms) /
                                       1000.0f,
                                       0.001f,
                                       0.1f);
        float dx = frame->screen.x - engine->previous.screen.x;
        float dy = frame->screen.y - engine->previous.screen.y;
        const float movement = std::hypot(dx, dy);
        const float maximum_step = 0.018f + delta * 0.55f;
        if (movement > maximum_step) {
            dx *= maximum_step / movement;
            dy *= maximum_step / movement;
            frame->screen.x = engine->previous.screen.x + dx;
            frame->screen.y = engine->previous.screen.y + dy;
        }
        if (movement < 0.004f) {
            frame->screen = engine->previous.screen;
        }
        const float alpha = 1.0f - std::exp(-6.0f * delta);
        frame->screen.x = engine->previous.screen.x +
            (frame->screen.x - engine->previous.screen.x) * alpha;
        frame->screen.y = engine->previous.screen.y +
            (frame->screen.y - engine->previous.screen.y) * alpha;
    }
    engine->previous = *frame;
    engine->last_timestamp_ms = timestamp_ms;
    return true;
}

void sg_gaze_engine_set_calibration(SgGazeEngine *engine,
                                    const SgGazeCalibration *calibration) {
    if (!engine || !calibration) {
        return;
    }
    engine->calibration = *calibration;
    engine->previous = {};
    engine->last_timestamp_ms = 0;
    engine->recent_screen_count = 0;
    engine->recent_screen_next = 0;
    engine->active_display = -1;
}

void sg_gaze_engine_get_calibration(const SgGazeEngine *engine,
                                    SgGazeCalibration *calibration) {
    if (engine && calibration) {
        *calibration = engine->calibration;
    }
}

bool sg_gaze_calibration_fit(SgGazeCalibration *calibration) {
    if (!calibration || calibration->point_count < 12 ||
        calibration->point_count > SG_GAZE_MAX_CALIBRATION_POINTS) {
        return false;
    }

    for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
        double mean = 0.0;
        for (uint32_t point = 0; point < calibration->point_count; ++point) {
            mean += calibration->features[point][feature];
        }
        mean /= calibration->point_count;
        double variance = 0.0;
        for (uint32_t point = 0; point < calibration->point_count; ++point) {
            const double delta = calibration->features[point][feature] - mean;
            variance += delta * delta;
        }
        calibration->feature_mean[feature] = static_cast<float>(mean);
        calibration->feature_scale[feature] = static_cast<float>(
            std::max(std::sqrt(variance / calibration->point_count), 1e-4));
    }

    if (!fit_axis(*calibration, 0, calibration->coefficients_x) ||
        !fit_axis(*calibration, 1, calibration->coefficients_y) ||
        !fit_displays(*calibration)) {
        calibration->valid = false;
        return false;
    }
    calibration->valid = true;
    double error = 0.0;
    for (uint32_t point = 0; point < calibration->point_count; ++point) {
        SgVec3 predicted {};
        sg_gaze_calibration_predict(calibration,
                                    calibration->features[point],
                                    &predicted);
        const double dx = (predicted.x - calibration->screen_points[point][0]) *
            std::max(calibration->screen_aspect, 1.0f);
        const double dy = predicted.y - calibration->screen_points[point][1];
        error += std::hypot(dx, dy);
    }
    calibration->mean_error = static_cast<float>(error / calibration->point_count);
    return true;
}

bool sg_gaze_calibration_predict(const SgGazeCalibration *calibration,
                                 const float features[SG_GAZE_FEATURE_COUNT],
                                 SgVec3 *screen) {
    if (!calibration || !calibration->valid || !features || !screen) {
        return false;
    }
    int selected_display = -1;
    return predict_calibration(*calibration,
                               features,
                               -1,
                               *screen,
                               selected_display);
}

bool sg_gaze_calibration_save(const SgGazeCalibration *calibration,
                              const char *path) {
    if (!calibration || !path || !calibration->valid) {
        return false;
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "SGGAZE 4\n";
    output << calibration->point_count << ' '
           << calibration->screen_aspect << ' '
           << calibration->left_open_reference << ' '
           << calibration->right_open_reference << ' '
           << calibration->mean_error << ' '
           << calibration->valid << '\n';
    for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
        output << calibration->feature_mean[feature] << ' '
               << calibration->feature_scale[feature] << '\n';
    }
    for (int coefficient = 0;
         coefficient < SG_GAZE_COEFFICIENT_COUNT;
         ++coefficient) {
        output << calibration->coefficients_x[coefficient] << ' '
               << calibration->coefficients_y[coefficient] << '\n';
    }
    output << calibration->display_count << '\n';
    for (uint32_t display = 0;
         display < calibration->display_count;
         ++display) {
        for (int value = 0; value < 4; ++value) {
            output << calibration->display_bounds[display][value] << ' ';
        }
        output << '\n';
        for (int feature = 0;
             feature < SG_GAZE_DISPLAY_FEATURE_COUNT;
             ++feature) {
            output << calibration->display_feature_center[display][feature] << ' '
                   << calibration->display_feature_mean[display][feature] << ' '
                   << calibration->display_feature_scale[display][feature] << '\n';
        }
        for (int coefficient = 0;
             coefficient < SG_GAZE_DISPLAY_COEFFICIENT_COUNT;
             ++coefficient) {
            output << calibration->display_coefficients_x[display][coefficient] << ' '
                   << calibration->display_coefficients_y[display][coefficient] << '\n';
        }
    }
    for (uint32_t point = 0; point < calibration->point_count; ++point) {
        output << calibration->screen_points[point][0] << ' '
               << calibration->screen_points[point][1];
        for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
            output << ' ' << calibration->features[point][feature];
        }
        output << '\n';
    }
    return output.good();
}

bool sg_gaze_calibration_load(SgGazeCalibration *calibration,
                              const char *path) {
    if (!calibration || !path) {
        return false;
    }
    std::ifstream input(path);
    std::string magic;
    int version = 0;
    if (!(input >> magic >> version) || magic != "SGGAZE" || version != 4) {
        return false;
    }
    SgGazeCalibration parsed {};
    if (!(input >> parsed.point_count
                >> parsed.screen_aspect
                >> parsed.left_open_reference
                >> parsed.right_open_reference
                >> parsed.mean_error) ||
        !read_bool(input, parsed.valid)) {
        return false;
    }
    if (!parsed.valid ||
        parsed.point_count < 12 ||
        parsed.point_count > SG_GAZE_MAX_CALIBRATION_POINTS ||
        !std::isfinite(parsed.screen_aspect) ||
        !std::isfinite(parsed.mean_error)) {
        return false;
    }
    for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
        if (!(input >> parsed.feature_mean[feature]
                    >> parsed.feature_scale[feature]) ||
            !std::isfinite(parsed.feature_mean[feature]) ||
            !std::isfinite(parsed.feature_scale[feature]) ||
            parsed.feature_scale[feature] <= 0.0f) {
            return false;
        }
    }
    for (int coefficient = 0;
         coefficient < SG_GAZE_COEFFICIENT_COUNT;
         ++coefficient) {
        if (!(input >> parsed.coefficients_x[coefficient]
                    >> parsed.coefficients_y[coefficient]) ||
            !std::isfinite(parsed.coefficients_x[coefficient]) ||
            !std::isfinite(parsed.coefficients_y[coefficient])) {
            return false;
        }
    }
    if (!(input >> parsed.display_count) ||
        parsed.display_count > SG_GAZE_MAX_DISPLAYS) {
        return false;
    }
    for (uint32_t display = 0;
         display < parsed.display_count;
         ++display) {
        for (int value = 0; value < 4; ++value) {
            if (!(input >> parsed.display_bounds[display][value]) ||
                !std::isfinite(parsed.display_bounds[display][value])) {
                return false;
            }
        }
        const auto &bounds = parsed.display_bounds[display];
        if (bounds[0] < 0.0f || bounds[1] < 0.0f ||
            bounds[2] <= 0.0f || bounds[3] <= 0.0f ||
            bounds[0] + bounds[2] > 1.001f ||
            bounds[1] + bounds[3] > 1.001f) {
            return false;
        }
        for (int feature = 0;
             feature < SG_GAZE_DISPLAY_FEATURE_COUNT;
             ++feature) {
            if (!(input >> parsed.display_feature_center[display][feature]
                        >> parsed.display_feature_mean[display][feature]
                        >> parsed.display_feature_scale[display][feature]) ||
                !std::isfinite(
                    parsed.display_feature_center[display][feature]) ||
                !std::isfinite(
                    parsed.display_feature_mean[display][feature]) ||
                !std::isfinite(
                    parsed.display_feature_scale[display][feature]) ||
                parsed.display_feature_scale[display][feature] <= 0.0f) {
                return false;
            }
        }
        for (int coefficient = 0;
             coefficient < SG_GAZE_DISPLAY_COEFFICIENT_COUNT;
             ++coefficient) {
            if (!(input >> parsed.display_coefficients_x[display][coefficient]
                        >> parsed.display_coefficients_y[display][coefficient]) ||
                !std::isfinite(
                    parsed.display_coefficients_x[display][coefficient]) ||
                !std::isfinite(
                    parsed.display_coefficients_y[display][coefficient])) {
                return false;
            }
        }
    }
    for (uint32_t point = 0; point < parsed.point_count; ++point) {
        if (!(input >> parsed.screen_points[point][0]
                    >> parsed.screen_points[point][1]) ||
            !std::isfinite(parsed.screen_points[point][0]) ||
            !std::isfinite(parsed.screen_points[point][1]) ||
            parsed.screen_points[point][0] < 0.0f ||
            parsed.screen_points[point][0] > 1.0f ||
            parsed.screen_points[point][1] < 0.0f ||
            parsed.screen_points[point][1] > 1.0f) {
            return false;
        }
        for (int feature = 0; feature < SG_GAZE_FEATURE_COUNT; ++feature) {
            if (!(input >> parsed.features[point][feature]) ||
                !std::isfinite(parsed.features[point][feature])) {
                return false;
            }
        }
    }
    *calibration = parsed;
    return parsed.valid;
}

} // extern "C"
