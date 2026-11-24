// SPDX-License-Identifier: Apache-2.0
#include <drivers/cst92xx.h>

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/touch.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <cstdlib>
#include <utility>

#define TAG "CST92xx"
#define GET_CONFIG(device) (static_cast<const Cst92xxConfig*>((device)->config))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

// Register/command constants from the vendor's TouchDrvCST92xx (SensorLib).
static constexpr uint8_t REG_READ_HI = 0xD0;
static constexpr uint8_t REG_READ_LO = 0x00;
static constexpr uint8_t CST92XX_ACK = 0xAB;
static constexpr uint8_t MAX_FINGER_NUM = 2;
static constexpr uint8_t EVENT_DOWN = 0x06;

struct Cst92xxInternal {
    int16_t last_x;
    int16_t last_y;
    bool has_point;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
};

static bool write_bytes(Device* i2c, uint8_t address, const uint8_t* data, size_t length) {
    return i2c_controller_write(i2c, address, data, length, I2C_TIMEOUT) == ERROR_NONE;
}

static bool read_bytes(Device* i2c, uint8_t address, uint8_t* data, size_t length) {
    return i2c_controller_read(i2c, address, data, length, I2C_TIMEOUT) == ERROR_NONE;
}

static bool write_then_read(Device* i2c, uint8_t address, const uint8_t* command, size_t command_length, uint8_t* data, size_t data_length) {
    return write_bytes(i2c, address, command, command_length) &&
        read_bytes(i2c, address, data, data_length);
}

static void perform_hardware_reset(const Cst92xxConfig* config) {
    if (config->pin_reset.gpio_controller == nullptr) {
        return;
    }

    // Drive the raw physical level: the reset line is active-low, so low asserts and high releases.
    // No GPIO_FLAG_ACTIVE_LOW here because expanders can't invert driven outputs.
    auto* rst = gpio_descriptor_acquire(config->pin_reset.gpio_controller, config->pin_reset.pin, GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
    if (rst == nullptr) {
        return;
    }

    gpio_descriptor_set_level(rst, false);
    delay_millis(10);
    gpio_descriptor_set_level(rst, true);
    gpio_descriptor_release(rst);
    // The reset pulse exits boot mode; give the controller time to re-init.
    delay_millis(80);
}

// region Driver lifecycle

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Cst92xxInternal*>(malloc(sizeof(Cst92xxInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->last_x = 0;
    internal->last_y = 0;
    internal->has_point = false;
    internal->swap_xy = config->swap_xy;
    internal->mirror_x = config->mirror_x;
    internal->mirror_y = config->mirror_y;

    perform_hardware_reset(config);
    delay_millis(30);

    // Enter command mode.
    const uint8_t command_mode[2] = { 0xD1, 0x01 };
    write_bytes(parent, config->address, command_mode, 2);
    delay_millis(10);

    // Firmware check code: bits 31..16 must read 0xCACA.
    const uint8_t read_check[2] = { 0xD1, 0xFC };
    uint8_t check[4] = {};
    bool ok = write_then_read(parent, config->address, read_check, 2, check, 4);
    uint32_t check_code = static_cast<uint32_t>(check[0]) |
        (static_cast<uint32_t>(check[1]) << 8) |
        (static_cast<uint32_t>(check[2]) << 16) |
        (static_cast<uint32_t>(check[3]) << 24);

    // Panel resolution.
    const uint8_t read_resolution[2] = { 0xD1, 0xF8 };
    uint8_t resolution[4] = {};
    ok = ok && write_then_read(parent, config->address, read_resolution, 2, resolution, 4);
    uint16_t resolution_x = static_cast<uint16_t>((static_cast<uint16_t>(resolution[1]) << 8) | resolution[0]);
    uint16_t resolution_y = static_cast<uint16_t>((static_cast<uint16_t>(resolution[3]) << 8) | resolution[2]);

    // Chip type: 0x9217 (CST9217) or 0x9220 (CST9220).
    const uint8_t read_type[2] = { 0xD2, 0x04 };
    uint8_t type[4] = {};
    ok = ok && write_then_read(parent, config->address, read_type, 2, type, 4);
    uint16_t chip_type = static_cast<uint16_t>((static_cast<uint16_t>(type[3]) << 8) | type[2]);

    if (ok && (check_code & 0xFFFF0000u) == 0xCACA0000u && (chip_type == 0x9217 || chip_type == 0x9220)) {
        LOG_I(TAG, "Initialised (chip 0x%04X, %ux%u)", chip_type, resolution_x, resolution_y);
    } else {
        LOG_W(TAG, "Identity not confirmed (check=0x%08lx, chip=0x%04X)", static_cast<unsigned long>(check_code), chip_type);
    }

    // Normal reporting mode.
    const uint8_t normal_mode[2] = { 0xD1, 0x09 };
    write_bytes(parent, config->address, normal_mode, 2);

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Cst92xxInternal*>(device_get_driver_data(device));
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region TouchApi

static error_t cst92xx_read_data(Device* device, TickType_t /*timeout*/) {
    auto* internal = static_cast<Cst92xxInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    auto* parent = device_get_parent(device);

    uint8_t frame[MAX_FINGER_NUM * 5 + 5] = {};
    const uint8_t read_command[2] = { REG_READ_HI, REG_READ_LO };
    if (!write_then_read(parent, config->address, read_command, 2, frame, sizeof(frame))) {
        internal->has_point = false;
        return ERROR_RESOURCE;
    }

    // Acknowledge/clear the frame after every read.
    const uint8_t ack_command[3] = { REG_READ_HI, REG_READ_LO, CST92XX_ACK };
    write_bytes(parent, config->address, ack_command, 3);

    // Frame layout: point 0 at [0..4], count at [5], ACK marker at [6], point 1 at [7..11].
    if (frame[0] == CST92XX_ACK || frame[6] != CST92XX_ACK || frame[0] == 0x00) {
        internal->has_point = false;
        return ERROR_NONE;
    }

    const uint8_t point_count = frame[5] & 0x7F;
    if (point_count == 0 || point_count > MAX_FINGER_NUM) {
        internal->has_point = false;
        return ERROR_NONE;
    }

    // Single-touch driver: use the first point.
    const uint8_t* point = frame;
    const uint8_t event = point[0] & 0x0F;
    if (event != EVENT_DOWN) {
        internal->has_point = false;
        return ERROR_NONE;
    }

    int16_t raw_x = static_cast<int16_t>((static_cast<uint16_t>(point[1]) << 4) | (point[3] >> 4));
    int16_t raw_y = static_cast<int16_t>((static_cast<uint16_t>(point[2]) << 4) | (point[3] & 0x0F));

    if (internal->swap_xy) {
        std::swap(raw_x, raw_y);
    }
    if (internal->mirror_x) {
        raw_x = static_cast<int16_t>(config->x_max - 1 - raw_x);
    }
    if (internal->mirror_y) {
        raw_y = static_cast<int16_t>(config->y_max - 1 - raw_y);
    }

    internal->last_x = raw_x;
    internal->last_y = raw_y;
    internal->has_point = true;
    return ERROR_NONE;
}

static bool cst92xx_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Cst92xxInternal*>(device_get_driver_data(device));
    if (!internal->has_point || max_point_count < 1) {
        *point_count = 0;
        return false;
    }

    x[0] = static_cast<uint16_t>(internal->last_x);
    y[0] = static_cast<uint16_t>(internal->last_y);
    if (strength != nullptr) {
        strength[0] = 0;
    }
    *point_count = 1;
    return true;
}

static error_t cst92xx_set_swap_xy(Device* device, bool swap) {
    static_cast<Cst92xxInternal*>(device_get_driver_data(device))->swap_xy = swap;
    return ERROR_NONE;
}

static error_t cst92xx_get_swap_xy(Device* device, bool* swap) {
    *swap = static_cast<Cst92xxInternal*>(device_get_driver_data(device))->swap_xy;
    return ERROR_NONE;
}

static error_t cst92xx_set_mirror_x(Device* device, bool mirror) {
    static_cast<Cst92xxInternal*>(device_get_driver_data(device))->mirror_x = mirror;
    return ERROR_NONE;
}

static error_t cst92xx_get_mirror_x(Device* device, bool* mirror) {
    *mirror = static_cast<Cst92xxInternal*>(device_get_driver_data(device))->mirror_x;
    return ERROR_NONE;
}

static error_t cst92xx_set_mirror_y(Device* device, bool mirror) {
    static_cast<Cst92xxInternal*>(device_get_driver_data(device))->mirror_y = mirror;
    return ERROR_NONE;
}

static error_t cst92xx_get_mirror_y(Device* device, bool* mirror) {
    *mirror = static_cast<Cst92xxInternal*>(device_get_driver_data(device))->mirror_y;
    return ERROR_NONE;
}

// enter_sleep/exit_sleep are not exposed: the ported protocol has no equivalent commands.

// endregion

static const TouchApi cst92xx_touch_api = {
    .enter_sleep = nullptr,
    .exit_sleep = nullptr,
    .read_data = cst92xx_read_data,
    .get_touched_points = cst92xx_get_touched_points,
    .set_swap_xy = cst92xx_set_swap_xy,
    .get_swap_xy = cst92xx_get_swap_xy,
    .set_mirror_x = cst92xx_set_mirror_x,
    .get_mirror_x = cst92xx_get_mirror_x,
    .set_mirror_y = cst92xx_set_mirror_y,
    .get_mirror_y = cst92xx_get_mirror_y,
};

Driver cst92xx_driver = {
    .name = "cst92xx",
    .compatible = (const char*[]) { "hynitron,cst92xx", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &cst92xx_touch_api,
    .device_type = &TOUCH_TYPE,
    .internal = nullptr
};
