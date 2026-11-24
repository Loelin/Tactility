// SPDX-License-Identifier: Apache-2.0

// Plays the light haptic click when the launcher's page changes.
//
// A page change is not an LVGL event that the board can observe, but every page change ends with
// lvgl_page_indicator_set_page(). Wrapping that call catches both swipes and the previous/next
// buttons. The buttons already produce a normal click through the LV_EVENT_CLICKED haptic path,
// so this only fires while no input device is active: swipe-driven page changes are deferred with
// lv_async_call() and therefore run outside of input processing.

#include "haptic_feedback.h"

#include <lvgl/lvgl.h>
#include <lvgl/widgets/page_indicator.h>

namespace {

// The page indicator's first child is the page-number label; the dots follow.
// Returns -1 when no dot is checked, e.g. before the first page is set.
int32_t get_current_page(lv_obj_t* page_indicator) {
    const uint32_t dot_count = lv_obj_get_child_count(page_indicator) - 1;
    for (uint32_t i = 0; i < dot_count; i++) {
        lv_obj_t* dot = lv_obj_get_child(page_indicator, static_cast<int32_t>(i + 1));
        if (lv_obj_has_state(dot, LV_STATE_CHECKED)) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

} // namespace

extern "C" {

void __real_lvgl_page_indicator_set_page(lv_obj_t* obj, uint32_t page);

void __wrap_lvgl_page_indicator_set_page(lv_obj_t* obj, uint32_t page) {
    const int32_t previous_page = get_current_page(obj);

    __real_lvgl_page_indicator_set_page(obj, page);

    const bool page_changed = previous_page >= 0 && static_cast<uint32_t>(previous_page) != page;
    if (page_changed && lv_indev_active() == nullptr) {
        watch_haptic_play(WATCH_HAPTIC_LIGHT);
    }
}

} // extern "C"
