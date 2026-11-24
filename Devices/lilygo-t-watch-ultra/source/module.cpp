// SPDX-License-Identifier: Apache-2.0
#include <tactility/module.h>

#include "low_battery_brightness.h"
#include "power_management.h"
#include "watch_settings.h"

// The dictation and TT9 keyboard sources are local-only features and are not tracked in the
// branch. Guard the hooks so a clean checkout still builds without them.
#if __has_include("t9_keyboard.h")
#include "t9_keyboard.h"
#define TWATCH_HAS_T9_KEYBOARD 1
#else
#define TWATCH_HAS_T9_KEYBOARD 0
#endif

#if __has_include("voice_input.h")
#include "voice_input.h"
#define TWATCH_HAS_VOICE_INPUT 1
#else
#define TWATCH_HAS_VOICE_INPUT 0
#endif

namespace {

error_t start() {
    // Register the sleep/FS callbacks before the platform mounts /data and before services start.
    power_management_start();
#if TWATCH_HAS_T9_KEYBOARD
    t9_keyboard_start();
#endif
#if TWATCH_HAS_VOICE_INPUT
    voice_input_start();
#endif
    watch_settings_register_app();
#if TWATCH_HAS_VOICE_INPUT
    voice_input_register_app();
#endif
    low_battery_brightness_start();
    return ERROR_NONE;
}

error_t stop() {
    low_battery_brightness_stop();
#if TWATCH_HAS_VOICE_INPUT
    voice_input_stop();
#endif
#if TWATCH_HAS_T9_KEYBOARD
    t9_keyboard_stop();
#endif
    power_management_stop();
    return ERROR_NONE;
}

} // namespace

extern "C" {

Module lilygo_t_watch_ultra_module = {
    .name = "lilygo-t-watch-ultra",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
