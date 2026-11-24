// SPDX-License-Identifier: Apache-2.0

// Board-local LVGL task stack placement.
//
// Core Tactility asks esp_lvgl_port for a PSRAM stack whenever external RAM exists. Any code
// running on that task which reads the flash filesystem (stat/mkdir/open on /data) trips the
// ESP-IDF cache-disable assert, because the stack cannot be accessed while the flash cache is
// off. Core UI callbacks reach such paths easily: the Display and Appearance apps trigger
// settings saves that stat() the flash FAT.
//
// The board cannot change the core's lvgl_module configuration, so wrap lvgl_port_init() and
// force the task stack into internal RAM for every caller. lvgl_port_init() is a public symbol
// of the esp_lvgl_port component, so a link-time --wrap is enough.

#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include <esp_err.h>

extern "C" esp_err_t __real_lvgl_port_init(const lvgl_port_cfg_t* config);

extern "C" esp_err_t __wrap_lvgl_port_init(const lvgl_port_cfg_t* config) {
    if (config == nullptr) {
        return __real_lvgl_port_init(config);
    }

    lvgl_port_cfg_t patched = *config;
    patched.task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT;
    // The core requests 9120 bytes, which the richer notification/system UI and app players
    // (e.g. AudioRecorder) now overflow on the LVGL task. 16 KB covers those call stacks.
    if (patched.task_stack < 16384) {
        patched.task_stack = 16384;
    }
    return __real_lvgl_port_init(&patched);
}
