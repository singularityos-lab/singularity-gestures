#include <singularity/gaze.h>

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {

bool close_to(float actual, float expected, float epsilon = 0.025f) {
    return std::abs(actual - expected) < epsilon;
}

void fill_features(float x, float y, float features[SG_GAZE_FEATURE_COUNT]) {
    features[0] = x;
    features[1] = y;
    features[2] = x * y;
    features[3] = x * x;
    features[4] = y * y;
    features[5] = std::sin(x * 2.0f);
    features[6] = std::cos(y * 1.7f);
    features[7] = x - y;
    features[8] = x + y;
    features[9] = 0.4f * x - 0.2f * y;
}

void test_calibration() {
    SgGazeCalibration calibration {};
    calibration.screen_aspect = 4.8f;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 5; ++column) {
            const int point = row * 5 + column;
            const float x = 0.10f + column * 0.20f;
            const float y = 0.14f + row * 0.24f;
            fill_features(x, y, calibration.features[point]);
            calibration.screen_points[point][0] =
                0.03f + x * 0.90f + y * 0.04f;
            calibration.screen_points[point][1] =
                0.02f + y * 0.92f - x * 0.03f;
            ++calibration.point_count;
        }
    }
    assert(sg_gaze_calibration_fit(&calibration));
    assert(calibration.valid);

    float features[SG_GAZE_FEATURE_COUNT] {};
    fill_features(0.46f, 0.53f, features);
    SgVec3 screen {};
    assert(sg_gaze_calibration_predict(&calibration, features, &screen));
    assert(close_to(screen.x, 0.03f + 0.46f * 0.90f + 0.53f * 0.04f));
    assert(close_to(screen.y, 0.02f + 0.53f * 0.92f - 0.46f * 0.03f));

    const auto path = std::filesystem::temp_directory_path() /
        "singularity-gaze-test.calibration";
    assert(sg_gaze_calibration_save(&calibration, path.c_str()));
    SgGazeCalibration loaded {};
    assert(sg_gaze_calibration_load(&loaded, path.c_str()));
    assert(close_to(loaded.mean_error, calibration.mean_error, 0.001f));
    assert(sg_gaze_calibration_predict(&loaded, features, &screen));
    std::filesystem::remove(path);

    {
        std::ofstream invalid(path);
        invalid << "SGGAZE 2\n33 4.8 0.2 0.2 20 1\n";
    }
    assert(!sg_gaze_calibration_load(&loaded, path.c_str()));
    std::filesystem::remove(path);
}

void test_local_correction() {
    SgGazeCalibration calibration {};
    calibration.screen_aspect = 4.8f;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 5; ++column) {
            const int point = row * 5 + column;
            const float x = 0.10f + column * 0.20f;
            const float y = 0.14f + row * 0.24f;
            calibration.features[point][0] = x;
            calibration.features[point][1] = y;
            calibration.features[point][6] = x * 0.4f;
            calibration.features[point][7] = y * 0.3f;
            calibration.screen_points[point][0] =
                0.04f + x * 0.84f + std::sin(x * 7.0f) * 0.07f;
            calibration.screen_points[point][1] =
                0.05f + y * 0.82f + std::cos(y * 6.0f) * 0.06f;
            ++calibration.point_count;
        }
    }
    assert(sg_gaze_calibration_fit(&calibration));
    for (uint32_t point = 0; point < calibration.point_count; ++point) {
        SgVec3 screen {};
        assert(sg_gaze_calibration_predict(&calibration,
                                           calibration.features[point],
                                           &screen));
        assert(close_to(screen.x,
                        calibration.screen_points[point][0],
                        0.012f));
        assert(close_to(screen.y,
                        calibration.screen_points[point][1],
                        0.012f));
    }
}

void fill_display_features(int display,
                           float x,
                           float y,
                           float features[SG_GAZE_FEATURE_COUNT]) {
    const float display_yaw[] {-0.85f, 0.0f, 0.90f};
    features[0] = display_yaw[display] + (x - 0.5f) * 0.34f;
    features[1] = (y - 0.5f) * 0.58f + display * 0.012f;
    features[2] = features[0] * 0.92f + y * 0.03f;
    features[3] = features[1] * 1.04f - x * 0.02f;
    features[4] = features[0] * 1.07f - y * 0.02f;
    features[5] = features[1] * 0.95f + x * 0.02f;
    features[6] = display_yaw[display] * 0.62f + (x - 0.5f) * 0.14f;
    features[7] = 0.54f + (y - 0.5f) * 0.16f + display * 0.008f;
    features[8] = display_yaw[display] * 0.02f;
    features[9] = 3.0f + y * 0.04f;
}

void test_display_calibration() {
    static constexpr std::array<std::array<float, 2>, 9> points {{
        {{0.50f, 0.50f}}, {{0.12f, 0.18f}}, {{0.50f, 0.18f}},
        {{0.88f, 0.18f}}, {{0.88f, 0.50f}}, {{0.88f, 0.82f}},
        {{0.50f, 0.82f}}, {{0.12f, 0.82f}}, {{0.12f, 0.50f}},
    }};
    SgGazeCalibration calibration {};
    calibration.screen_aspect = 4.8f;
    calibration.display_count = 3;
    for (int display = 0; display < 3; ++display) {
        calibration.display_bounds[display][0] = display / 3.0f;
        calibration.display_bounds[display][1] = 0.0f;
        calibration.display_bounds[display][2] = 1.0f / 3.0f;
        calibration.display_bounds[display][3] = 1.0f;
        for (const auto &point : points) {
            const uint32_t sample = calibration.point_count++;
            fill_display_features(display,
                                  point[0],
                                  point[1],
                                  calibration.features[sample]);
            calibration.screen_points[sample][0] =
                (display + point[0]) / 3.0f;
            calibration.screen_points[sample][1] = point[1];
        }
    }
    assert(sg_gaze_calibration_fit(&calibration));
    assert(calibration.valid);

    for (int display = 0; display < 3; ++display) {
        float features[SG_GAZE_FEATURE_COUNT] {};
        fill_display_features(display, 0.34f, 0.63f, features);
        SgVec3 screen {};
        assert(sg_gaze_calibration_predict(&calibration, features, &screen));
        assert(screen.x >= display / 3.0f);
        assert(screen.x <= (display + 1.0f) / 3.0f);
        assert(close_to(screen.x, (display + 0.34f) / 3.0f, 0.035f));
        assert(close_to(screen.y, 0.63f, 0.035f));
        const SgVec3 stable = screen;
        features[6] += 0.06f;
        features[7] -= 0.05f;
        assert(sg_gaze_calibration_predict(&calibration, features, &screen));
        assert(close_to(screen.x, stable.x, 0.005f));
        assert(close_to(screen.y, stable.y, 0.005f));
    }

    const auto path = std::filesystem::temp_directory_path() /
        "singularity-gaze-display-test.calibration";
    assert(sg_gaze_calibration_save(&calibration, path.c_str()));
    SgGazeCalibration loaded {};
    assert(sg_gaze_calibration_load(&loaded, path.c_str()));
    assert(loaded.display_count == 3);
    std::filesystem::remove(path);
}

} // namespace

int main() {
    test_calibration();
    test_local_correction();
    test_display_calibration();
    return 0;
}
