#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>

#include <SDL.h>

#include "lab/lab.hpp"

typedef struct _cairo_surface cairo_surface_t;

namespace sg::lab {

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool initialize();
    void resize(int width, int height);
    void render(const RenderState &state);
    void toggle_fullscreen();
    void set_fullscreen(bool fullscreen);
    SDL_Window *window() const;
    const std::string &error() const;

private:
    SDL_Window *window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    unsigned int camera_texture_ = 0;
    unsigned int hud_texture_ = 0;
    cairo_surface_t *hud_surface_ = nullptr;
    int width_ = 1280;
    int height_ = 800;
    int windowed_x_ = 0;
    int windowed_y_ = 0;
    int windowed_width_ = 1280;
    int windowed_height_ = 800;
    bool spanned_ = false;
    int camera_width_ = 0;
    int camera_height_ = 0;
    uint64_t camera_sequence_ = 0;
    uint64_t gesture_sequence_ = 0;
    uint64_t gaze_sequence_ = 0;
    int display_count_ = 1;
    std::string error_;
    std::array<std::deque<SgVec3>, SG_GESTURE_MAX_HANDS * 5> trails_;
    std::deque<SgVec3> gaze_trail_;

    void create_hud_surface();
    void upload_camera(const CameraFrame &frame);
    void update_trails(const SgGestureFrame &frame);
    void draw_camera() const;
    void update_gaze_trail(const SgGazeFrame &frame);
    void draw_gaze(const RenderState &state) const;
    void draw_scene(const CubeState &cube) const;
    void draw_cube(const CubeState &cube) const;
    void draw_fragments(const CubeState &cube) const;
    void draw_hands(const SgGestureFrame &frame) const;
    void draw_hud(const RenderState &state);
    void composite_hud() const;
};

} // namespace sg::lab
