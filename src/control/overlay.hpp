#pragma once

#include <memory>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "control/hand_controller.hpp"
#include "control/toy_controller.hpp"

namespace sg::control {

class OverlayApplication {
public:
    explicit OverlayApplication(std::string runtime_dir);
    ~OverlayApplication();

    int run(int argc, char **argv);

private:
    struct OverlayWindow {
        OverlayApplication *owner = nullptr;
        GtkWindow *window = nullptr;
        GtkWidget *area = nullptr;
        GdkRectangle monitor {};
    };

    std::string runtime_dir_;
    GtkApplication *application_ = nullptr;
    std::unique_ptr<HandController> controller_;
    std::vector<std::unique_ptr<OverlayWindow>> windows_;
    GdkRectangle desktop_ {};
    guint tick_source_ = 0;
    guint capture_timeout_ = 0;
    guint show_timeout_ = 0;
    gulong monitor_handler_ = 0;
    uint64_t screenshot_serial_ = 0;
    bool pending_fullscreen_ = false;
    int pending_x_ = 0;
    int pending_y_ = 0;
    int pending_width_ = 0;
    int pending_height_ = 0;
    std::string startup_error_;
    ToyController toy_;

    void activate();
    int command_line(GApplicationCommandLine *command_line);
    void create_windows();
    void destroy_windows();
    void reset_calibration();
    void tick();
    void update_toy(const ControllerSnapshot &state, int64_t now_ms);
    void schedule_screenshot(const ControllerSnapshot &state);
    void capture_screenshot();
    void show_overlays();
    void draw(OverlayWindow &window, cairo_t *cr, int width, int height);

    static void on_activate(GApplication *application, void *data);
    static int on_command_line(GApplication *application,
                               GApplicationCommandLine *command_line,
                               void *data);
    static gboolean on_tick(void *data);
    static gboolean on_capture_timeout(void *data);
    static gboolean on_show_timeout(void *data);
    static void on_monitors_changed(GListModel *model,
                                    guint position,
                                    guint removed,
                                    guint added,
                                    void *data);
    static void on_draw(GtkDrawingArea *area,
                        cairo_t *cr,
                        int width,
                        int height,
                        void *data);
    static void on_realize(GtkWidget *widget, void *data);
};

} // namespace sg::control
