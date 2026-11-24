// SPDX-License-Identifier: Apache-2.0

// Board-local wake-time RTC sync and deep-sleep power sequencing for the T-Watch Ultra.
//
// The PWR button itself is exposed as a kernel button by the axp2101 module (the PWRKEY child
// device), so core standby and ButtonActions own its short press. This file only prepares the
// PMIC/peripherals when the core sleep controller enters deep sleep: the PWRKEY interrupt has to
// be the only enabled AXP source, its stale status has to be cleared (otherwise the IRQ line keeps
// the ESP32 awake), and the unused rails are switched off. The external PCF85063A RTC keeps time
// through deep sleep; after wake the existing GpsTimeSyncService updates the system clock and
// RtcTimeService writes it back to the RTC.

#include "power_management.h"

#include "watch_settings.h"

#include <Tactility/settings/Time.h>

#include <location/location.h>

#include <tactility/device.h>
#include <drivers/axp2101.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/system_event.h>

#include <driver/gpio.h>

#include <atomic>

namespace {

constexpr auto* TAG = "PowerManagement";

constexpr gpio_num_t PWR_IRQ_PIN = GPIO_NUM_7;
constexpr uint8_t AXP2101_ADDRESS = 0x34;

constexpr uint8_t REG_INTEN1 = 0x40;
constexpr uint8_t REG_INTEN2 = 0x41;
constexpr uint8_t REG_INTEN3 = 0x42;
constexpr uint8_t REG_INTSTS1 = 0x48;
constexpr uint8_t REG_INTSTS2 = 0x49;
constexpr uint8_t REG_INTSTS3 = 0x4A;

// AXP2101 IRQ register 2, bit 3 = PWRKEY short press.
constexpr uint8_t PWRKEY_SHORT_PRESS_BIT = 1U << 3U;

constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

std::atomic<bool> wake_sync_applied { false };

Device* axp_device = nullptr;
Device* i2c_parent = nullptr;

bool find_axp_device() {
    if (axp_device != nullptr) {
        return true;
    }

    Device* device = nullptr;
    if (device_get_by_name("axp2101", &device) != ERROR_NONE) {
        return false;
    }
    if (!device_is_ready(device)) {
        device_put(device);
        return false;
    }

    Device* parent = device_get_parent(device);
    if (parent == nullptr) {
        device_put(device);
        return false;
    }

    axp_device = device;
    i2c_parent = parent;
    return true;
}

bool axp_write(uint8_t reg, uint8_t value) {
    if (i2c_parent == nullptr) {
        return false;
    }
    return i2c_controller_register8_set(i2c_parent, AXP2101_ADDRESS, reg, value, I2C_TIMEOUT) == ERROR_NONE;
}

void clear_axp_irq() {
    // Status registers are write-1-to-clear.
    axp_write(REG_INTSTS1, 0xFF);
    axp_write(REG_INTSTS2, 0xFF);
    axp_write(REG_INTSTS3, 0xFF);
}

bool configure_axp_irq() {
    if (!find_axp_device()) {
        return false;
    }

    // Mask every source except PWRKEY short press. Other events (VBUS, battery, watchdog, ...)
    // must not assert GPIO7 and cause a spurious wake.
    if (!axp_write(REG_INTEN1, 0x00)) {
        return false;
    }
    if (!axp_write(REG_INTEN2, PWRKEY_SHORT_PRESS_BIT)) {
        return false;
    }
    if (!axp_write(REG_INTEN3, 0x00)) {
        return false;
    }

    clear_axp_irq();
    return true;
}

void configure_irq_pin() {
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << PWR_IRQ_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&config);
}

void set_ldo(enum Axp2101Ldo ldo, bool enabled) {
    if (axp_device == nullptr) {
        return;
    }
    if (axp2101_set_ldo_enabled(axp_device, ldo, enabled) != ERROR_NONE) {
        LOG_W(TAG, "Failed to %s LDO %d", enabled ? "enable" : "disable", static_cast<int>(ldo));
    }
}

// Runs synchronously from sleep_controller_deep_sleep(), immediately before esp_deep_sleep_start().
void power_down_peripherals() {
    Device* backlight = nullptr;
    if (device_get_first_active_by_type(&BACKLIGHT_TYPE, &backlight) == ERROR_NONE) {
        backlight_set_brightness(backlight, 0);
        device_put(backlight);
    }

    set_ldo(AXP2101_ALDO2, false); // display
    set_ldo(AXP2101_ALDO3, false); // LoRa
    set_ldo(AXP2101_ALDO4, false); // BHI260AP
    set_ldo(AXP2101_BLDO1, false); // GNSS
    set_ldo(AXP2101_BLDO2, false); // speaker
    set_ldo(AXP2101_DLDO1, false); // NFC
    // ALDO1 (SD) and DCDC1 (ESP32) stay on: cutting SD power without unmounting risks corruption.
}

void on_deep_sleep_prepare(SystemEvent* /*event*/, void* /*context*/) {
    // Keep only the PWRKEY short press enabled, and clear any latched status so the IRQ line is
    // released before the ESP32 sleeps.
    configure_axp_irq();
    configure_irq_pin();
    power_down_peripherals();
}

void on_file_system_mounted(SystemEvent* /*event*/, void* /*context*/) {
    if (wake_sync_applied.load(std::memory_order_acquire)) {
        return;
    }
    if (!watch_settings_load()) {
        return; // Retry on the next mount event
    }

    if (watch_settings_get_wake_gps_sync_enabled()) {
        tt::settings::setGpsTimeSyncAtBoot(true);
    }
    wake_sync_applied.store(true, std::memory_order_release);
}

void on_boot_completed(SystemEvent* /*event*/, void* /*context*/) {
    if (!watch_settings_get_wake_gps_sync_enabled()) {
        return;
    }
    // GpsTimeSyncService waits for location services, so make sure they are running. The service
    // subscribes on its next timer tick and stops the providers again after the first time fix.
    if (!location_is_enabled()) {
        location_set_enabled(true);
    }
}

} // namespace

extern "C" {

void power_management_start(void) {
    wake_sync_applied.store(false, std::memory_order_release);

    system_event_callback_add(KERNEL_EVENT_FILE_SYSTEM_MOUNTED, &on_file_system_mounted, nullptr);
    system_event_callback_add(KERNEL_EVENT_BOOT_COMPLETED, &on_boot_completed, nullptr);
    system_event_callback_add(KERNEL_EVENT_DEEP_SLEEP_PREPARE, &on_deep_sleep_prepare, nullptr);
}

void power_management_stop(void) {
    system_event_callback_remove(KERNEL_EVENT_FILE_SYSTEM_MOUNTED, &on_file_system_mounted);
    system_event_callback_remove(KERNEL_EVENT_BOOT_COMPLETED, &on_boot_completed);
    system_event_callback_remove(KERNEL_EVENT_DEEP_SLEEP_PREPARE, &on_deep_sleep_prepare);

    if (axp_device != nullptr) {
        device_put(axp_device);
        axp_device = nullptr;
        i2c_parent = nullptr;
    }
}

} // extern "C"
