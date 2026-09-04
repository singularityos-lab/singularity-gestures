#include "control/virtual_pointer.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <linux/input-event-codes.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <gio/gio.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
#include "singularity-gesture-unstable-v1-client-protocol.h"

namespace sg::control {

namespace {

const wl_registry_listener registry_listener {
    .global = VirtualPointer::global,
    .global_remove = VirtualPointer::global_remove,
};

uint32_t event_time(int64_t timestamp_ms) {
    return static_cast<uint32_t>(timestamp_ms & 0xffffffffu);
}

void desktop_gesture_begin(void *, zsingularity_gesture_manager_v1 *,
                           uint32_t, uint32_t) {
}

void desktop_gesture_update(void *, zsingularity_gesture_manager_v1 *,
                            wl_fixed_t, wl_fixed_t) {
}

void desktop_gesture_end(void *, zsingularity_gesture_manager_v1 *,
                         uint32_t, uint32_t) {
}

const zsingularity_gesture_manager_v1_listener desktop_gesture_listener {
    .begin = desktop_gesture_begin,
    .update = desktop_gesture_update,
    .end = desktop_gesture_end,
};

} // namespace

VirtualPointer::~VirtualPointer() {
    disconnect();
}

bool VirtualPointer::connect(std::string &error) {
    disconnect();
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        error = "Could not connect to the Wayland display";
        return false;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &registry_listener, this);
    if (wl_display_roundtrip(display_) < 0 || !manager_) {
        error = "The compositor does not expose virtual pointer control";
        disconnect();
        return false;
    }
    pointer_ = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(
        manager_, nullptr);
    if (!pointer_) {
        error = "Could not create a virtual pointer";
        disconnect();
        return false;
    }
    setup_keyboard();
    wl_display_flush(display_);
    return true;
}

void VirtualPointer::disconnect() {
    if (keyboard_) {
        zwp_virtual_keyboard_v1_destroy(keyboard_);
        keyboard_ = nullptr;
    }
    if (keyboard_manager_) {
        zwp_virtual_keyboard_manager_v1_destroy(keyboard_manager_);
        keyboard_manager_ = nullptr;
    }
    if (gesture_manager_) {
        zsingularity_gesture_manager_v1_destroy(gesture_manager_);
        gesture_manager_ = nullptr;
    }
    if (pointer_) {
        zwlr_virtual_pointer_v1_destroy(pointer_);
        pointer_ = nullptr;
    }
    if (manager_) {
        zwlr_virtual_pointer_manager_v1_destroy(manager_);
        manager_ = nullptr;
    }
    if (seat_) {
        wl_seat_destroy(seat_);
        seat_ = nullptr;
    }
    if (registry_) {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }
    if (display_) {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
    left_pressed_ = false;
    right_pressed_ = false;
    alt_mask_ = 0;
    ctrl_mask_ = 0;
    shift_mask_ = 0;
}

bool VirtualPointer::available() const {
    return pointer_ != nullptr;
}

void VirtualPointer::move(float x, float y, int64_t timestamp_ms) {
    if (!pointer_) {
        return;
    }
    constexpr uint32_t extent = 100000;
    const auto px = static_cast<uint32_t>(std::clamp(x, 0.0f, 1.0f) * extent);
    const auto py = static_cast<uint32_t>(std::clamp(y, 0.0f, 1.0f) * extent);
    zwlr_virtual_pointer_v1_motion_absolute(
        pointer_, event_time(timestamp_ms), px, py, extent, extent);
    zwlr_virtual_pointer_v1_frame(pointer_);
    wl_display_flush(display_);
}

void VirtualPointer::button(uint32_t code, bool pressed, int64_t timestamp_ms) {
    if (!pointer_) {
        return;
    }
    bool *state = code == BTN_RIGHT ? &right_pressed_ : &left_pressed_;
    if (*state == pressed) {
        return;
    }
    *state = pressed;
    zwlr_virtual_pointer_v1_button(
        pointer_, event_time(timestamp_ms), code,
        pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                : WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(pointer_);
    wl_display_flush(display_);
}

void VirtualPointer::scroll(float horizontal,
                            float vertical,
                            int64_t timestamp_ms) {
    if (!pointer_ || (horizontal == 0.0f && vertical == 0.0f)) {
        return;
    }
    zwlr_virtual_pointer_v1_axis_source(pointer_,
                                        WL_POINTER_AXIS_SOURCE_FINGER);
    if (vertical != 0.0f) {
        zwlr_virtual_pointer_v1_axis(pointer_, event_time(timestamp_ms),
                                     WL_POINTER_AXIS_VERTICAL_SCROLL,
                                     wl_fixed_from_double(vertical));
    }
    if (horizontal != 0.0f) {
        zwlr_virtual_pointer_v1_axis(pointer_, event_time(timestamp_ms),
                                     WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                                     wl_fixed_from_double(horizontal));
    }
    zwlr_virtual_pointer_v1_frame(pointer_);
    wl_display_flush(display_);
}

void VirtualPointer::switch_window(bool forward, int64_t timestamp_ms) {
    GError *error = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (bus) {
        GVariant *reply = g_dbus_connection_call_sync(
            bus,
            "dev.sinty.desktop",
            "/dev/sinty/desktop/Shortcuts",
            "dev.sinty.desktop.Shortcuts",
            "ExecuteAction",
            g_variant_new("(s)", forward
                ? "switch_windows_next"
                : "switch_windows_prev"),
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            300,
            nullptr,
            &error);
        g_object_unref(bus);
        if (reply) {
            g_variant_unref(reply);
            return;
        }
    }
    g_clear_error(&error);
    if (!keyboard_) {
        return;
    }
    const uint32_t time = event_time(timestamp_ms);
    zwp_virtual_keyboard_v1_key(keyboard_, time, KEY_LEFTALT,
                                WL_KEYBOARD_KEY_STATE_PRESSED);
    if (!forward) {
        zwp_virtual_keyboard_v1_key(keyboard_, time, KEY_LEFTSHIFT,
                                    WL_KEYBOARD_KEY_STATE_PRESSED);
    }
    zwp_virtual_keyboard_v1_modifiers(
        keyboard_, alt_mask_ | (forward ? 0 : shift_mask_), 0, 0, 0);
    zwp_virtual_keyboard_v1_key(keyboard_, time, KEY_TAB,
                                WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(keyboard_, time + 1, KEY_TAB,
                                WL_KEYBOARD_KEY_STATE_RELEASED);
    if (!forward) {
        zwp_virtual_keyboard_v1_key(keyboard_, time + 1, KEY_LEFTSHIFT,
                                    WL_KEYBOARD_KEY_STATE_RELEASED);
    }
    zwp_virtual_keyboard_v1_key(keyboard_, time + 1, KEY_LEFTALT,
                                WL_KEYBOARD_KEY_STATE_RELEASED);
    zwp_virtual_keyboard_v1_modifiers(keyboard_, 0, 0, 0, 0);
    wl_display_flush(display_);
}

void VirtualPointer::lock_screen() {
    GError *error = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (bus) {
        GVariant *reply = g_dbus_connection_call_sync(
            bus,
            "dev.sinty.desktop",
            "/dev/sinty/desktop/Shortcuts",
            "dev.sinty.desktop.Shortcuts",
            "ExecuteAction",
            g_variant_new("(s)", "lock_screen"),
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            300,
            nullptr,
            &error);
        if (reply) {
            g_variant_unref(reply);
        }
        g_object_unref(bus);
    }
    g_clear_error(&error);
}

void VirtualPointer::paste(int64_t timestamp_ms) {
    if (!keyboard_) {
        return;
    }
    const uint32_t time = event_time(timestamp_ms);
    zwp_virtual_keyboard_v1_key(keyboard_, time, KEY_LEFTCTRL,
                                WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_modifiers(keyboard_, ctrl_mask_, 0, 0, 0);
    zwp_virtual_keyboard_v1_key(keyboard_, time, KEY_V,
                                WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(keyboard_, time + 1, KEY_V,
                                WL_KEYBOARD_KEY_STATE_RELEASED);
    zwp_virtual_keyboard_v1_key(keyboard_, time + 1, KEY_LEFTCTRL,
                                WL_KEYBOARD_KEY_STATE_RELEASED);
    zwp_virtual_keyboard_v1_modifiers(keyboard_, 0, 0, 0, 0);
    wl_display_flush(display_);
}

void VirtualPointer::begin_desktop_gesture(uint32_t fingers,
                                           uint32_t direction) {
    if (!gesture_manager_ ||
        zsingularity_gesture_manager_v1_get_version(gesture_manager_) < 3) {
        return;
    }
    zsingularity_gesture_manager_v1_begin_control(
        gesture_manager_, fingers, direction);
    wl_display_flush(display_);
}

void VirtualPointer::update_desktop_gesture(float dx, float dy) {
    if (!gesture_manager_ ||
        zsingularity_gesture_manager_v1_get_version(gesture_manager_) < 3) {
        return;
    }
    zsingularity_gesture_manager_v1_update_control(
        gesture_manager_, wl_fixed_from_double(dx), wl_fixed_from_double(dy));
    wl_display_flush(display_);
}

void VirtualPointer::end_desktop_gesture(bool cancelled, bool committed) {
    if (!gesture_manager_ ||
        zsingularity_gesture_manager_v1_get_version(gesture_manager_) < 3) {
        return;
    }
    zsingularity_gesture_manager_v1_end_control(
        gesture_manager_, cancelled ? 1u : 0u, committed ? 1u : 0u);
    wl_display_flush(display_);
}

void VirtualPointer::release_buttons(int64_t timestamp_ms) {
    button(BTN_LEFT, false, timestamp_ms);
    button(BTN_RIGHT, false, timestamp_ms);
}

void VirtualPointer::global(void *data,
                            wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version) {
    auto *self = static_cast<VirtualPointer *>(data);
    if (std::strcmp(interface,
                    zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
        self->manager_ = static_cast<zwlr_virtual_pointer_manager_v1 *>(
            wl_registry_bind(registry, name,
                             &zwlr_virtual_pointer_manager_v1_interface,
                             std::min(version, 2u)));
    } else if (std::strcmp(
                   interface,
                   zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        self->keyboard_manager_ =
            static_cast<zwp_virtual_keyboard_manager_v1 *>(
                wl_registry_bind(
                    registry, name,
                    &zwp_virtual_keyboard_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        self->seat_ = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface,
                             std::min(version, 7u)));
    } else if (std::strcmp(
                   interface,
                   zsingularity_gesture_manager_v1_interface.name) == 0) {
        self->gesture_manager_ =
            static_cast<zsingularity_gesture_manager_v1 *>(
                wl_registry_bind(
                    registry, name,
                    &zsingularity_gesture_manager_v1_interface,
                    std::min(version, 3u)));
        zsingularity_gesture_manager_v1_add_listener(
            self->gesture_manager_, &desktop_gesture_listener, nullptr);
    }
}

void VirtualPointer::global_remove(void *, wl_registry *, uint32_t) {
}

void VirtualPointer::setup_keyboard() {
    if (!keyboard_manager_ || !seat_) {
        return;
    }
    keyboard_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        keyboard_manager_, seat_);
    xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) {
        zwp_virtual_keyboard_v1_destroy(keyboard_);
        keyboard_ = nullptr;
        return;
    }
    const xkb_rule_names names {
        .rules = "evdev",
        .model = "pc105",
        .layout = "us",
        .variant = nullptr,
        .options = nullptr,
    };
    xkb_keymap *keymap = xkb_keymap_new_from_names(
        context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(keyboard_);
        keyboard_ = nullptr;
        return;
    }
    bool configured = false;
    char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (text) {
        const size_t size = std::strlen(text) + 1;
        const int fd = static_cast<int>(
            syscall(SYS_memfd_create, "singularity-hand-control", 0));
        if (fd >= 0 && ftruncate(fd, static_cast<off_t>(size)) == 0) {
            size_t written = 0;
            while (written < size) {
                const ssize_t count = write(fd, text + written, size - written);
                if (count <= 0) {
                    break;
                }
                written += static_cast<size_t>(count);
            }
            if (written == size) {
                lseek(fd, 0, SEEK_SET);
                zwp_virtual_keyboard_v1_keymap(
                    keyboard_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                    fd, static_cast<uint32_t>(size));
                configured = true;
            }
        }
        if (fd >= 0) {
            close(fd);
        }
        free(text);
    }
    if (!configured) {
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(keyboard_);
        keyboard_ = nullptr;
        return;
    }
    const xkb_mod_index_t alt = xkb_keymap_mod_get_index(keymap,
                                                          XKB_MOD_NAME_ALT);
    const xkb_mod_index_t ctrl = xkb_keymap_mod_get_index(keymap,
                                                           XKB_MOD_NAME_CTRL);
    const xkb_mod_index_t shift = xkb_keymap_mod_get_index(
        keymap, XKB_MOD_NAME_SHIFT);
    if (alt != XKB_MOD_INVALID) {
        alt_mask_ = 1u << alt;
    }
    if (ctrl != XKB_MOD_INVALID) {
        ctrl_mask_ = 1u << ctrl;
    }
    if (shift != XKB_MOD_INVALID) {
        shift_mask_ = 1u << shift;
    }
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
}

} // namespace sg::control
