#include "lab/renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <GL/gl.h>
#include <GL/glu.h>
#include <X11/Xlib.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <SDL_syswm.h>

namespace sg::lab {

namespace {

constexpr std::array<std::array<int, 2>, 21> kHandConnections {{
    {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 4}},
    {{0, 5}}, {{5, 6}}, {{6, 7}}, {{7, 8}},
    {{5, 9}}, {{9, 10}}, {{10, 11}}, {{11, 12}},
    {{9, 13}}, {{13, 14}}, {{14, 15}}, {{15, 16}},
    {{13, 17}}, {{17, 18}}, {{18, 19}}, {{19, 20}}, {{0, 17}},
}};

constexpr std::array<int, 5> kFingerTips {{4, 8, 12, 16, 20}};

bool gaze_pointer_visible(const RenderState &state) {
    if (state.control_mode != ControlMode::Eyes ||
        !state.gaze.present || !state.gaze.calibrated ||
        state.gaze_calibration.visible) {
        return false;
    }
    return state.eye_control.enabled;
}

bool set_x11_geometry(SDL_Window *window,
                      bool override_redirect,
                      int x,
                      int y,
                      int width,
                      int height) {
    SDL_SysWMinfo info {};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) ||
        info.subsystem != SDL_SYSWM_X11) {
        return false;
    }
    XSetWindowAttributes attributes {};
    attributes.override_redirect = override_redirect ? True : False;
    XUnmapWindow(info.info.x11.display, info.info.x11.window);
    XChangeWindowAttributes(info.info.x11.display,
                            info.info.x11.window,
                            CWOverrideRedirect,
                            &attributes);
    XMoveResizeWindow(info.info.x11.display,
                      info.info.x11.window,
                      x,
                      y,
                      static_cast<unsigned int>(width),
                      static_cast<unsigned int>(height));
    XMapRaised(info.info.x11.display, info.info.x11.window);
    XSync(info.info.x11.display, False);
    SDL_PumpEvents();
    return true;
}

void begin_overlay(int width, int height) {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, width, height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void circle(float x, float y, float radius, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= 28; ++i) {
        const float angle = static_cast<float>(i) * 2.0f * static_cast<float>(M_PI) / 28.0f;
        glVertex2f(x + std::cos(angle) * radius,
                   y + std::sin(angle) * radius);
    }
    glEnd();
}

void ring(float x, float y, float radius, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 36; ++i) {
        const float angle = static_cast<float>(i) * 2.0f * static_cast<float>(M_PI) / 36.0f;
        glVertex2f(x + std::cos(angle) * radius,
                   y + std::sin(angle) * radius);
    }
    glEnd();
}

void progress_ring(float x,
                   float y,
                   float radius,
                   float progress,
                   float red,
                   float green,
                   float blue,
                   float alpha) {
    const int segments = std::max(2, static_cast<int>(48 * progress));
    glColor4f(red, green, blue, alpha);
    glLineWidth(5.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= segments; ++i) {
        const float angle = -static_cast<float>(M_PI_2) +
            static_cast<float>(i) * 2.0f * static_cast<float>(M_PI) / 48.0f;
        glVertex2f(x + std::cos(angle) * radius,
                   y + std::sin(angle) * radius);
    }
    glEnd();
}

void rounded_rectangle(cairo_t *cr,
                       double x,
                       double y,
                       double width,
                       double height,
                       double radius) {
    const double right = x + width;
    const double bottom = y + height;
    cairo_new_sub_path(cr);
    cairo_arc(cr, right - radius, y + radius, radius, -M_PI_2, 0.0);
    cairo_arc(cr, right - radius, bottom - radius, radius, 0.0, M_PI_2);
    cairo_arc(cr, x + radius, bottom - radius, radius, M_PI_2, M_PI);
    cairo_arc(cr, x + radius, y + radius, radius, M_PI, M_PI * 1.5);
    cairo_close_path(cr);
}

void text(cairo_t *cr,
          const std::string &value,
          double x,
          double y,
          int width,
          const char *font,
          double red,
          double green,
          double blue,
          double alpha,
          PangoAlignment alignment = PANGO_ALIGN_LEFT) {
    auto *layout = pango_cairo_create_layout(cr);
    auto *description = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, description);
    pango_layout_set_text(layout, value.c_str(), -1);
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_alignment(layout, alignment);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    cairo_set_source_rgba(cr, red, green, blue, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(description);
    g_object_unref(layout);
}

void solid_cube(float half_size, float alpha) {
    glBegin(GL_QUADS);
    glColor4f(0.20f, 0.57f, 0.91f, alpha);
    glVertex3f(-half_size, -half_size, half_size);
    glVertex3f(half_size, -half_size, half_size);
    glVertex3f(half_size, half_size, half_size);
    glVertex3f(-half_size, half_size, half_size);

    glColor4f(0.08f, 0.29f, 0.66f, alpha);
    glVertex3f(half_size, -half_size, -half_size);
    glVertex3f(-half_size, -half_size, -half_size);
    glVertex3f(-half_size, half_size, -half_size);
    glVertex3f(half_size, half_size, -half_size);

    glColor4f(0.11f, 0.39f, 0.76f, alpha);
    glVertex3f(-half_size, -half_size, -half_size);
    glVertex3f(-half_size, -half_size, half_size);
    glVertex3f(-half_size, half_size, half_size);
    glVertex3f(-half_size, half_size, -half_size);

    glColor4f(0.31f, 0.69f, 0.96f, alpha);
    glVertex3f(half_size, -half_size, half_size);
    glVertex3f(half_size, -half_size, -half_size);
    glVertex3f(half_size, half_size, -half_size);
    glVertex3f(half_size, half_size, half_size);

    glColor4f(0.42f, 0.76f, 0.98f, alpha);
    glVertex3f(-half_size, half_size, half_size);
    glVertex3f(half_size, half_size, half_size);
    glVertex3f(half_size, half_size, -half_size);
    glVertex3f(-half_size, half_size, -half_size);

    glColor4f(0.05f, 0.20f, 0.52f, alpha);
    glVertex3f(-half_size, -half_size, -half_size);
    glVertex3f(half_size, -half_size, -half_size);
    glVertex3f(half_size, -half_size, half_size);
    glVertex3f(-half_size, -half_size, half_size);
    glEnd();
}

} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer() {
    if (hud_surface_) {
        cairo_surface_destroy(hud_surface_);
    }
    if (context_) {
        SDL_GL_DeleteContext(context_);
    }
    if (window_) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

bool Renderer::initialize() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        error_ = SDL_GetError();
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    window_ = SDL_CreateWindow(
        "Singularity Gesture Lab",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width_,
        height_,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        error_ = SDL_GetError();
        return false;
    }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        error_ = SDL_GetError();
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    SDL_GL_GetDrawableSize(window_, &width_, &height_);
    display_count_ = std::max(SDL_GetNumVideoDisplays(), 1);

    glGenTextures(1, &camera_texture_);
    glGenTextures(1, &hud_texture_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_POINT_SMOOTH);
    create_hud_surface();
    return true;
}

void Renderer::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    create_hud_surface();
}

void Renderer::render(const RenderState &state) {
    glViewport(0, 0, width_, height_);
    glClearColor(0.075f, 0.075f, 0.075f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!state.presentation && state.camera &&
        state.camera->sequence != camera_sequence_) {
        upload_camera(*state.camera);
    }
    if (state.control_mode == ControlMode::Hands &&
        state.gestures.sequence != gesture_sequence_) {
        update_trails(state.gestures);
    } else if (state.control_mode != ControlMode::Hands) {
        for (auto &trail : trails_) {
            trail.clear();
        }
    }
    if (gaze_pointer_visible(state) &&
        state.gaze.sequence != gaze_sequence_) {
        update_gaze_trail(state.gaze);
    } else if (!gaze_pointer_visible(state)) {
        gaze_trail_.clear();
    }
    if (state.calibration.visible || state.gaze_calibration.visible) {
        for (auto &trail : trails_) {
            trail.clear();
        }
        if (state.gaze_calibration.phase != GazeCalibrationPhase::Validate) {
            gaze_trail_.clear();
        }
    }

    if (!state.presentation) {
        draw_camera();
    }
    draw_scene(state.cube);
    draw_gaze(state);
    if (state.control_mode == ControlMode::Hands &&
        !state.gaze_calibration.visible) {
        draw_hands(state.gestures);
    }
    draw_hud(state);
    composite_hud();
    SDL_GL_SwapWindow(window_);
}

void Renderer::toggle_fullscreen() {
    set_fullscreen(!spanned_);
}

void Renderer::set_fullscreen(bool fullscreen) {
    if (fullscreen == spanned_) {
        int width = 0;
        int height = 0;
        SDL_GL_GetDrawableSize(window_, &width, &height);
        resize(width, height);
        return;
    }
    if (fullscreen) {
        SDL_GetWindowPosition(window_, &windowed_x_, &windowed_y_);
        SDL_GetWindowSize(window_, &windowed_width_, &windowed_height_);
        SDL_Rect span {};
        bool has_span = false;
        const int displays = SDL_GetNumVideoDisplays();
        for (int display = 0; display < displays; ++display) {
            SDL_Rect bounds {};
            if (SDL_GetDisplayBounds(display, &bounds) != 0) {
                continue;
            }
            if (!has_span) {
                span = bounds;
                has_span = true;
                continue;
            }
            const int right = std::max(span.x + span.w, bounds.x + bounds.w);
            const int bottom = std::max(span.y + span.h, bounds.y + bounds.h);
            span.x = std::min(span.x, bounds.x);
            span.y = std::min(span.y, bounds.y);
            span.w = right - span.x;
            span.h = bottom - span.y;
        }
        SDL_SetWindowFullscreen(window_, 0);
        SDL_SetWindowBordered(window_, SDL_FALSE);
        if (has_span) {
            set_x11_geometry(window_, true, span.x, span.y, span.w, span.h);
            SDL_SetWindowSize(window_, span.w, span.h);
            SDL_SetWindowPosition(window_, span.x, span.y);
            SDL_Log("Spanning %d displays at %dx%d%+d%+d",
                    displays,
                    span.w,
                    span.h,
                    span.x,
                    span.y);
        }
        SDL_RaiseWindow(window_);
        spanned_ = true;
    } else {
        SDL_SetWindowFullscreen(window_, 0);
        SDL_SetWindowBordered(window_, SDL_TRUE);
        if (!set_x11_geometry(window_,
                              false,
                              windowed_x_,
                              windowed_y_,
                              windowed_width_,
                              windowed_height_)) {
            SDL_SetWindowSize(window_, windowed_width_, windowed_height_);
            SDL_SetWindowPosition(window_, windowed_x_, windowed_y_);
        }
        spanned_ = false;
    }
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(window_, &width, &height);
    resize(width, height);
}

SDL_Window *Renderer::window() const {
    return window_;
}

const std::string &Renderer::error() const {
    return error_;
}

void Renderer::create_hud_surface() {
    if (hud_surface_) {
        cairo_surface_destroy(hud_surface_);
    }
    hud_surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width_, height_);
    glBindTexture(GL_TEXTURE_2D, hud_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 width_,
                 height_,
                 0,
                 GL_BGRA,
                 GL_UNSIGNED_BYTE,
                 nullptr);
}

void Renderer::upload_camera(const CameraFrame &frame) {
    camera_width_ = frame.width;
    camera_height_ = frame.height;
    camera_sequence_ = frame.sequence;
    glBindTexture(GL_TEXTURE_2D, camera_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGB,
                 frame.width,
                 frame.height,
                 0,
                 GL_RGB,
                 GL_UNSIGNED_BYTE,
                 frame.pixels.data());
}

void Renderer::update_trails(const SgGestureFrame &frame) {
    gesture_sequence_ = frame.sequence;
    for (auto &trail : trails_) {
        if (!trail.empty()) {
            trail.pop_front();
        }
    }
    for (uint32_t hand_index = 0; hand_index < frame.hand_count; ++hand_index) {
        const auto &hand = frame.hands[hand_index];
        for (size_t tip_index = 0; tip_index < kFingerTips.size(); ++tip_index) {
            auto &trail = trails_[hand_index * 5 + tip_index];
            const auto &point = hand.landmarks[kFingerTips[tip_index]].screen;
            if (!trail.empty()) {
                const auto &last = trail.back();
                const float dx = point.x - last.x;
                const float dy = point.y - last.y;
                if (std::hypot(dx, dy) > 0.18f) {
                    trail.clear();
                }
            }
            trail.push_back(point);
            while (trail.size() > 18) {
                trail.pop_front();
            }
        }
    }
}

void Renderer::update_gaze_trail(const SgGazeFrame &frame) {
    gaze_sequence_ = frame.sequence;
    if (!frame.present) {
        gaze_trail_.clear();
        return;
    }
    if (!gaze_trail_.empty()) {
        const auto &last = gaze_trail_.back();
        if (std::hypot(frame.screen.x - last.x,
                       frame.screen.y - last.y) > 0.28f) {
            gaze_trail_.clear();
        }
    }
    gaze_trail_.push_back(frame.screen);
    while (gaze_trail_.size() > 28) {
        gaze_trail_.pop_front();
    }
}

void Renderer::draw_camera() const {
    if (camera_width_ < 1 || camera_height_ < 1) {
        return;
    }
    begin_overlay(width_, height_);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, camera_texture_);
    glColor4f(0.72f, 0.76f, 0.84f, 0.32f);

    const float camera_aspect = static_cast<float>(camera_width_) / camera_height_;
    const float draw_width = std::min(static_cast<float>(width_),
                                      height_ * camera_aspect);
    const float draw_height = draw_width / camera_aspect;
    const float left = (width_ - draw_width) * 0.5f;
    const float top = (height_ - draw_height) * 0.5f;
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(left, top);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(left + draw_width, top);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(left + draw_width, top + draw_height);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(left, top + draw_height);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    glColor4f(0.012f, 0.017f, 0.030f, 0.56f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(static_cast<float>(width_), 0.0f);
    glColor4f(0.012f, 0.017f, 0.030f, 0.78f);
    glVertex2f(static_cast<float>(width_), static_cast<float>(height_));
    glVertex2f(0.0f, static_cast<float>(height_));
    glEnd();
}

void Renderer::draw_gaze(const RenderState &state) const {
    if (!state.presentation || state.control_mode != ControlMode::Eyes) {
        return;
    }
    const auto &frame = state.gaze;

    begin_overlay(width_, height_);
    const float unit = static_cast<float>(height_);
    const float sensor_x = width_ * 0.5f - unit * 0.58f;
    const float sensor_y = height_ * 0.46f;
    const float eye_spacing = unit * 0.15f;
    const float lens_radius = unit * 0.037f;
    const float alpha = frame.present ? 0.82f : 0.20f;
    const float left_eye_x = sensor_x - eye_spacing * 0.5f;
    const float right_eye_x = sensor_x + eye_spacing * 0.5f;
    ring(left_eye_x,
         sensor_y,
         lens_radius,
         0.54f,
         0.85f,
         1.0f,
         alpha);
    ring(right_eye_x,
         sensor_y,
         lens_radius,
         0.54f,
         0.85f,
         1.0f,
         alpha);
    ring(left_eye_x,
         sensor_y,
         lens_radius + 12.0f,
         0.21f,
         0.52f,
         0.89f,
         alpha * 0.25f);
    ring(right_eye_x,
         sensor_y,
         lens_radius + 12.0f,
         0.21f,
         0.52f,
         0.89f,
         alpha * 0.25f);

    if (!frame.present) {
        return;
    }

    const float left_pupil_x = left_eye_x +
        std::clamp(frame.features[2], -1.0f, 1.0f) * lens_radius * 0.42f;
    const float left_pupil_y = sensor_y +
        std::clamp(frame.features[3], -1.0f, 1.0f) * lens_radius * 0.42f;
    const float right_pupil_x = right_eye_x +
        std::clamp(frame.features[4], -1.0f, 1.0f) * lens_radius * 0.42f;
    const float right_pupil_y = sensor_y +
        std::clamp(frame.features[5], -1.0f, 1.0f) * lens_radius * 0.42f;
    const float iris_radius = unit * 0.017f;
    circle(left_pupil_x,
           left_pupil_y,
           iris_radius,
           0.24f,
           0.78f,
           1.0f,
           0.92f);
    circle(right_pupil_x,
           right_pupil_y,
           iris_radius,
           0.24f,
           0.78f,
           1.0f,
           0.92f);
    circle(left_pupil_x,
           left_pupil_y,
           iris_radius * 0.38f,
           0.02f,
           0.06f,
           0.10f,
           1.0f);
    circle(right_pupil_x,
           right_pupil_y,
           iris_radius * 0.38f,
           0.02f,
           0.06f,
           0.10f,
           1.0f);

    if (!gaze_pointer_visible(state)) {
        return;
    }

    const float gaze_x = frame.screen.x * width_;
    const float gaze_y = frame.screen.y * height_;
    glColor4f(0.42f, 0.84f, 1.0f, 0.16f);
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    glVertex2f(sensor_x, sensor_y);
    glVertex2f(gaze_x, gaze_y);
    glEnd();

    if (gaze_trail_.size() > 1) {
        glLineWidth(2.5f);
        glBegin(GL_LINE_STRIP);
        size_t index = 0;
        for (const auto &point : gaze_trail_) {
            const float trail_alpha = 0.04f +
                0.34f * static_cast<float>(index + 1) / gaze_trail_.size();
            glColor4f(0.45f, 0.86f, 1.0f, trail_alpha);
            glVertex2f(point.x * width_, point.y * height_);
            ++index;
        }
        glEnd();
    }
    circle(gaze_x, gaze_y, 7.0f, 0.54f, 0.91f, 1.0f, 1.0f);
    ring(gaze_x, gaze_y, 20.0f, 0.54f, 0.91f, 1.0f, 0.94f);
    ring(gaze_x, gaze_y, 34.0f, 0.54f, 0.91f, 1.0f, 0.22f);
    if (state.eye_control.enabled &&
        state.eye_control.dwell_progress > 0.0f) {
        progress_ring(gaze_x,
                      gaze_y,
                      28.0f,
                      state.eye_control.dwell_progress,
                      0.31f,
                      0.85f,
                      0.56f,
                      1.0f);
    }
}

void Renderer::draw_scene(const CubeState &cube) const {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(42.0, static_cast<double>(width_) / height_, 0.1, 30.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0.0, 0.0, 4.8, 0.0, -0.1, 0.0, 0.0, 1.0, 0.0);

    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int i = -8; i <= 8; ++i) {
        const float alpha = i == 0 ? 0.30f : 0.10f;
        glColor4f(0.32f, 0.67f, 0.94f, alpha);
        glVertex3f(i * 0.5f, -1.65f, -4.0f);
        glVertex3f(i * 0.5f, -1.65f, 2.0f);
        glVertex3f(-4.0f, -1.65f, i * 0.5f);
        glVertex3f(4.0f, -1.65f, i * 0.5f);
    }
    glEnd();

    if (cube.visible) {
        glColor4f(0.0f, 0.0f, 0.0f, 0.30f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(cube.position.x, -1.635f, cube.position.z);
        for (int i = 0; i <= 36; ++i) {
            const float angle = i * 2.0f * static_cast<float>(M_PI) / 36.0f;
            glColor4f(0.0f, 0.0f, 0.0f, i == 36 ? 0.0f : 0.10f);
            glVertex3f(cube.position.x + std::cos(angle) * cube.scale * 1.35f,
                       -1.635f,
                       cube.position.z + std::sin(angle) * cube.scale * 0.82f);
        }
        glEnd();
    }
    draw_cube(cube);
    draw_fragments(cube);
}

void Renderer::draw_cube(const CubeState &cube) const {
    if (!cube.visible) {
        return;
    }
    glPushMatrix();
    glTranslatef(cube.position.x, cube.position.y, cube.position.z);
    glRotatef(cube.rotation.x, 1.0f, 0.0f, 0.0f);
    glRotatef(cube.rotation.y, 0.0f, 1.0f, 0.0f);
    glRotatef(cube.rotation.z, 0.0f, 0.0f, 1.0f);
    solid_cube(cube.scale, 0.94f);
    glColor4f(0.78f, 0.93f, 1.0f, cube.grabbed ? 1.0f : 0.56f);
    glLineWidth(cube.grabbed ? 3.0f : 1.5f);
    const float h = cube.scale * 1.01f;
    glBegin(GL_LINES);
    const std::array<std::array<float, 3>, 8> vertices {{
        {{-h, -h, -h}}, {{h, -h, -h}}, {{h, h, -h}}, {{-h, h, -h}},
        {{-h, -h, h}}, {{h, -h, h}}, {{h, h, h}}, {{-h, h, h}},
    }};
    constexpr std::array<std::array<int, 2>, 12> edges {{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};
    for (const auto &edge : edges) {
        glVertex3fv(vertices[edge[0]].data());
        glVertex3fv(vertices[edge[1]].data());
    }
    glEnd();
    glPopMatrix();
}

void Renderer::draw_fragments(const CubeState &cube) const {
    for (const auto &fragment : cube.fragments) {
        const float alpha = std::clamp(fragment.life / 1.8f, 0.0f, 1.0f);
        glPushMatrix();
        glTranslatef(fragment.position.x, fragment.position.y, fragment.position.z);
        glRotatef(fragment.rotation.x, 1.0f, 0.0f, 0.0f);
        glRotatef(fragment.rotation.y, 0.0f, 1.0f, 0.0f);
        solid_cube(fragment.size, alpha);
        glPopMatrix();
    }
}

void Renderer::draw_hands(const SgGestureFrame &frame) const {
    begin_overlay(width_, height_);
    const float center_x = width_ * 0.5f;
    const float center_y = height_ * 0.5f;

    for (uint32_t hand_index = 0; hand_index < frame.hand_count; ++hand_index) {
        const auto &hand = frame.hands[hand_index];
        const bool left = hand.handedness == SG_HAND_LEFT;
        const float red = left ? 0.98f : 0.24f;
        const float green = left ? 0.51f : 0.78f;
        const float blue = left ? 0.24f : 1.0f;

        for (size_t tip_index = 0; tip_index < kFingerTips.size(); ++tip_index) {
            const auto &trail = trails_[hand_index * 5 + tip_index];
            if (trail.size() < 2) {
                continue;
            }
            glLineWidth(tip_index < 2 ? 3.0f : 1.5f);
            glBegin(GL_LINE_STRIP);
            size_t index = 0;
            for (const auto &point : trail) {
                const float alpha = 0.02f + 0.30f * static_cast<float>(index + 1) / trail.size();
                glColor4f(red, green, blue, alpha);
                glVertex2f(point.x * width_, point.y * height_);
                ++index;
            }
            glEnd();
        }

        glLineWidth(2.2f);
        glBegin(GL_LINES);
        for (const auto &connection : kHandConnections) {
            const auto &a = hand.landmarks[connection[0]].screen;
            const auto &b = hand.landmarks[connection[1]].screen;
            glColor4f(red, green, blue, 0.78f);
            glVertex2f(a.x * width_, a.y * height_);
            glVertex2f(b.x * width_, b.y * height_);
        }
        glEnd();

        for (int i = 0; i < SG_GESTURE_LANDMARK_COUNT; ++i) {
            const auto &point = hand.landmarks[i].screen;
            const float x = point.x * width_;
            const float y = point.y * height_;
            const float depth = std::clamp(point.z, 0.0f, 1.0f);
            const float shadow_x = x + (x - center_x) * depth * 0.055f;
            const float shadow_y = y + (y - center_y) * depth * 0.055f;
            glColor4f(red, green, blue, 0.18f);
            glLineWidth(1.0f);
            glBegin(GL_LINES);
            glVertex2f(x, y);
            glVertex2f(shadow_x, shadow_y);
            glEnd();
            ring(shadow_x, shadow_y, 3.0f + depth * 4.0f,
                 red, green, blue, 0.22f);

            const bool fingertip = std::find(kFingerTips.begin(),
                                             kFingerTips.end(),
                                             i) != kFingerTips.end();
            const bool active = hand.pinching && (i == 4 || i == 8);
            const float radius = active
                ? 11.0f + hand.pinch_strength * 5.0f
                : fingertip ? 7.0f + (1.0f - depth) * 3.0f : 3.5f;
            if (active) {
                circle(x, y, radius + 8.0f, red, green, blue, 0.12f);
            }
            circle(x, y, radius, red, green, blue, active ? 1.0f : 0.90f);
        }

        if (hand.pinch_strength > 0.15f) {
            const auto &thumb = hand.landmarks[4].screen;
            const auto &index = hand.landmarks[8].screen;
            glColor4f(red, green, blue, 0.35f + hand.pinch_strength * 0.65f);
            glLineWidth(2.0f + hand.pinch_strength * 4.0f);
            glBegin(GL_LINES);
            glVertex2f(thumb.x * width_, thumb.y * height_);
            glVertex2f(index.x * width_, index.y * height_);
            glEnd();
        }
    }

    if (frame.hand_count == 2 &&
        frame.hands[0].pinching && frame.hands[1].pinching) {
        SgVec3 points[2];
        for (int i = 0; i < 2; ++i) {
            const auto &thumb = frame.hands[i].landmarks[4].screen;
            const auto &index = frame.hands[i].landmarks[8].screen;
            points[i] = {(thumb.x + index.x) * 0.5f,
                         (thumb.y + index.y) * 0.5f,
                         (thumb.z + index.z) * 0.5f};
        }
        glColor4f(0.70f, 0.90f, 1.0f, 0.88f);
        glLineWidth(5.0f);
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(1, 0xF0F0);
        glBegin(GL_LINES);
        glVertex2f(points[0].x * width_, points[0].y * height_);
        glVertex2f(points[1].x * width_, points[1].y * height_);
        glEnd();
        glDisable(GL_LINE_STIPPLE);
        for (const auto &point : points) {
            circle(point.x * width_, point.y * height_, 18.0f,
                   0.56f, 0.85f, 1.0f, 0.16f);
            ring(point.x * width_, point.y * height_, 22.0f,
                 0.72f, 0.92f, 1.0f, 0.92f);
        }
    }
}

void Renderer::draw_hud(const RenderState &state) {
    auto *cr = cairo_create(hud_surface_);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (state.presentation) {
        text(cr,
             "SINGULARITY GESTURE LAB",
             28.0,
             24.0,
             420,
             "Sans Bold 15",
             0.90, 0.96, 1.0, 1.0);
        text(cr,
             state.control_mode == ControlMode::Select
                 ? "CHOOSE CONTROL MODE"
                 : state.control_mode == ControlMode::Eyes
                 ? "EYE CONTROL"
                 : "HAND CONTROL",
             28.0,
             49.0,
             420,
             "Sans SemiBold 9",
             0.36, 0.76, 1.0, 0.88);
        text(cr,
             "ON-DEVICE  |  CAMERA FRAMES ARE NOT RENDERED",
             width_ - 520.0,
             29.0,
             490,
             "Sans Bold 9",
             0.46, 0.82, 1.0, 0.90,
             PANGO_ALIGN_RIGHT);
    }

    const double pill_width = state.presentation ? 390.0 : 340.0;
    const bool primary_signal = state.control_mode == ControlMode::Eyes
        ? state.gaze.present
        : state.control_mode == ControlMode::Hands &&
            state.gestures.hand_count > 0;
    rounded_rectangle(cr, (width_ - pill_width) * 0.5, 22.0, pill_width, 48.0, 24.0);
    cairo_set_source_rgba(cr,
                          state.presentation ? 0.082 : 0.035,
                          state.presentation ? 0.208 : 0.045,
                          state.presentation ? 0.447 : 0.065,
                          state.presentation ? 0.78 : 0.88);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, state.presentation ? 0.60 : 0.28);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr,
                          primary_signal ? 0.33 : 0.95,
                          primary_signal ? 0.91 : 0.42,
                          0.48,
                          1.0);
    cairo_arc(cr, width_ * 0.5 - 138.0, 46.0, 4.5, 0.0, M_PI * 2.0);
    cairo_fill(cr);
    text(cr,
         state.control_mode == ControlMode::Select
             ? "Choose how to control the lab"
             : state.gaze_calibration.visible
             ? "Gaze calibration"
             : state.gaze_calibration.phase == GazeCalibrationPhase::Failed
             ? "Gaze calibration rejected"
             : state.eye_control.enabled
             ? (state.gaze.present ? "Eye-only control" : "Looking for eyes")
             : state.presentation
             ? (state.gestures.hand_count ? "Hand control active" : "Show your hands")
             : sg_gesture_kind_name(state.gestures.gesture),
         (width_ - pill_width) * 0.5 + 30.0,
         34.0,
         static_cast<int>(pill_width - 60.0),
         "Sans SemiBold 12",
         0.92, 0.95, 1.0, 1.0,
         PANGO_ALIGN_CENTER);

    if (state.control_mode == ControlMode::Select) {
        text(cr,
             state.gaze_calibration.phase == GazeCalibrationPhase::Failed
                 ? "Gaze calibration needs another pass"
                 : "How do you want to control the lab?",
             width_ * 0.5 - 420.0,
             height_ * 0.5 - 225.0,
             840,
             "Sans Bold 25",
             0.96, 0.98, 1.0, 1.0,
             PANGO_ALIGN_CENTER);
        text(cr,
             "Choose one mode. They never control the scene at the same time.",
             width_ * 0.5 - 440.0,
             height_ * 0.5 - 180.0,
             880,
             "Sans 12",
             0.68, 0.76, 0.86, 1.0,
             PANGO_ALIGN_CENTER);

        const double card_width = std::min(420.0, width_ * 0.32);
        const double card_height = 220.0;
        const double gap = 24.0;
        const double left = (width_ - card_width * 2.0 - gap) * 0.5;
        const double top = height_ * 0.5 - card_height * 0.5;
        for (int card = 0; card < 2; ++card) {
            const double x = left + card * (card_width + gap);
            rounded_rectangle(cr, x, top, card_width, card_height, 24.0);
            cairo_set_source_rgba(cr, 0.118, 0.118, 0.118, 0.96);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        const double hand_x = left + card_width * 0.5;
        const double icon_y = top + 72.0;
        rounded_rectangle(cr, hand_x - 30.0, icon_y - 20.0, 60.0, 62.0, 18.0);
        cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 0.86);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.48, 0.84, 1.0, 0.92);
        cairo_set_line_width(cr, 8.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        for (int finger = -2; finger <= 2; ++finger) {
            cairo_move_to(cr, hand_x + finger * 12.0, icon_y - 10.0);
            cairo_line_to(cr,
                          hand_x + finger * 12.0,
                          icon_y - 44.0 - (2 - std::abs(finger)) * 5.0);
        }
        cairo_stroke(cr);

        const double eyes_x = left + card_width + gap + card_width * 0.5;
        cairo_set_line_width(cr, 3.0);
        cairo_set_source_rgba(cr, 0.48, 0.84, 1.0, 0.94);
        cairo_arc(cr, eyes_x - 38.0, icon_y - 2.0, 27.0, 0.0, M_PI * 2.0);
        cairo_arc(cr, eyes_x + 38.0, icon_y - 2.0, 27.0, 0.0, M_PI * 2.0);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 1.0);
        cairo_arc(cr, eyes_x - 38.0, icon_y - 2.0, 10.0, 0.0, M_PI * 2.0);
        cairo_arc(cr, eyes_x + 38.0, icon_y - 2.0, 10.0, 0.0, M_PI * 2.0);
        cairo_fill(cr);

        text(cr,
             "Hands",
             left + 24.0,
             top + 132.0,
             static_cast<int>(card_width - 48.0),
             "Sans Bold 18",
             0.94, 0.97, 1.0, 1.0,
             PANGO_ALIGN_CENTER);
        text(cr,
             "Grab, move, scale and throw with hand gestures",
             left + 32.0,
             top + 170.0,
             static_cast<int>(card_width - 64.0),
             "Sans 10",
             0.66, 0.75, 0.85, 1.0,
             PANGO_ALIGN_CENTER);
        text(cr,
             "Eyes",
             left + card_width + gap + 24.0,
             top + 132.0,
             static_cast<int>(card_width - 48.0),
             "Sans Bold 18",
             0.94, 0.97, 1.0, 1.0,
             PANGO_ALIGN_CENTER);
        text(cr,
             "Calibrate gaze, then control with dwell",
             left + card_width + gap + 32.0,
             top + 170.0,
             static_cast<int>(card_width - 64.0),
             "Sans 10",
             0.66, 0.75, 0.85, 1.0,
             PANGO_ALIGN_CENTER);
        text(cr,
             "Click a mode, or press H / E",
             width_ * 0.5 - 220.0,
             top + card_height + 42.0,
             440,
             "Sans SemiBold 10",
             0.48, 0.80, 1.0, 0.92,
             PANGO_ALIGN_CENTER);
    } else if (state.gaze_calibration.visible) {
        cairo_set_source_rgba(cr, 0.075, 0.075, 0.075, 0.68);
        cairo_rectangle(cr, 0.0, 0.0, width_, height_);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
        cairo_set_line_width(cr, 1.0);
        for (int display = 1; display < display_count_; ++display) {
            const double x = width_ * display / display_count_;
            cairo_move_to(cr, x, 88.0);
            cairo_line_to(cr, x, height_ - 88.0);
        }
        cairo_stroke(cr);

        text(cr,
             state.gaze_calibration.title,
             width_ * 0.5 - 340.0,
             100.0,
             680,
             "Sans Bold 23",
             0.96, 0.98, 1.0, 1.0,
             PANGO_ALIGN_CENTER);
        text(cr,
             state.gaze_calibration.instruction,
             width_ * 0.5 - 500.0,
             140.0,
             1000,
             "Sans 12",
             0.72, 0.78, 0.86, 1.0,
             PANGO_ALIGN_CENTER);

        const double x = state.gaze_calibration.target_x * width_;
        const double y = state.gaze_calibration.target_y * height_;
        double pulse = 30.0;
        double red = 0.35;
        double green = 0.80;
        double blue = 1.0;
        double line_width = 2.0;
        if (state.gaze_calibration.sampling) {
            pulse = 44.0;
            red = 0.30;
            green = 0.88;
            blue = 0.58;
            line_width = 4.0;
        }
        cairo_set_line_width(cr, line_width);
        cairo_set_source_rgba(cr, red, green, blue, 0.72);
        cairo_arc(cr, x, y, pulse, 0.0, M_PI * 2.0);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, 0.42, 0.86, 1.0, 0.98);
        cairo_arc(cr, x, y, 10.0, 0.0, M_PI * 2.0);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.90);
        cairo_arc(cr, x, y, 3.0, 0.0, M_PI * 2.0);
        cairo_fill(cr);
        if (state.gaze.present && state.gaze.calibrated &&
            state.gaze_calibration.phase != GazeCalibrationPhase::Acquire) {
            const double estimate_x = std::clamp(state.gaze.screen.x,
                                                 0.0f,
                                                 1.0f) * width_;
            const double estimate_y = std::clamp(state.gaze.screen.y,
                                                 0.0f,
                                                 1.0f) * height_;
            const double dash[] = {6.0, 8.0};
            cairo_set_dash(cr, dash, 2, 0.0);
            cairo_set_line_width(cr, 1.5);
            cairo_set_source_rgba(cr, 0.82, 0.52, 1.0, 0.52);
            cairo_move_to(cr, x, y);
            cairo_line_to(cr, estimate_x, estimate_y);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0.0);
            cairo_set_line_width(cr, 2.5);
            cairo_set_source_rgba(cr, 0.82, 0.52, 1.0, 0.96);
            cairo_arc(cr, estimate_x, estimate_y, 17.0, 0.0, M_PI * 2.0);
            cairo_stroke(cr);
            cairo_move_to(cr, estimate_x - 8.0, estimate_y);
            cairo_line_to(cr, estimate_x + 8.0, estimate_y);
            cairo_move_to(cr, estimate_x, estimate_y - 8.0);
            cairo_line_to(cr, estimate_x, estimate_y + 8.0);
            cairo_stroke(cr);
            text(cr,
                 "GAZE ESTIMATE",
                 std::clamp(estimate_x - 80.0, 12.0, width_ - 172.0),
                 std::max(estimate_y - 42.0, 12.0),
                 160,
                 "Sans Bold 8",
                 0.84, 0.62, 1.0, 0.96,
                 PANGO_ALIGN_CENTER);
        }

        if (state.gaze_calibration.target_count > 1) {
            text(cr,
                 std::to_string(state.gaze_calibration.target_index + 1) +
                     " / " +
                     std::to_string(state.gaze_calibration.target_count),
                 width_ * 0.5 - 100.0,
                 height_ - 126.0,
                 200,
                 "Sans Bold 10",
                 0.52, 0.82, 1.0, 0.92,
                 PANGO_ALIGN_CENTER);
        }
        if (state.gaze_calibration.phase == GazeCalibrationPhase::Validate &&
            state.gaze_calibration.error_px > 0.0f) {
            text(cr,
                 "Current mean error: " +
                     std::to_string(static_cast<int>(std::round(
                         state.gaze_calibration.error_px))) + " px",
                 width_ * 0.5 - 150.0,
                 height_ - 104.0,
                 300,
                 "Sans 10",
                 0.68, 0.82, 0.92, 0.96,
                 PANGO_ALIGN_CENTER);
        }

        const double progress_width = 320.0;
        rounded_rectangle(cr,
                          width_ * 0.5 - progress_width * 0.5,
                          height_ - 76.0,
                          progress_width,
                          8.0,
                          4.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
        cairo_fill(cr);
        if (state.gaze_calibration.progress > 0.0f) {
            rounded_rectangle(cr,
                              width_ * 0.5 - progress_width * 0.5,
                              height_ - 76.0,
                              progress_width * state.gaze_calibration.progress,
                              8.0,
                              4.0);
            cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 1.0);
            cairo_fill(cr);
        }
    } else if (state.calibration.visible) {
        cairo_set_source_rgba(cr, 0.008, 0.012, 0.022, 0.46);
        cairo_rectangle(cr, 0.0, 0.0, width_, height_);
        cairo_fill(cr);

        text(cr,
             state.calibration.title,
             width_ * 0.5 - 300.0,
             96.0,
             600,
             "Sans Bold 23",
             0.96, 0.98, 1.0, 1.0,
             PANGO_ALIGN_CENTER);
        text(cr,
             state.calibration.instruction,
             width_ * 0.5 - 470.0,
             136.0,
             940,
             "Sans 12",
             0.72, 0.78, 0.86, 1.0,
             PANGO_ALIGN_CENTER);

        const double x = state.calibration.target_x * width_;
        const double y = state.calibration.target_y * height_;
        const double pulse = 18.0 + state.calibration.progress * 22.0;
        cairo_set_line_width(cr, 2.0);
        cairo_set_source_rgba(cr, 0.35, 0.80, 1.0, 0.40);
        cairo_arc(cr, x, y, pulse + 16.0, 0.0, M_PI * 2.0);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, 0.42, 0.86, 1.0, 0.95);
        cairo_arc(cr, x, y, 9.0, 0.0, M_PI * 2.0);
        cairo_fill(cr);

        const double progress_width = 280.0;
        rounded_rectangle(cr,
                          width_ * 0.5 - progress_width * 0.5,
                          height_ - 82.0,
                          progress_width,
                          8.0,
                          4.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
        cairo_fill(cr);
        if (state.calibration.progress > 0.0f) {
            rounded_rectangle(cr,
                              width_ * 0.5 - progress_width * 0.5,
                              height_ - 82.0,
                              progress_width * state.calibration.progress,
                              8.0,
                              4.0);
            cairo_set_source_rgba(cr, 0.30, 0.75, 1.0, 0.95);
            cairo_fill(cr);
        }
    } else if (state.presentation) {
        rounded_rectangle(cr, 28.0, 92.0, 246.0, 126.0, 18.0);
        cairo_set_source_rgba(cr, 0.118, 0.118, 0.118, 0.84);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        text(cr,
             "LIVE SIGNALS",
             48.0,
             108.0,
             180,
             "Sans Bold 9",
             0.52, 0.82, 1.0, 0.92);
        text(cr,
             state.gaze.present ? "●  Face landmarks" : "○  Face landmarks",
             48.0,
             136.0,
             190,
             "Sans 10",
             state.gaze.present ? 0.42 : 0.70,
             state.gaze.present ? 0.92 : 0.50,
             state.gaze.present ? 0.64 : 0.52,
             1.0);
        text(cr,
             state.gaze.present ? "●  Iris tracking" : "○  Iris tracking",
             48.0,
             160.0,
             190,
             "Sans 10",
             state.gaze.present ? 0.42 : 0.70,
             state.gaze.present ? 0.92 : 0.50,
             state.gaze.present ? 0.64 : 0.52,
             1.0);
        text(cr,
             state.gestures.hand_count ? "●  Hand landmarks" : "○  Hand landmarks",
             48.0,
             184.0,
             190,
             "Sans 10",
             state.gestures.hand_count ? 0.42 : 0.70,
             state.gestures.hand_count ? 0.92 : 0.50,
             state.gestures.hand_count ? 0.64 : 0.52,
             1.0);

        if (state.control_mode == ControlMode::Eyes &&
            !state.eye_control.grabbed && state.gaze.calibrated) {
            const double monitor_width = 126.0;
            const double monitor_height = 58.0;
            const double monitor_gap = 10.0;
            const double map_width = display_count_ * monitor_width +
                (display_count_ - 1) * monitor_gap;
            const double map_x = width_ * 0.5 - map_width * 0.5;
            const double map_y = height_ - 104.0;
            const double map_label_width = std::max(map_width, 280.0);
            text(cr,
                 "GAZE MAPPING  |  " + std::to_string(display_count_) + " DISPLAYS",
                 width_ * 0.5 - map_label_width * 0.5,
                 map_y - 25.0,
                 static_cast<int>(map_label_width),
                 "Sans Bold 9",
                 0.48, 0.80, 1.0, 0.88,
                 PANGO_ALIGN_CENTER);
            for (int display = 0; display < display_count_; ++display) {
                const double x = map_x +
                    display * (monitor_width + monitor_gap);
                rounded_rectangle(cr,
                                  x,
                                  map_y,
                                  monitor_width,
                                  monitor_height,
                                  8.0);
                cairo_set_source_rgba(cr, 0.118, 0.118, 0.118, 0.88);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);
            }
            if (state.gaze.present && state.gaze.calibrated) {
                const double scaled_x = std::clamp(state.gaze.screen.x,
                                                   0.0f,
                                                   0.9999f) * display_count_;
                const int display = std::clamp(static_cast<int>(scaled_x),
                                               0,
                                               display_count_ - 1);
                const double local_x = scaled_x - display;
                const double marker_x = map_x +
                    display * (monitor_width + monitor_gap) +
                    local_x * monitor_width;
                const double marker_y = map_y +
                    std::clamp(state.gaze.screen.y,
                               0.0f,
                               1.0f) * monitor_height;
                cairo_set_source_rgba(cr, 0.48, 0.90, 1.0, 1.0);
                cairo_arc(cr, marker_x, marker_y, 4.5, 0.0, M_PI * 2.0);
                cairo_fill(cr);
            }
        } else if (state.eye_control.grabbed) {
            const double release_x = state.eye_control.release_x * width_;
            const double release_y = state.eye_control.release_y * height_;
            rounded_rectangle(cr,
                              release_x - 130.0,
                              release_y - 24.0,
                              260.0,
                              48.0,
                              24.0);
            cairo_set_source_rgba(cr, 0.12, 0.32, 0.24, 0.94);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.30, 0.88, 0.58, 0.65);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
            text(cr,
                 "Look here to release",
                 release_x - 110.0,
                 release_y - 9.0,
                 220,
                 "Sans SemiBold 10",
                 0.90, 0.96, 1.0, 1.0,
                 PANGO_ALIGN_CENTER);
        }

        text(cr,
             state.control_mode == ControlMode::Eyes
                 ? "Dwell on the cube to grab  |  Look at release to drop  |  M: change mode"
                 : "Pinch to grab  |  Two pinches to scale  |  M: change mode",
             width_ * 0.5 - 410.0,
             height_ - 32.0,
             820,
             "Sans 10",
             0.68, 0.77, 0.87, 0.96,
             PANGO_ALIGN_CENTER);
    } else {
        rounded_rectangle(cr, 24.0, height_ - 66.0, width_ - 48.0, 42.0, 18.0);
        cairo_set_source_rgba(cr, 0.025, 0.034, 0.050, 0.74);
        cairo_fill(cr);
        text(cr,
             "Pinch to grab  |  Two pinches to scale and rotate  |  Fist then open to explode  |  Two open palms to reset",
             46.0,
             height_ - 56.0,
             width_ - 92,
             "Sans 10",
             0.70, 0.77, 0.86, 1.0,
             PANGO_ALIGN_CENTER);
    }

    if (!state.status.empty()) {
        text(cr,
             state.status,
             22.0,
             24.0,
             width_ / 3,
             "Sans 9",
             0.62, 0.69, 0.78, 0.92);
    }
    if (!state.presentation) {
        const std::string source = state.demo ? "DEMO" : "LOCAL CAMERA";
        text(cr,
             source + " | " + std::to_string(static_cast<int>(std::round(state.fps))) + " FPS",
             width_ - 180.0,
             28.0,
             150,
             "Sans Bold 9",
             0.52, 0.78, 0.96, 0.90,
             PANGO_ALIGN_RIGHT);
    }

    cairo_destroy(cr);
    cairo_surface_flush(hud_surface_);
    glBindTexture(GL_TEXTURE_2D, hud_texture_);
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    width_,
                    height_,
                    GL_BGRA,
                    GL_UNSIGNED_BYTE,
                    cairo_image_surface_get_data(hud_surface_));
}

void Renderer::composite_hud() const {
    begin_overlay(width_, height_);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, hud_texture_);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(static_cast<float>(width_), 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(static_cast<float>(width_), static_cast<float>(height_));
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, static_cast<float>(height_));
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

} // namespace sg::lab
