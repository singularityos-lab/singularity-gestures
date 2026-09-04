#pragma once

#include <cstdint>
#include <string>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct zwlr_virtual_pointer_manager_v1;
struct zwlr_virtual_pointer_v1;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;
struct zsingularity_gesture_manager_v1;

namespace sg::control {

class VirtualPointer {
public:
    VirtualPointer() = default;
    ~VirtualPointer();

    VirtualPointer(const VirtualPointer &) = delete;
    VirtualPointer &operator=(const VirtualPointer &) = delete;

    bool connect(std::string &error);
    void disconnect();
    bool available() const;
    void move(float x, float y, int64_t timestamp_ms);
    void button(uint32_t code, bool pressed, int64_t timestamp_ms);
    void scroll(float horizontal, float vertical, int64_t timestamp_ms);
    void switch_window(bool forward, int64_t timestamp_ms);
    void lock_screen();
    void paste(int64_t timestamp_ms);
    void begin_desktop_gesture(uint32_t fingers, uint32_t direction);
    void update_desktop_gesture(float dx, float dy);
    void end_desktop_gesture(bool cancelled, bool committed);
    void release_buttons(int64_t timestamp_ms);

    static void global(void *data,
                       wl_registry *registry,
                       uint32_t name,
                       const char *interface,
                       uint32_t version);
    static void global_remove(void *data, wl_registry *registry, uint32_t name);

private:
    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    zwlr_virtual_pointer_manager_v1 *manager_ = nullptr;
    zwlr_virtual_pointer_v1 *pointer_ = nullptr;
    wl_seat *seat_ = nullptr;
    zwp_virtual_keyboard_manager_v1 *keyboard_manager_ = nullptr;
    zwp_virtual_keyboard_v1 *keyboard_ = nullptr;
    zsingularity_gesture_manager_v1 *gesture_manager_ = nullptr;
    uint32_t alt_mask_ = 0;
    uint32_t ctrl_mask_ = 0;
    uint32_t shift_mask_ = 0;
    bool left_pressed_ = false;
    bool right_pressed_ = false;

    void setup_keyboard();
};

} // namespace sg::control
