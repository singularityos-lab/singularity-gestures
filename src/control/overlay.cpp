#include "control/overlay.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

#include <gtk4-layer-shell.h>
#include <pango/pangocairo.h>

namespace sg::control {

namespace {

constexpr std::array<std::array<int, 2>, 20> bones {{
    {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 4}},
    {{0, 5}}, {{5, 6}}, {{6, 7}}, {{7, 8}},
    {{5, 9}}, {{9, 10}}, {{10, 11}}, {{11, 12}},
    {{9, 13}}, {{13, 14}}, {{14, 15}}, {{15, 16}},
    {{13, 17}}, {{17, 18}}, {{18, 19}}, {{19, 20}},
}};

struct GestureGuideEntry {
    const char *action;
    const char *pose;
};

constexpr std::array<GestureGuideEntry, 15> gesture_guide {{
    {"Move pointer", "Point with the index finger"},
    {"Click", "Bend the pointed index, then raise it"},
    {"Drag", "Hold a thumb-index pinch and move"},
    {"Right click", "Hold a thumb-middle pinch"},
    {"Scroll", "Move joined index and middle fingers"},
    {"Switch windows", "Slap an open palm left or right"},
    {"Pause or resume", "Hold two open palms"},
    {"Scrolling tiling", "Slide joined index and middle fingers"},
    {"Switch workspace", "Slide three joined fingers"},
    {"Overview", "Make a fist, then open it"},
    {"Full screenshot", "Frame a camera with both hands"},
    {"Region screenshot", "Frame with two pinches, then release"},
    {"Paste", "Pull a right pinch from the left palm"},
    {"Lock", "Bring two fists together and hold"},
    {"Gesture guide", "Point at the center of the open left palm"},
}};

int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
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
    cairo_arc(cr, right - radius, y + radius, radius,
              -M_PI_2, 0.0);
    cairo_arc(cr, right - radius, bottom - radius, radius,
              0.0, M_PI_2);
    cairo_arc(cr, x + radius, bottom - radius, radius,
              M_PI_2, M_PI);
    cairo_arc(cr, x + radius, y + radius, radius,
              M_PI, M_PI * 1.5);
    cairo_close_path(cr);
}

void draw_text(cairo_t *cr,
               const std::string &text,
               double x,
               double y,
               int size,
               bool bold,
               const GdkRGBA &color,
               double alpha = 1.0) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text.c_str(), -1);
    PangoFontDescription *font = pango_font_description_new();
    pango_font_description_set_family(font, "Inter, sans-serif");
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    pango_font_description_set_weight(
        font, bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue,
                          color.alpha * alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
}

struct ShellPalette {
    GdkRGBA surface;
    GdkRGBA text;
    GdkRGBA accent;
    GdkRGBA destructive;
};

GSettings *desktop_settings() {
    static GSettings *settings = [] {
        GSettingsSchemaSource *source = g_settings_schema_source_get_default();
        if (!source) {
            return static_cast<GSettings *>(nullptr);
        }
        GSettingsSchema *schema = g_settings_schema_source_lookup(
            source, "dev.sinty.desktop", TRUE);
        if (!schema) {
            return static_cast<GSettings *>(nullptr);
        }
        GSettings *result = g_settings_new_full(schema, nullptr, nullptr);
        g_settings_schema_unref(schema);
        return result;
    }();
    return settings;
}

GdkRGBA parse_color(const char *value, const GdkRGBA &fallback) {
    GdkRGBA color {};
    return value && gdk_rgba_parse(&color, value) ? color : fallback;
}

ShellPalette shell_palette() {
    static constexpr std::array<std::array<const char *, 2>, 9> accents {{
        {{"blue", "#3584e4"}},
        {{"teal", "#2190a4"}},
        {{"green", "#3a944a"}},
        {{"yellow", "#e5a50a"}},
        {{"orange", "#e66100"}},
        {{"red", "#e01b24"}},
        {{"pink", "#d56199"}},
        {{"purple", "#9141ac"}},
        {{"slate", "#787878"}},
    }};
    static ShellPalette palette {
        {30.0 / 255.0, 30.0 / 255.0, 30.0 / 255.0, 0.70},
        {1.0, 1.0, 1.0, 1.0},
        {0.21, 0.52, 0.89, 1.0},
        {1.0, 0.23, 0.19, 1.0},
    };
    static int64_t updated_ms = 0;
    const int64_t now_ms = monotonic_ms();
    if (now_ms - updated_ms < 1000) {
        return palette;
    }
    updated_ms = now_ms;

    GSettings *settings = desktop_settings();
    if (!settings) {
        return palette;
    }
    gchar *mode = g_settings_get_string(settings, "theme-mode");
    const bool dark = g_strcmp0(mode, "light") != 0;
    g_free(mode);
    palette.surface = dark
        ? GdkRGBA {30.0 / 255.0, 30.0 / 255.0, 30.0 / 255.0, 0.70}
        : GdkRGBA {245.0 / 255.0, 245.0 / 255.0,
                   245.0 / 255.0, 0.85};
    palette.text = dark
        ? GdkRGBA {1.0, 1.0, 1.0, 1.0}
        : GdkRGBA {26.0 / 255.0, 26.0 / 255.0,
                   26.0 / 255.0, 1.0};
    palette.destructive = dark
        ? parse_color("#ff3b30", {})
        : parse_color("#e01b24", {});

    gchar *accent_name = g_settings_get_string(settings, "accent-color");
    const char *accent_value = "#3584e4";
    gchar *custom = nullptr;
    if (g_strcmp0(accent_name, "custom") == 0) {
        custom = g_settings_get_string(settings, "custom-accent-color");
        accent_value = custom;
    } else {
        for (const auto &accent : accents) {
            if (g_strcmp0(accent_name, accent[0]) == 0) {
                accent_value = accent[1];
                break;
            }
        }
    }
    palette.accent = parse_color(accent_value,
                                 {0.21, 0.52, 0.89, 1.0});
    g_free(custom);
    g_free(accent_name);
    return palette;
}

void set_source(cairo_t *cr, const GdkRGBA &color, double alpha = 1.0) {
    cairo_set_source_rgba(cr, color.red, color.green, color.blue,
                          color.alpha * alpha);
}

void draw_shell_surface(cairo_t *cr,
                        const ShellPalette &palette,
                        double x,
                        double y,
                        double width,
                        double height,
                        double radius,
                        const GdkRGBA &border) {
    rounded_rectangle(cr, x, y, width, height, radius);
    set_source(cr, palette.surface);
    cairo_fill_preserve(cr);
    set_source(cr, border, 0.12);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
}

} // namespace

OverlayApplication::OverlayApplication(std::string runtime_dir)
    : runtime_dir_(std::move(runtime_dir)) {
    application_ = gtk_application_new(
        "dev.sinty.HandControl",
        static_cast<GApplicationFlags>(G_APPLICATION_HANDLES_COMMAND_LINE));
    g_signal_connect(application_, "activate",
                     G_CALLBACK(on_activate), this);
    g_signal_connect(application_, "command-line",
                     G_CALLBACK(on_command_line), this);
}

OverlayApplication::~OverlayApplication() {
    if (tick_source_) {
        g_source_remove(tick_source_);
    }
    if (capture_timeout_) {
        g_source_remove(capture_timeout_);
    }
    if (show_timeout_) {
        g_source_remove(show_timeout_);
    }
    if (monitor_handler_) {
        GdkDisplay *display = gdk_display_get_default();
        if (display) {
            GListModel *monitors = gdk_display_get_monitors(display);
            g_signal_handler_disconnect(monitors, monitor_handler_);
        }
    }
    controller_.reset();
    destroy_windows();
    g_clear_object(&application_);
}

int OverlayApplication::run(int argc, char **argv) {
    return g_application_run(G_APPLICATION(application_), argc, argv);
}

void OverlayApplication::activate() {
    if (controller_) {
        return;
    }
    create_windows();
    controller_ = std::make_unique<HandController>(runtime_dir_);
    std::string error;
    if (!controller_->start(desktop_.width, desktop_.height, error)) {
        startup_error_ = std::move(error);
    }
    tick_source_ = g_timeout_add(16, on_tick, this);

    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        GListModel *monitors = gdk_display_get_monitors(display);
        monitor_handler_ = g_signal_connect(monitors, "items-changed",
                                            G_CALLBACK(on_monitors_changed), this);
    }
}

int OverlayApplication::command_line(
    GApplicationCommandLine *command_line) {
    int argc = 0;
    gchar **argv = g_application_command_line_get_arguments(command_line, &argc);
    bool calibrate = false;
    bool quit = false;
    for (int i = 1; i < argc; ++i) {
        calibrate = calibrate || g_strcmp0(argv[i], "--calibrate") == 0;
        quit = quit || g_strcmp0(argv[i], "--quit") == 0;
    }
    g_strfreev(argv);
    if (quit) {
        g_application_quit(G_APPLICATION(application_));
        return 0;
    }
    g_application_activate(G_APPLICATION(application_));
    if (calibrate) {
        reset_calibration();
    }
    return 0;
}

void OverlayApplication::create_windows() {
    destroy_windows();
    desktop_ = {0, 0, 1, 1};
    GdkDisplay *display = gdk_display_get_default();
    if (!display) {
        return;
    }
    GListModel *monitors = gdk_display_get_monitors(display);
    const guint count = g_list_model_get_n_items(monitors);
    bool first = true;
    for (guint i = 0; i < count; ++i) {
        GdkMonitor *monitor = GDK_MONITOR(g_list_model_get_item(monitors, i));
        GdkRectangle geometry {};
        gdk_monitor_get_geometry(monitor, &geometry);
        if (first) {
            desktop_ = geometry;
            first = false;
        } else {
            const int left = std::min(desktop_.x, geometry.x);
            const int top = std::min(desktop_.y, geometry.y);
            const int right = std::max(desktop_.x + desktop_.width,
                                       geometry.x + geometry.width);
            const int bottom = std::max(desktop_.y + desktop_.height,
                                        geometry.y + geometry.height);
            desktop_ = {left, top, right - left, bottom - top};
        }

        auto item = std::make_unique<OverlayWindow>();
        item->owner = this;
        item->monitor = geometry;
        item->window = GTK_WINDOW(gtk_application_window_new(application_));
        gtk_window_set_decorated(item->window, false);
        gtk_widget_add_css_class(GTK_WIDGET(item->window), "hand-control-overlay");
        gtk_layer_init_for_window(item->window);
        gtk_layer_set_namespace(item->window, "singularity-hand-control");
        gtk_layer_set_layer(item->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_monitor(item->window, monitor);
        gtk_layer_set_exclusive_zone(item->window, -1);
        gtk_layer_set_keyboard_mode(item->window,
                                    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        for (int edge = 0; edge < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; ++edge) {
            gtk_layer_set_anchor(item->window,
                static_cast<GtkLayerShellEdge>(edge), true);
        }
        item->area = gtk_drawing_area_new();
        gtk_widget_set_hexpand(item->area, true);
        gtk_widget_set_vexpand(item->area, true);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(item->area),
                                       on_draw, item.get(), nullptr);
        gtk_window_set_child(item->window, item->area);
        g_signal_connect(item->window, "realize",
                         G_CALLBACK(on_realize), item.get());
        gtk_window_present(item->window);
        windows_.push_back(std::move(item));
        g_object_unref(monitor);
    }

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        provider, ".hand-control-overlay { background: transparent; }");
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void OverlayApplication::destroy_windows() {
    for (auto &item : windows_) {
        gtk_window_destroy(item->window);
    }
    windows_.clear();
}

void OverlayApplication::reset_calibration() {
    if (!controller_) {
        return;
    }
    const float aspect = static_cast<float>(desktop_.width) /
        std::max(desktop_.height, 1);
    controller_->reset_calibration(aspect);
}

void OverlayApplication::tick() {
    const int64_t now_ms = monotonic_ms();
    if (controller_) {
        controller_->tick(now_ms);
        const auto state = controller_->snapshot();
        update_toy(state, now_ms);
        schedule_screenshot(state);
    }
    for (const auto &item : windows_) {
        gtk_widget_queue_draw(item->area);
    }
}

void OverlayApplication::schedule_screenshot(
    const ControllerSnapshot &state) {
    if (state.screenshot_serial == screenshot_serial_) {
        return;
    }
    screenshot_serial_ = state.screenshot_serial;
    pending_fullscreen_ = state.screenshot_fullscreen;
    const float left = std::min(state.screenshot_ax, state.screenshot_bx);
    const float top = std::min(state.screenshot_ay, state.screenshot_by);
    const float right = std::max(state.screenshot_ax, state.screenshot_bx);
    const float bottom = std::max(state.screenshot_ay, state.screenshot_by);
    pending_x_ = desktop_.x + static_cast<int>(left * desktop_.width);
    pending_y_ = desktop_.y + static_cast<int>(top * desktop_.height);
    pending_width_ = std::max(1,
        static_cast<int>((right - left) * desktop_.width));
    pending_height_ = std::max(1,
        static_cast<int>((bottom - top) * desktop_.height));

    for (const auto &item : windows_) {
        gtk_widget_set_visible(GTK_WIDGET(item->window), false);
    }
    if (capture_timeout_) {
        g_source_remove(capture_timeout_);
    }
    capture_timeout_ = g_timeout_add(120, on_capture_timeout, this);
}

void OverlayApplication::capture_screenshot() {
    GError *error = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (bus) {
        const char *method = pending_fullscreen_
            ? "CaptureFullscreen" : "CaptureRegion";
        GVariant *parameters = pending_fullscreen_ ? nullptr
            : g_variant_new("(iiii)", pending_x_, pending_y_,
                            pending_width_, pending_height_);
        g_dbus_connection_call(
            bus,
            "dev.sinty.desktop",
            "/dev/sinty/desktop/Shortcuts",
            "dev.sinty.desktop.Shortcuts",
            method,
            parameters,
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            1000,
            nullptr,
            nullptr,
            nullptr);
        g_object_unref(bus);
    }
    g_clear_error(&error);
    if (show_timeout_) {
        g_source_remove(show_timeout_);
    }
    show_timeout_ = g_timeout_add(650, on_show_timeout, this);
}

void OverlayApplication::show_overlays() {
    for (const auto &item : windows_) {
        gtk_window_present(item->window);
    }
}

void OverlayApplication::update_toy(const ControllerSnapshot &state,
                                    int64_t now_ms) {
    ToyFrame frame;
    frame.paused = state.paused;
    std::array<bool, 2> occupied {};
    for (uint32_t i = 0; i < state.frame.hand_count; ++i) {
        const auto &hand = state.frame.hands[i];
        int slot = hand.handedness == SG_HAND_RIGHT ? 1 : 0;
        if (occupied[slot]) {
            slot = 1 - slot;
        }
        occupied[slot] = true;
        const auto &thumb = hand.landmarks[4].screen;
        const auto &index = hand.landmarks[8].screen;
        frame.hands[slot].present = hand.present;
        frame.hands[slot].pinch_strength = hand.pinch_strength;
        frame.hands[slot].x = (thumb.x + index.x) * 0.5f;
        frame.hands[slot].y = (thumb.y + index.y) * 0.5f;
    }
    toy_.update(frame, now_ms, desktop_.width, desktop_.height);
}

void OverlayApplication::draw(OverlayWindow &window,
                              cairo_t *cr,
                              int,
                              int) {
    if (!controller_) {
        return;
    }
    const auto state = controller_->snapshot();
    const ShellPalette palette = shell_palette();
    const auto point = [this, &window](const SgVec3 &value) {
        return std::array<double, 2> {
            desktop_.x + value.x * desktop_.width - window.monitor.x,
            desktop_.y + value.y * desktop_.height - window.monitor.y,
        };
    };

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (uint32_t h = 0; h < state.frame.hand_count; ++h) {
        const auto &hand = state.frame.hands[h];
        if (state.lock_gesture_active) {
            continue;
        }
        cairo_set_line_width(cr, 3.0);
        set_source(cr, palette.accent, 0.68);
        for (const auto &bone : bones) {
            const auto a = point(hand.landmarks[bone[0]].screen);
            const auto b = point(hand.landmarks[bone[1]].screen);
            cairo_move_to(cr, a[0], a[1]);
            cairo_line_to(cr, b[0], b[1]);
        }
        cairo_stroke(cr);

        for (int i = 0; i < SG_GESTURE_LANDMARK_COUNT; ++i) {
            const auto p = point(hand.landmarks[i].screen);
            const bool fingertip = i == 4 || i == 8 || i == 12 ||
                i == 16 || i == 20;
            const double radius = fingertip ? 8.0 : 4.5;
            cairo_arc(cr, p[0], p[1], radius, 0.0, M_PI * 2.0);
            if (i == 8) {
                cairo_set_source_rgba(cr, 0.73, 0.35, 1.0, 0.95);
            } else {
                cairo_set_source_rgba(cr, 0.30, 0.86, 1.0,
                                      fingertip ? 0.92 : 0.75);
            }
            cairo_fill(cr);
        }

        if (hand.pinching && !state.paused) {
            const auto thumb = point(hand.landmarks[4].screen);
            const auto index = point(hand.landmarks[8].screen);
            cairo_arc(cr, (thumb[0] + index[0]) * 0.5,
                      (thumb[1] + index[1]) * 0.5,
                      18.0, 0.0, M_PI * 2.0);
            cairo_set_line_width(cr, 3.0);
            set_source(cr, palette.accent, 0.95);
            cairo_stroke(cr);
        }
    }

    if (state.lock_gesture_active) {
        const auto a = point({state.lock_ax, state.lock_ay, 0.0f});
        const auto b = point({state.lock_bx, state.lock_by, 0.0f});
        cairo_move_to(cr, a[0], a[1]);
        cairo_line_to(cr, b[0], b[1]);
        cairo_set_line_width(cr, 5.0);
        set_source(cr, palette.accent, 0.72);
        cairo_stroke(cr);
        for (const auto &center : {a, b}) {
            cairo_arc(cr, center[0], center[1], 18.0,
                      0.0, M_PI * 2.0);
            set_source(cr, palette.surface);
            cairo_fill_preserve(cr);
            set_source(cr, palette.accent, 0.98);
            cairo_set_line_width(cr, 3.0);
            cairo_stroke(cr);
        }
    }

    if (state.screenshot_region_active) {
        const auto a = point({state.screenshot_ax, state.screenshot_ay, 0.0f});
        const auto b = point({state.screenshot_bx, state.screenshot_by, 0.0f});
        const double x = std::min(a[0], b[0]);
        const double y = std::min(a[1], b[1]);
        const double width = std::abs(b[0] - a[0]);
        const double height = std::abs(b[1] - a[1]);
        cairo_rectangle(cr, x, y, width, height);
        set_source(cr, palette.accent, 0.10);
        cairo_fill_preserve(cr);
        set_source(cr, palette.accent, 0.96);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
        for (const auto &corner : {a, b}) {
            cairo_arc(cr, corner[0], corner[1], 7.0, 0.0, M_PI * 2.0);
            set_source(cr, palette.text, 0.98);
            cairo_fill(cr);
        }
    }

    if (state.clipboard_active) {
        const auto anchor = point({state.clipboard_anchor_x,
                                   state.clipboard_anchor_y, 0.0f});
        const auto cursor = point({state.clipboard_cursor_x,
                                   state.clipboard_cursor_y, 0.0f});
        cairo_move_to(cr, anchor[0], anchor[1]);
        cairo_line_to(cr, cursor[0], cursor[1]);
        cairo_set_line_width(cr, 3.0);
        set_source(cr, palette.accent, 0.88);
        cairo_stroke(cr);

        rounded_rectangle(cr, anchor[0] - 15.0, anchor[1] - 18.0,
                          30.0, 36.0, 7.0);
        set_source(cr, palette.surface);
        cairo_fill_preserve(cr);
        set_source(cr, palette.accent, 0.92);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
        rounded_rectangle(cr, anchor[0] - 7.0, anchor[1] - 22.0,
                          14.0, 8.0, 3.0);
        set_source(cr, palette.accent, 0.96);
        cairo_fill(cr);

        cairo_arc(cr, cursor[0], cursor[1], 9.0, 0.0, M_PI * 2.0);
        set_source(cr, palette.text, 0.98);
        cairo_fill(cr);
    }

    const ToyState &toy = toy_.state();
    if (state.paused && toy.active && !state.guide_visible) {
        if (toy.stretching) {
            const auto a = point({toy.string_ax, toy.string_ay, 0.0f});
            const auto b = point({toy.string_bx, toy.string_by, 0.0f});
            cairo_move_to(cr, a[0], a[1]);
            cairo_line_to(cr, b[0], b[1]);
            cairo_set_line_width(cr, 3.0);
            set_source(cr, palette.text, 0.78);
            cairo_stroke(cr);
            for (const auto &end : {a, b}) {
                cairo_arc(cr, end[0], end[1], 6.0, 0.0, M_PI * 2.0);
                set_source(cr, palette.text, 0.96);
                cairo_fill(cr);
            }
        }

        SgVec3 ball {toy.x, toy.y, 0.0f};
        const auto p = point(ball);
        const double margin = toy.radius + 4.0;
        if (p[0] > -margin && p[0] < window.monitor.width + margin &&
            p[1] > -margin && p[1] < window.monitor.height + margin) {
            cairo_arc(cr, p[0], p[1], toy.radius, 0.0, M_PI * 2.0);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.98);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.20);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
        }
    }

    if (!state.calibrated) {
        SgVec3 target {state.calibration.target_x,
                       state.calibration.target_y, 0.0f};
        if (state.calibration.phase != CalibrationPhase::Screen) {
            target = {0.5f, 0.5f, 0.0f};
        }
        const auto target_point = point(target);
        const bool target_here = target_point[0] >= 0.0 &&
            target_point[0] <= window.monitor.width &&
            target_point[1] >= 0.0 &&
            target_point[1] <= window.monitor.height;
        if (target_here) {
            const double pulse = 4.0 +
                std::sin(monotonic_ms() / 180.0) * 3.0;
            cairo_arc(cr, target_point[0], target_point[1], 25.0 + pulse,
                      0.0, M_PI * 2.0);
            set_source(cr, palette.accent, 0.25);
            cairo_fill(cr);
            cairo_arc(cr, target_point[0], target_point[1], 16.0,
                      0.0, M_PI * 2.0);
            set_source(cr, palette.accent, 0.98);
            cairo_fill(cr);

            const double card_width = std::min(500.0,
                window.monitor.width - 48.0);
            const double card_x = std::clamp(
                target_point[0] - card_width * 0.5,
                24.0, window.monitor.width - card_width - 24.0);
            const double card_y = target_point[1] < window.monitor.height * 0.45
                ? window.monitor.height - 154.0 : 34.0;
            draw_shell_surface(cr, palette, card_x, card_y,
                               card_width, 120.0, 16.0, palette.text);
            draw_text(cr, state.calibration.title,
                      card_x + 24.0, card_y + 18.0, 18, true, palette.text);
            draw_text(cr, state.calibration.instruction,
                      card_x + 24.0, card_y + 50.0, 13, false,
                      palette.text, 0.72);
            rounded_rectangle(cr, card_x + 24.0, card_y + 88.0,
                              card_width - 48.0, 6.0, 3.0);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
            cairo_fill(cr);
            rounded_rectangle(cr, card_x + 24.0, card_y + 88.0,
                              (card_width - 48.0) * state.calibration.progress,
                              6.0, 3.0);
            set_source(cr, palette.accent, 0.95);
            cairo_fill(cr);
        }
    } else if (state.guide_visible) {
        SgVec3 center {0.5f, 0.5f, 0.0f};
        const auto p = point(center);
        const bool center_here = p[0] >= 0.0 &&
            p[0] <= window.monitor.width &&
            p[1] >= 0.0 && p[1] <= window.monitor.height;
        if (center_here) {
            const double card_width = std::min(
                1080.0, window.monitor.width - 48.0);
            constexpr double card_height = 486.0;
            const double card_x = p[0] - card_width * 0.5;
            const double card_y = p[1] - card_height * 0.5;
            const double column_gap = 44.0;
            const double column_width =
                (card_width - 92.0 - column_gap) * 0.5;
            draw_shell_surface(cr, palette, card_x, card_y,
                               card_width, card_height, 24.0, palette.text);
            draw_text(cr, "Hand gestures", card_x + 32.0, card_y + 24.0,
                      22, true, palette.text);
            draw_text(cr,
                      "Point at your open left palm again to close this guide",
                      card_x + 32.0, card_y + 58.0,
                      13, false, palette.text, 0.68);

            const double divider_x = card_x + card_width * 0.5;
            cairo_move_to(cr, divider_x, card_y + 96.0);
            cairo_line_to(cr, divider_x, card_y + card_height - 30.0);
            set_source(cr, palette.text, 0.10);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            for (size_t i = 0; i < gesture_guide.size(); ++i) {
                const bool right_column = i >= 8;
                const size_t row = right_column ? i - 8 : i;
                const double x = card_x + 32.0 +
                    (right_column ? column_width + column_gap : 0.0);
                const double y = card_y + 102.0 + row * 43.0;
                cairo_arc(cr, x + 4.0, y + 8.0, 3.5,
                          0.0, M_PI * 2.0);
                set_source(cr, palette.accent, 0.96);
                cairo_fill(cr);
                draw_text(cr, gesture_guide[i].action,
                          x + 16.0, y, 13, true, palette.text, 0.94);
                draw_text(cr, gesture_guide[i].pose,
                          x + 16.0, y + 19.0, 11, false,
                          palette.text, 0.62);
            }

            if (state.gesture_progress > 0.0f) {
                rounded_rectangle(cr, card_x + 32.0,
                                  card_y + card_height - 16.0,
                                  card_width - 64.0, 4.0, 2.0);
                set_source(cr, palette.text, 0.10);
                cairo_fill(cr);
                rounded_rectangle(cr, card_x + 32.0,
                                  card_y + card_height - 16.0,
                                  (card_width - 64.0) *
                                      state.gesture_progress,
                                  4.0, 2.0);
                set_source(cr, palette.accent, 0.96);
                cairo_fill(cr);
            }
        }
    } else if (state.frame.hand_count > 0) {
        const auto anchor = point(state.frame.hands[0].landmarks[0].screen);
        if (anchor[0] > -180.0 && anchor[0] < window.monitor.width + 180.0 &&
            anchor[1] > -80.0 && anchor[1] < window.monitor.height + 80.0) {
            const std::string label = hand_action_name(state.action);
            constexpr double action_width = 230.0;
            draw_shell_surface(cr, palette,
                               anchor[0] + 18.0, anchor[1] + 18.0,
                               action_width, 42.0, 21.0, palette.text);
            draw_text(cr, label, anchor[0] + 36.0, anchor[1] + 27.0,
                      12, true, palette.text, 0.92);
            if (state.gesture_progress > 0.0f) {
                rounded_rectangle(cr, anchor[0] + 36.0, anchor[1] + 51.0,
                                  194.0, 3.0, 1.5);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
                cairo_fill(cr);
                rounded_rectangle(cr, anchor[0] + 36.0, anchor[1] + 51.0,
                                  194.0 * state.gesture_progress, 3.0, 1.5);
                set_source(cr, palette.accent, 0.95);
                cairo_fill(cr);
            }
        }
    }

    if (state.paused && state.pause_progress == 0.0f &&
        !state.guide_visible) {
        SgVec3 top_center {0.5f, 0.035f, 0.0f};
        const auto p = point(top_center);
        if (p[0] >= 0.0 && p[0] <= window.monitor.width &&
            p[1] >= 0.0 && p[1] <= window.monitor.height) {
            draw_shell_surface(cr, palette, p[0] - 270.0, p[1] - 18.0,
                               540.0, 48.0, 24.0, palette.text);
            draw_text(cr,
                      "Paused - pinch to play; two pinches resize; two palms resume",
                      p[0] - 243.0, p[1] - 3.0, 13, true,
                      palette.text, 0.92);
        }
    }

    if (state.pause_progress > 0.0f && !state.guide_visible) {
        SgVec3 center {0.5f, 0.075f, 0.0f};
        const auto p = point(center);
        if (p[0] >= 0.0 && p[0] <= window.monitor.width &&
            p[1] >= 0.0 && p[1] <= window.monitor.height) {
            draw_shell_surface(cr, palette, p[0] - 170.0, p[1] - 20.0,
                               340.0, 48.0, 24.0, palette.text);
            rounded_rectangle(cr, p[0] - 148.0, p[1] + 14.0,
                              296.0, 4.0, 2.0);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
            cairo_fill(cr);
            rounded_rectangle(cr, p[0] - 148.0, p[1] + 14.0,
                              296.0 * state.pause_progress, 4.0, 2.0);
            set_source(cr, palette.accent, 0.95);
            cairo_fill(cr);
            const std::string instruction = state.pause_target_enabled
                ? "Keep both palms open to pause"
                : "Keep both palms open to resume";
            draw_text(cr, instruction,
                      p[0] - 125.0, p[1] - 8.0, 13, true,
                      palette.text, 0.92);
        }
    }

    if (!startup_error_.empty()) {
        draw_shell_surface(cr, palette, 24.0, 24.0,
                           520.0, 72.0, 16.0, palette.destructive);
        draw_text(cr, "Hand control could not start", 48.0, 38.0,
                  16, true, palette.text);
        draw_text(cr, startup_error_, 48.0, 64.0,
                  12, false, palette.text, 0.74);
    }
}

void OverlayApplication::on_activate(GApplication *, void *data) {
    static_cast<OverlayApplication *>(data)->activate();
}

int OverlayApplication::on_command_line(
    GApplication *, GApplicationCommandLine *command_line, void *data) {
    return static_cast<OverlayApplication *>(data)->command_line(command_line);
}

gboolean OverlayApplication::on_tick(void *data) {
    static_cast<OverlayApplication *>(data)->tick();
    return G_SOURCE_CONTINUE;
}

gboolean OverlayApplication::on_capture_timeout(void *data) {
    auto *self = static_cast<OverlayApplication *>(data);
    self->capture_timeout_ = 0;
    self->capture_screenshot();
    return G_SOURCE_REMOVE;
}

gboolean OverlayApplication::on_show_timeout(void *data) {
    auto *self = static_cast<OverlayApplication *>(data);
    self->show_timeout_ = 0;
    self->show_overlays();
    return G_SOURCE_REMOVE;
}

void OverlayApplication::on_monitors_changed(
    GListModel *, guint, guint, guint, void *data) {
    auto *self = static_cast<OverlayApplication *>(data);
    self->create_windows();
    if (self->controller_) {
        self->reset_calibration();
    }
}

void OverlayApplication::on_draw(
    GtkDrawingArea *, cairo_t *cr, int width, int height, void *data) {
    auto *window = static_cast<OverlayWindow *>(data);
    window->owner->draw(*window, cr, width, height);
}

void OverlayApplication::on_realize(GtkWidget *widget, void *) {
    GtkNative *native = gtk_widget_get_native(widget);
    if (!native) {
        return;
    }
    GdkSurface *surface = gtk_native_get_surface(native);
    cairo_region_t *empty = cairo_region_create();
    gdk_surface_set_input_region(surface, empty);
    cairo_region_destroy(empty);
}

} // namespace sg::control
