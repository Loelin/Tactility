// SPDX-License-Identifier: Apache-2.0

// Board-local dirty-area rounding for the CO5300.
//
// The CO5300 datasheet (7.5.21 CASET, 7.5.22 RASET) requires both the window start address and the
// start-to-end span to be divisible by 2 on each axis. The panel also shears when a write window is
// narrower than the panel, so this board only ever presents full-width strips. At rotation 0/180 the
// panel columns are the logical x range, which this board widens to the full (even) width; at
// 90/270 the logical y range becomes the panel column range, so the row range needs the even
// rounding instead.
//
// The core renders in PARTIAL mode: LVGL hands each invalid area to the flush callback, so this
// hook widens the invalid area itself. At 90/270 it expands to the whole screen, because a logical
// full-width strip becomes a narrow physical column there, which is the shape that shears.
//
// LVGL exposes LV_EVENT_INVALIDATE_AREA for exactly this; the handler is attached by wrapping
// lv_display_set_flush_cb.

#include <lvgl/lvgl.h>

namespace {

void round_flush_area(lv_event_t* event) {
    lv_area_t* area = lv_event_get_invalidated_area(event);
    if (area == nullptr) {
        return;
    }

    lv_display_t* display = static_cast<lv_display_t*>(lv_event_get_user_data(event));
    if (display == nullptr) {
        return;
    }

    const int32_t width = lv_display_get_horizontal_resolution(display);
    if (width <= 0) {
        return;
    }

    // LVGL also sends this event with a scratch area (x1 = x2 = 0, y1 = 0) when get_max_row()
    // computes how many rows fit in the draw buffer. Widening that probe to the full screen
    // would make the probe never fit, so get_max_row() would return 0 and LVGL would divide by
    // zero on the resulting empty area. Leave its geometry alone; only even-round its height.
    const bool capacity_probe = area->x1 == 0 && area->x2 == 0 && area->y1 == 0;
    const bool direct = lv_display_get_render_mode(display) == LV_DISPLAY_RENDER_MODE_DIRECT;
    const lv_display_rotation_t rotation = lv_display_get_rotation(display);
    const bool columns_are_rows = rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270;

    if (!direct && !capacity_probe) {
        area->x1 = 0;
        area->x2 = width - 1;

        if (columns_are_rows) {
            const int32_t height = lv_display_get_vertical_resolution(display);
            if (height > 0) {
                area->y1 = 0;
                area->y2 = height - 1;
            }
        }
    }

    area->y1 &= ~1;
    if (((area->y2 - area->y1 + 1) & 1) != 0) {
        // Both panel dimensions are even, so a strip that reaches the last row (height - 1, odd)
        // already has an even height and never needs to grow past the screen here.
        area->y2++;
    }
}

} // namespace

extern "C" {

void __real_lv_display_set_flush_cb(lv_display_t* display, lv_display_flush_cb_t flush_cb);

void __wrap_lv_display_set_flush_cb(lv_display_t* display, lv_display_flush_cb_t flush_cb) {
    if (display != nullptr) {
        lv_display_add_event_cb(display, round_flush_area, LV_EVENT_INVALIDATE_AREA, display);
    }
    __real_lv_display_set_flush_cb(display, flush_cb);
}

}
