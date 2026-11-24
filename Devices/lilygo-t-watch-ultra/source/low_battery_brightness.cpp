// SPDX-License-Identifier: Apache-2.0

// Board-local low-battery brightness cap for the T-Watch Ultra.
//
// When the AXP2101 E-Gauge reports less than 10% capacity, cap the display backlight at 20%
// of its range to stretch the remaining charge. The cap is applied through a link-time wrapper
// on backlight_set_brightness(), so every caller (boot, Display settings, DisplayIdle) is
// covered without modifying core Tactility.
//
// A small monitor task watches for the low -> normal transition and restores the last
// requested brightness. It also avoids turning the screen on when the backlight is off.

#include "low_battery_brightness.h"

#include <tactility/device.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

extern "C" error_t __real_backlight_set_brightness(Device* device, uint8_t brightness);

namespace {

constexpr auto* TAG = "LowBatteryBrightness";

constexpr int LOW_BATTERY_PERCENTAGE = 10;
constexpr int LOW_BRIGHTNESS_PERCENTAGE = 20;
constexpr uint32_t INITIAL_DELAY_MS = 5000;
constexpr uint32_t POLL_INTERVAL_MS = 30000;

std::atomic<uint8_t> last_requested_brightness { 255 };
std::atomic<bool> stop_requested { false };
TaskHandle_t task_handle = nullptr;

bool read_battery_capacity(int& out_capacity) {
    Device* power_supply = nullptr;
    device_for_each_of_type(&POWER_SUPPLY_TYPE, &power_supply, [](Device* device, void* context) {
        if (device_is_ready(device) && power_supply_supports_property(device, POWER_SUPPLY_PROP_CAPACITY)) {
            *static_cast<Device**>(context) = device;
            return false;
        }
        return true;
    });

    if (power_supply == nullptr) {
        return false;
    }

    PowerSupplyPropertyValue value {};
    if (power_supply_get_property(power_supply, POWER_SUPPLY_PROP_CAPACITY, &value) != ERROR_NONE) {
        return false;
    }

    out_capacity = value.int_value;
    return true;
}

Device* get_backlight() {
    Device* backlight = nullptr;
    if (device_get_first_active_by_type(&BACKLIGHT_TYPE, &backlight) != ERROR_NONE) {
        return nullptr;
    }
    return backlight;
}

uint8_t get_low_brightness(Device* backlight) {
    const uint8_t min = backlight_get_min_brightness(backlight);
    const uint8_t max = backlight_get_max_brightness(backlight);
    return static_cast<uint8_t>(min + ((static_cast<uint16_t>(max - min) * LOW_BRIGHTNESS_PERCENTAGE + 50) / 100));
}

void monitor_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(INITIAL_DELAY_MS));

    bool was_low = false;
    while (!stop_requested.load(std::memory_order_acquire)) {
        Device* backlight = get_backlight();
        int capacity = 100;
        if (backlight != nullptr && read_battery_capacity(capacity)) {
            const bool low = capacity < LOW_BATTERY_PERCENTAGE;
            uint8_t current = 0;
            const uint8_t min = backlight_get_min_brightness(backlight);
            if (backlight_get_brightness(backlight, &current) == ERROR_NONE) {
                if (low && !was_low) {
                    // Screen off stays off; only cap a screen that is currently on.
                    if (current > min) {
                        const uint8_t limit = get_low_brightness(backlight);
                        if (current > limit) {
                            __real_backlight_set_brightness(backlight, limit);
                        }
                    }
                } else if (!low && was_low && current > min) {
                    __real_backlight_set_brightness(backlight, last_requested_brightness.load(std::memory_order_acquire));
                }
            }
            was_low = low;
            device_put(backlight);
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }

    task_handle = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

extern "C" {

error_t __real_backlight_set_brightness(Device* device, uint8_t brightness);

error_t __wrap_backlight_set_brightness(Device* device, uint8_t brightness) {
    const uint8_t min = backlight_get_min_brightness(device);
    if (brightness > min) {
        // Remember the caller's intent so it can be restored after the battery recovers.
        // Zero/min requests (screen off) must not overwrite it.
        last_requested_brightness.store(brightness, std::memory_order_release);
    }

    uint8_t effective_brightness = brightness;
    int capacity = 100;
    if (read_battery_capacity(capacity) && capacity < LOW_BATTERY_PERCENTAGE) {
        const uint8_t limit = get_low_brightness(device);
        if (effective_brightness > limit) {
            effective_brightness = limit;
        }
    }

    return __real_backlight_set_brightness(device, effective_brightness);
}

void low_battery_brightness_start(void) {
    stop_requested.store(false, std::memory_order_release);
    if (task_handle == nullptr) {
        if (xTaskCreate(monitor_task, "low_batt_brightness", 3072, nullptr, 1, &task_handle) != pdPASS) {
            LOG_W(TAG, "Failed to create brightness monitor task");
            task_handle = nullptr;
        }
    }
}

void low_battery_brightness_stop(void) {
    stop_requested.store(true, std::memory_order_release);
}

} // extern "C"
