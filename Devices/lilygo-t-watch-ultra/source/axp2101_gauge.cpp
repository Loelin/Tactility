// SPDX-License-Identifier: Apache-2.0

// Board-local AXP2101 fuel-gauge bridge.
//
// The shared AXP2101 driver exposes charging state and battery voltage only, and the statusbar
// only creates its battery indicator when the power supply advertises
// POWER_SUPPLY_PROP_CAPACITY. This wrapper advertises capacity for this board's AXP2101 power
// supply and reads the PMIC's E-Gauge percentage register, leaving the shared driver untouched.

#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/error.h>

#include <freertos/FreeRTOS.h>

#include <cstring>

namespace {

constexpr uint8_t AXP2101_ADDRESS = 0x34;
constexpr uint8_t REG_CHARGE_GAUGE_WDT_CTRL = 0x18; // bit3: E-Gauge enable
constexpr uint8_t BIT_GAUGE_ENABLED = 1U << 3U;
constexpr uint8_t REG_BATTERY_PERCENTAGE = 0xA4;
constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

bool gauge_enabled = false;

// Must not call into the device registry here: power_supply_supports_property() is invoked from
// inside device_for_each_of_type() during boot, which holds the registry lock.
bool is_axp2101_power_supply(struct Device* device) {
    return device != nullptr && device->parent != nullptr &&
        std::strcmp(device->parent->name, "axp2101") == 0;
}

} // namespace

extern "C" {

bool __real_power_supply_supports_property(struct Device* device, enum PowerSupplyProperty property);
error_t __real_power_supply_get_property(struct Device* device, enum PowerSupplyProperty property, union PowerSupplyPropertyValue* out_value);

bool __wrap_power_supply_supports_property(struct Device* device, enum PowerSupplyProperty property) {
    if (property == POWER_SUPPLY_PROP_CAPACITY && is_axp2101_power_supply(device)) {
        return true;
    }
    return __real_power_supply_supports_property(device, property);
}

error_t __wrap_power_supply_get_property(struct Device* device, enum PowerSupplyProperty property, union PowerSupplyPropertyValue* out_value) {
    if (property != POWER_SUPPLY_PROP_CAPACITY || !is_axp2101_power_supply(device)) {
        return __real_power_supply_get_property(device, property, out_value);
    }
    if (out_value == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }

    struct Device* axp = device->parent;
    struct Device* bus = axp != nullptr ? axp->parent : nullptr;
    if (bus == nullptr) {
        return ERROR_NOT_FOUND;
    }

    // The gauge is enabled by default; set it explicitly once so capacity reads still work after
    // a power-on that left the bit cleared.
    if (!gauge_enabled) {
        gauge_enabled = i2c_controller_register8_set_bits(
            bus, AXP2101_ADDRESS, REG_CHARGE_GAUGE_WDT_CTRL, BIT_GAUGE_ENABLED, I2C_TIMEOUT
        ) == ERROR_NONE;
    }

    uint8_t percentage = 0;
    const error_t result = i2c_controller_register8_get(
        bus, AXP2101_ADDRESS, REG_BATTERY_PERCENTAGE, &percentage, I2C_TIMEOUT
    );
    if (result != ERROR_NONE) {
        return result;
    }

    out_value->int_value = percentage > 100 ? 100 : percentage;
    return ERROR_NONE;
}

}
