// SPDX-License-Identifier: Apache-2.0

// AXP2101 PWRKEY as a kernel button.
//
// The AXP2101 latches a short-press in IRQ status register 0x49 (bit 3) and holds its IRQ output
// low until the host writes 1 to that bit. The driver enables only that source, polls the status
// register and emits a normal Button event. Reading the status and clearing it is also what
// releases the shared IRQ line, so no board-local code has to poke the PMIC.

#include <drivers/axp2101_pwrkey.h>

#include <drivers/axp2101.h>

#include <tactility/concurrent/timer.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/button.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <atomic>
#include <new>

#define TAG "Axp2101Pwrkey"
#define GET_CONFIG(device) (static_cast<const Axp2101PwrkeyConfig*>((device)->config))

namespace {

constexpr uint8_t REG_INTEN1 = 0x40;
constexpr uint8_t REG_INTEN2 = 0x41;
constexpr uint8_t REG_INTEN3 = 0x42;
constexpr uint8_t REG_INTSTS1 = 0x48;
constexpr uint8_t REG_INTSTS2 = 0x49;
constexpr uint8_t REG_INTSTS3 = 0x4A;

// IRQ enable/status register 2, bit 3 = PWRKEY short press.
constexpr uint8_t PWRKEY_SHORT_PRESS_BIT = 1U << 3U;

constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);
constexpr TickType_t POLL_INTERVAL = pdMS_TO_TICKS(20);
// The AXP only reports a short press after the key is released, so there is no physical "held"
// state to poll. Keep the logical press asserted long enough for a 20 ms standby poll to see it.
constexpr uint32_t PRESS_HOLD_MS = 250;

struct Internal {
    Device* device;
    Device* i2c;
    uint8_t address;
    const Axp2101PwrkeyConfig* config;
    Timer* timer;
    std::atomic<bool> stopping { false };
    std::atomic<uint32_t> pressed_until { 0 };
    std::atomic<bool> fence_reached { false };
};

error_t write_register(Internal* internal, uint8_t reg, uint8_t value) {
    return i2c_controller_register8_set(internal->i2c, internal->address, reg, value, I2C_TIMEOUT);
}

error_t clear_irq_status(Internal* internal) {
    // Status bits are write-1-to-clear.
    error_t error = write_register(internal, REG_INTSTS1, 0xFF);
    if (error == ERROR_NONE) {
        error = write_register(internal, REG_INTSTS2, 0xFF);
    }
    if (error == ERROR_NONE) {
        error = write_register(internal, REG_INTSTS3, 0xFF);
    }
    return error;
}

error_t configure_irq(Internal* internal) {
    // Mask every source except PWRKEY short press. Other AXP events must not keep the shared IRQ
    // line asserted or wake the host by accident.
    error_t error = write_register(internal, REG_INTEN1, 0x00);
    if (error == ERROR_NONE) {
        error = write_register(internal, REG_INTEN2, PWRKEY_SHORT_PRESS_BIT);
    }
    if (error == ERROR_NONE) {
        error = write_register(internal, REG_INTEN3, 0x00);
    }
    if (error == ERROR_NONE) {
        error = clear_irq_status(internal);
    }
    return error;
}

void emit(Internal* internal, ButtonValue value, uint32_t timestamp_ms) {
    button_emit_event(internal->device, ButtonEvent {
        .code = static_cast<uint16_t>(internal->config->short_press_code),
        .value = value,
        .timestamp_ms = timestamp_ms,
    });
}

void on_timer(void* context) {
    auto* internal = static_cast<Internal*>(context);
    if (internal->stopping.load(std::memory_order_acquire)) {
        return;
    }

    uint8_t status = 0;
    if (i2c_controller_register8_get(internal->i2c, internal->address, REG_INTSTS2, &status, I2C_TIMEOUT) != ERROR_NONE) {
        return;
    }
    if ((status & PWRKEY_SHORT_PRESS_BIT) == 0) {
        return;
    }

    // Clear before emitting: this releases the AXP IRQ line.
    if (write_register(internal, REG_INTSTS2, PWRKEY_SHORT_PRESS_BIT) != ERROR_NONE) {
        return;
    }

    const uint32_t now = static_cast<uint32_t>(get_millis());
    internal->pressed_until.store(now + PRESS_HOLD_MS, std::memory_order_release);
    emit(internal, BUTTON_VALUE_PRESSED, now);
    emit(internal, BUTTON_VALUE_RELEASED, now);
}

error_t is_pressed(Device* device, bool* out_pressed) {
    auto* internal = static_cast<Internal*>(device_get_driver_data(device));
    if (internal == nullptr) {
        return ERROR_INVALID_STATE;
    }
    const uint32_t now = static_cast<uint32_t>(get_millis());
    const uint32_t until = internal->pressed_until.load(std::memory_order_acquire);
    *out_pressed = static_cast<int32_t>(now - until) < 0;
    return ERROR_NONE;
}

constexpr ButtonApi AXP2101_PWRKEY_BUTTON_API = {
    .is_pressed = is_pressed,
};

void mark_fence_reached(void* context, uint32_t) {
    static_cast<Internal*>(context)->fence_reached.store(true, std::memory_order_release);
}

/** The timer daemon runs its queue in order, so after the fence everything that was queued before it has run. */
void wait_for_timer_daemon(Internal* internal) {
    internal->fence_reached.store(false, std::memory_order_release);
    if (timer_set_pending_callback(nullptr, mark_fence_reached, internal, 0, portMAX_DELAY) == ERROR_NONE) {
        while (!internal->fence_reached.load(std::memory_order_acquire)) {
            delay_ticks(1);
        }
    }
}

error_t start(Device* device) {
    Device* parent = device_get_parent(device);
    if (parent == nullptr || !device_is_compatible(parent, "x-powers,axp2101")) {
        LOG_E(TAG, "Parent is not an AXP2101");
        return ERROR_INVALID_STATE;
    }

    Device* i2c = device_get_parent(parent);
    if (i2c == nullptr || device_get_type(i2c) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "AXP2101 parent is not an I2C controller");
        return ERROR_INVALID_STATE;
    }

    const auto* axp_config = static_cast<const Axp2101Config*>(parent->config);
    if (axp_config == nullptr) {
        return ERROR_INVALID_STATE;
    }

    auto* internal = new(std::nothrow) Internal {
        .device = device,
        .i2c = i2c,
        .address = axp_config->address,
        .config = GET_CONFIG(device),
        .timer = nullptr,
    };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = configure_irq(internal);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure PWRKEY interrupt: %s", error_to_string(error));
        delete internal;
        return error;
    }

    internal->timer = timer_alloc(TIMER_TYPE_PERIODIC, POLL_INTERVAL, on_timer, internal);
    if (internal->timer == nullptr) {
        delete internal;
        return ERROR_OUT_OF_MEMORY;
    }
    if (timer_start(internal->timer) != ERROR_NONE) {
        timer_free(internal->timer);
        delete internal;
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

error_t stop(Device* device) {
    auto* internal = static_cast<Internal*>(device_get_driver_data(device));
    if (internal == nullptr) {
        return ERROR_NONE;
    }
    device_set_driver_data(device, nullptr);

    internal->stopping.store(true, std::memory_order_release);
    timer_stop(internal->timer);
    wait_for_timer_daemon(internal);
    timer_free(internal->timer);

    // Leave the PMIC IRQ masked; a suspend/sleep path re-enables only what it needs.
    write_register(internal, REG_INTEN2, 0x00);
    clear_irq_status(internal);

    delete internal;
    return ERROR_NONE;
}

} // namespace

extern "C" {

Driver axp2101_pwrkey_driver = {
    .name = "axp2101-pwrkey",
    .compatible = (const char*[]) { "x-powers,axp2101-pwrkey", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &AXP2101_PWRKEY_BUTTON_API,
    .device_type = &BUTTON_TYPE,
    .internal = nullptr
};

} // extern "C"
