// SPDX-License-Identifier: Apache-2.0

// Board-local settings app. Core Tactility's Display settings app is intentionally not modified,
// so the T-Watch Ultra's haptic feedback toggle lives here instead and is listed with the other
// settings apps in the launcher.

#include "watch_settings.h"

// These features are local-only and not tracked in the branch; hide their settings when absent.
#if __has_include("t9_keyboard.h")
#include "t9_keyboard.h"
#define TWATCH_SETTINGS_HAS_T9_KEYBOARD 1
#else
#define TWATCH_SETTINGS_HAS_T9_KEYBOARD 0
#endif

#if __has_include("voice_input.h")
#include "voice_input.h"
#define TWATCH_SETTINGS_HAS_VOICE_INPUT 1
#else
#define TWATCH_SETTINGS_HAS_VOICE_INPUT 0
#endif

#include <Tactility/settings/Time.h>

#include <location/location.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

namespace {

constexpr auto* TAG = "WatchSettings";

struct Context {
    uint32_t appInstanceId = 0;
    lv_obj_t* hapticSwitch = nullptr;
    lv_obj_t* tt9Switch = nullptr;
    lv_obj_t* voiceSwitch = nullptr;
    lv_obj_t* gpsSyncSwitch = nullptr;
};

void onBackPressed(lv_event_t* event) {
    auto* context = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(context->appInstanceId);
}

void onHapticChanged(lv_event_t* event) {
    auto* context = static_cast<Context*>(lv_event_get_user_data(event));
    watch_settings_set_vibration_enabled(lv_obj_has_state(context->hapticSwitch, LV_STATE_CHECKED));
}

#if TWATCH_SETTINGS_HAS_T9_KEYBOARD
void onTt9Changed(lv_event_t* event) {
    auto* context = static_cast<Context*>(lv_event_get_user_data(event));
    // Persists the preference and applies it to the keyboard immediately.
    t9_keyboard_set_enabled(lv_obj_has_state(context->tt9Switch, LV_STATE_CHECKED));
}
#endif

#if TWATCH_SETTINGS_HAS_VOICE_INPUT
void onVoiceChanged(lv_event_t* event) {
    auto* context = static_cast<Context*>(lv_event_get_user_data(event));
    const bool enabled = lv_obj_has_state(context->voiceSwitch, LV_STATE_CHECKED);
    watch_settings_set_dictation_enabled(enabled);
    if (!enabled) {
        voice_input_cancel();
    }
}
#endif

void onWakeGpsSyncChanged(lv_event_t* event) {
    auto* context = static_cast<Context*>(lv_event_get_user_data(event));
    const bool enabled = lv_obj_has_state(context->gpsSyncSwitch, LV_STATE_CHECKED);
    watch_settings_set_wake_gps_sync_enabled(enabled);
    tt::settings::setGpsTimeSyncAtBoot(enabled);
    if (enabled && !location_is_enabled()) {
        location_set_enabled(true);
    }
}

lv_obj_t* createSwitchRow(
    lv_obj_t* parent,
    const char* text,
    bool checked,
    lv_event_cb_t onChange,
    void* userData
) {
    auto* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(row, false);

    auto* label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_flex_grow(label, 1);

    auto* toggle = lv_switch_create(row);
    lv_obj_set_state(toggle, LV_STATE_CHECKED, checked);
    lv_obj_add_event_cb(toggle, onChange, LV_EVENT_VALUE_CHANGED, userData);
    return toggle;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* context = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Miscellaneous");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, context);

    // The window manager makes the app's root widget non-scrollable, so the app needs its own
    // scrollable region. Growing to the remaining height (instead of sizing to content) keeps
    // this wrapper inside the window; older cards then scroll when they don't all fit.
    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);

    auto* hapticCard = lvgl_card_create(wrapper);
    lv_obj_set_size(hapticCard, LV_PCT(100), LV_SIZE_CONTENT);
    context->hapticSwitch = createSwitchRow(
        hapticCard,
        "Haptic Feedback",
        watch_settings_get_vibration_enabled(),
        onHapticChanged,
        context
    );

#if TWATCH_SETTINGS_HAS_T9_KEYBOARD
    auto* tt9Card = lvgl_card_create(wrapper);
    lv_obj_set_size(tt9Card, LV_PCT(100), LV_SIZE_CONTENT);
    context->tt9Switch = createSwitchRow(
        tt9Card,
        "TT9 Input",
        t9_keyboard_is_enabled(),
        onTt9Changed,
        context
    );
#endif

#if TWATCH_SETTINGS_HAS_VOICE_INPUT
    auto* voiceCard = lvgl_card_create(wrapper);
    lv_obj_set_size(voiceCard, LV_PCT(100), LV_SIZE_CONTENT);
    context->voiceSwitch = createSwitchRow(
        voiceCard,
        "Dictation",
        watch_settings_get_dictation_enabled(),
        onVoiceChanged,
        context
    );
#endif

    auto* gpsSyncCard = lvgl_card_create(wrapper);
    lv_obj_set_size(gpsSyncCard, LV_PCT(100), LV_SIZE_CONTENT);
    context->gpsSyncSwitch = createSwitchRow(
        gpsSyncCard,
        "GPS Sync on Wake",
        watch_settings_get_wake_gps_sync_enabled(),
        onWakeGpsSyncChanged,
        context
    );
}

void destroyWidgets(void* userData) {
    static_cast<Context*>(userData)->hapticSwitch = nullptr;
    static_cast<Context*>(userData)->tt9Switch = nullptr;
    static_cast<Context*>(userData)->voiceSwitch = nullptr;
    static_cast<Context*>(userData)->gpsSyncSwitch = nullptr;
}

int32_t appMain(int /*argc*/, char* /*argv*/[]) {
    Context context {};
    context.appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);

    AppEventSubscription subscription {};
    if (app_event_subscribe(&subscription, &eventGroup) != ERROR_NONE) {
        task_event_group_destruct(&eventGroup);
        return 0;
    }

    const WindowId window = window_manager_create_ext(context.appInstanceId, createWidgets, destroyWidgets, &context);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&eventGroup, &flags, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&subscription, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&subscription);
    task_event_group_destruct(&eventGroup);
    return 0;
}

const AppManifest manifest = {
    .id = "lilygo.twatchultra.settings",
    .name = "Miscellaneous",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace

extern "C" {

void watch_settings_register_app(void) {
    const error_t error = app_manager_add(&manifest);
    if (error != ERROR_NONE) {
        LOG_W(TAG, "Failed to register Miscellaneous app: %s", error_to_string(error));
    }
}

} // extern "C"
