// SPDX-License-Identifier: Apache-2.0

// Confirms touch input on the T-Watch Ultra with the onboard DRV2605 haptic motor.
//
// Firing from the raw touch layer would buzz on every contact, including empty screen
// areas and the start of a scroll. Instead this wraps LVGL's event dispatch at link time
// and only fires when a completed click reaches a widget that has an interaction handler - either
// the clicked object itself or an ancestor (the hit target is often a label inside a button).
// LVGL only sends LV_EVENT_CLICKED on release when the gesture was not a scroll.
//
// Keyboard/button-matrix clicks use a lighter waveform so typing does not feel as heavy as
// a normal widget press. A settling scroll and a launcher page change both play the same light
// sharp click (waveform 6). The enable/disable flag comes from the board's Miscellaneous app.

#include "haptic_feedback.h"
#include "watch_settings.h"

#include <tactility/device.h>
#include <tactility/drivers/haptic.h>

#include <lvgl/lvgl.h>

namespace {

struct Device* haptic_device = nullptr;
uint8_t configured_waveform = 0;

Device* find_haptic_device() {
    if (haptic_device == nullptr) {
        if (device_get_first_active_by_type(&HAPTIC_TYPE, &haptic_device) != ERROR_NONE) {
            haptic_device = nullptr;
        }
    }
    return haptic_device;
}

// True when the clicked object (or one of its parents, up to but excluding the screen)
// has an event callback, which means the click actually activates a widget instead of
// landing on background/container space.
bool is_interactive_widget(lv_obj_t* obj) {
    for (lv_obj_t* current = obj; current != nullptr; current = lv_obj_get_parent(current)) {
        if (lv_obj_get_parent(current) == nullptr) {
            // Reached the screen layer: not a widget.
            return false;
        }
        if (lv_obj_get_event_count(current) > 0) {
            return true;
        }
    }
    return false;
}

// The click target for a key is usually the keyboard's button matrix (or a button inside it).
bool is_keyboard_widget(lv_obj_t* obj) {
    for (lv_obj_t* current = obj; current != nullptr; current = lv_obj_get_parent(current)) {
        if (lv_obj_check_type(current, &lv_keyboard_class) ||
            lv_obj_check_type(current, &lv_buttonmatrix_class)) {
            return true;
        }
        if (lv_obj_get_parent(current) == nullptr) {
            break;
        }
    }
    return false;
}

} // namespace

// Plays a short click used to confirm a touch interaction.
void watch_haptic_play(uint8_t waveform) {
    if (!watch_settings_get_vibration_enabled()) {
        return;
    }

    Device* device = find_haptic_device();
    if (device == nullptr) {
        return;
    }

    if (configured_waveform != waveform) {
        // Slot 0: click; slot 1: zero terminates the sequence.
        if (haptic_set_waveform(device, 0, waveform) != ERROR_NONE ||
            haptic_set_waveform(device, 1, 0) != ERROR_NONE) {
            return;
        }
        configured_waveform = waveform;
    }

    haptic_start_playback(device);
}

// Plays the waveform in several consecutive sequence slots: one hardware sequence, no software
// delay, so it is safe to call from the LVGL task.
void watch_haptic_play_sequence(uint8_t waveform, uint8_t count) {
    if (!watch_settings_get_vibration_enabled() || count == 0) {
        return;
    }

    Device* device = find_haptic_device();
    if (device == nullptr) {
        return;
    }

    if (count > 7) {
        count = 7;
    }
    for (uint8_t slot = 0; slot < count; ++slot) {
        if (haptic_set_waveform(device, slot, waveform) != ERROR_NONE) {
            return;
        }
    }
    if (haptic_set_waveform(device, count, 0) != ERROR_NONE) {
        return;
    }

    // The single-click cache no longer matches the slots.
    configured_waveform = 0;
    haptic_start_playback(device);
}

extern "C" {

lv_result_t __real_lv_obj_send_event(lv_obj_t* obj, lv_event_code_t event_code, void* param);

lv_result_t __wrap_lv_obj_send_event(lv_obj_t* obj, lv_event_code_t event_code, void* param) {
    // Only real pointer/keypad clicks, not programmatic events. lv_indev_active() is set
    // for the duration of input processing.
    const bool interactive_click = event_code == LV_EVENT_CLICKED &&
        lv_indev_active() != nullptr &&
        is_interactive_widget(obj);
    const bool keyboard_click = interactive_click && is_keyboard_widget(obj);
    // A scroll gesture that settled. LVGL passes the indev for gesture-driven scroll ends and
    // NULL for programmatic ones (lv_obj_scroll_to*), so this ignores code-driven scrolling.
    const bool scroll_finished = event_code == LV_EVENT_SCROLL_END && param != nullptr;

    const lv_result_t result = __real_lv_obj_send_event(obj, event_code, param);

    if (interactive_click) {
        watch_haptic_play(keyboard_click ? WATCH_HAPTIC_KEY : WATCH_HAPTIC_CLICK);
    } else if (scroll_finished) {
        watch_haptic_play(WATCH_HAPTIC_LIGHT);
    }

    return result;
}

} // extern "C"
