// SPDX-License-Identifier: Apache-2.0
#include <drivers/bhi260ap.h>
#include <bhi260ap_module.h>

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/imu.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern "C" {
#include "bhy2.h"
#include "bhy2_parse.h"
}

#if defined(BHI260AP_HAVE_FIRMWARE)
// Proprietary Bosch KLIO firmware, deliberately not committed. See private/firmware/README.md.
#include <firmware/bosch_bhi260_klio.h>
#else
static const unsigned char bhi260ap_placeholder_firmware[1] = { 0 };
static const unsigned char* bosch_firmware_image = bhi260ap_placeholder_firmware;
static const unsigned int bosch_firmware_size = 0;
#endif

constexpr auto* TAG = "BHI260AP";

#define GET_CONFIG(device) (static_cast<const Bhi260apConfig*>((device)->config))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(250);
static constexpr uint32_t MAX_READ_WRITE_LENGTH = 256;
static constexpr uint32_t FIFO_BUFFER_SIZE = 2048;

// Bosch reports the corrected sensors as int16. These are the same fixed-point scales
// SensorLib applies (accel: 1/4096 g per LSB, gyro: 2000/32768 deg/s per LSB).
static constexpr float ACCEL_SCALE = 1.0f / 4096.0f;
static constexpr float GYRO_SCALE = 2000.0f / 32768.0f;

// A change of the gravity vector between two 100 Hz samples that is larger than this counts as
// movement. Resting noise stays well below it, while turning the wrist (or a bump) crosses it.
static constexpr float MOTION_DELTA_SQUARED = 0.15f * 0.15f;

struct Bhi260apInternal {
    Device* i2c;
    uint8_t address;
    struct bhy2_dev bhy2;
    SemaphoreHandle_t lock;
    uint8_t* fifo_buffer;
    bool ready;
    bool has_accel;
    bool has_gyro;
    float ax, ay, az;
    float gx, gy, gz;
    bool has_prev_accel;
    float prev_ax, prev_ay, prev_az;
    bool motion_detected;
};

// region Bosch BHY2 transport

static int8_t transport_read(uint8_t reg, uint8_t* data, uint32_t length, void* intf_ptr) {
    auto* internal = static_cast<Bhi260apInternal*>(intf_ptr);
    if (length == 0) {
        return BHY2_OK;
    }

    if (i2c_controller_write_read(internal->i2c, internal->address, &reg, 1, data, length, I2C_TIMEOUT) != ERROR_NONE) {
        return BHY2_E_IO;
    }
    return BHY2_OK;
}

static int8_t transport_write(uint8_t reg, const uint8_t* data, uint32_t length, void* intf_ptr) {
    auto* internal = static_cast<Bhi260apInternal*>(intf_ptr);

    error_t result;
    if (length == 0) {
        result = i2c_controller_write(internal->i2c, internal->address, &reg, 1, I2C_TIMEOUT);
    } else {
        result = i2c_controller_write_register(internal->i2c, internal->address, reg, data, static_cast<uint16_t>(length), I2C_TIMEOUT);
    }
    return result == ERROR_NONE ? BHY2_OK : BHY2_E_IO;
}

static void transport_delay_us(uint32_t period_us, void*) {
    delay_micros(period_us);
}

// endregion

// region FIFO parsing

/** Applies the devicetree mount matrix (sensor frame -> device frame) to a 3-axis reading. */
static void applyMountMatrix(const Bhi260apConfig& config, float& x, float& y, float& z) {
    const int32_t* m = config.mount_matrix;
    const float rx = m[0] * x + m[1] * y + m[2] * z;
    const float ry = m[3] * x + m[4] * y + m[5] * z;
    const float rz = m[6] * x + m[7] * y + m[8] * z;
    x = rx;
    y = ry;
    z = rz;
}

static void fifo_parse_callback(const struct bhy2_fifo_parse_data_info* info, void* private_data) {
    auto* internal = static_cast<Bhi260apInternal*>(private_data);
    if (info->data_size < 6) {
        return;
    }

    struct bhy2_data_xyz value = {};
    bhy2_parse_xyz(info->data_ptr, &value);

    if (info->sensor_id == BHY2_SENSOR_ID_ACC) {
        const float ax = static_cast<float>(value.x) * ACCEL_SCALE;
        const float ay = static_cast<float>(value.y) * ACCEL_SCALE;
        const float az = static_cast<float>(value.z) * ACCEL_SCALE;

        if (internal->has_prev_accel) {
            const float dx = ax - internal->prev_ax;
            const float dy = ay - internal->prev_ay;
            const float dz = az - internal->prev_az;
            if ((dx * dx) + (dy * dy) + (dz * dz) >= MOTION_DELTA_SQUARED) {
                internal->motion_detected = true;
            }
        }

        internal->ax = ax;
        internal->ay = ay;
        internal->az = az;
        internal->prev_ax = ax;
        internal->prev_ay = ay;
        internal->prev_az = az;
        internal->has_prev_accel = true;
        internal->has_accel = true;
    } else if (info->sensor_id == BHY2_SENSOR_ID_GYRO) {
        internal->gx = static_cast<float>(value.x) * GYRO_SCALE;
        internal->gy = static_cast<float>(value.y) * GYRO_SCALE;
        internal->gz = static_cast<float>(value.z) * GYRO_SCALE;
        internal->has_gyro = true;
    }
}

static error_t poll_fifo(Bhi260apInternal* internal) {
    if (!internal->ready) {
        return ERROR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(internal->lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ERROR_TIMEOUT;
    }

    int8_t result = bhy2_get_and_process_fifo(internal->fifo_buffer, FIFO_BUFFER_SIZE, &internal->bhy2);

    xSemaphoreGive(internal->lock);
    return result == BHY2_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// endregion

// region Driver lifecycle

static void destroy(Bhi260apInternal* internal) {
    if (internal == nullptr) {
        return;
    }
    if (internal->lock != nullptr) {
        vSemaphoreDelete(internal->lock);
    }
    free(internal->fifo_buffer);
    free(internal);
}

static bool bring_up(Bhi260apInternal* internal, const Bhi260apConfig* config) {
    int8_t result = bhy2_init(
        BHY2_I2C_INTERFACE,
        transport_read,
        transport_write,
        transport_delay_us,
        MAX_READ_WRITE_LENGTH,
        internal,
        &internal->bhy2
    );
    if (result != BHY2_OK) {
        LOG_E(TAG, "bhy2_init failed (%d)", result);
        return false;
    }

    result = bhy2_soft_reset(&internal->bhy2);
    if (result != BHY2_OK) {
        LOG_E(TAG, "Soft reset failed (%d)", result);
        return false;
    }

    uint8_t chip_id = 0;
    result = bhy2_get_chip_id(&chip_id, &internal->bhy2);
    if (result != BHY2_OK) {
        LOG_E(TAG, "Chip ID read failed (%d)", result);
        return false;
    }
    LOG_I(TAG, "Chip ID 0x%02X", chip_id);
    if (chip_id != BHI260_CHIP_ID) {
        LOG_W(TAG, "Unexpected chip id 0x%02X (expected 0x%02X)", chip_id, BHI260_CHIP_ID);
    }

    result = bhy2_upload_firmware_to_ram(bosch_firmware_image, bosch_firmware_size, nullptr, nullptr, &internal->bhy2);
    if (result != BHY2_OK) {
        LOG_E(TAG, "Firmware upload failed (%d)", result);
        return false;
    }

    uint8_t sensor_error = 0;
    if (bhy2_get_error_value(&sensor_error, &internal->bhy2) == BHY2_OK && sensor_error != 0) {
        LOG_E(TAG, "Firmware upload error 0x%02X", sensor_error);
        return false;
    }

    // The upload alone does not start the firmware; the BHI260 must be told to boot from RAM.
    result = bhy2_boot_from_ram(&internal->bhy2);
    if (result != BHY2_OK) {
        LOG_E(TAG, "Boot from RAM failed (%d)", result);
        return false;
    }

    uint8_t boot_status = 0;
    if (bhy2_get_boot_status(&boot_status, &internal->bhy2) == BHY2_OK) {
        LOG_I(TAG, "Boot status 0x%02X", boot_status);
    }

    // Wait for the BSX firmware to boot (up to ~3 s).
    uint16_t kernel_version = 0;
    for (int attempt = 0; attempt < 300; ++attempt) {
        if (bhy2_get_kernel_version(&kernel_version, &internal->bhy2) == BHY2_OK && kernel_version != 0) {
            break;
        }
        delay_millis(10);
    }
    if (kernel_version == 0) {
        LOG_E(TAG, "Firmware did not boot");
        return false;
    }
    LOG_I(TAG, "Firmware booted (kernel %u)", kernel_version);

    bhy2_update_virtual_sensor_list(&internal->bhy2);

    bhy2_register_fifo_parse_callback(BHY2_SENSOR_ID_ACC, fifo_parse_callback, internal, &internal->bhy2);
    bhy2_register_fifo_parse_callback(BHY2_SENSOR_ID_GYRO, fifo_parse_callback, internal, &internal->bhy2);

    // Drain the meta-event backlog produced during boot.
    bhy2_get_and_process_fifo(internal->fifo_buffer, FIFO_BUFFER_SIZE, &internal->bhy2);

    constexpr float SAMPLE_RATE_HZ = 100.0f;
    constexpr uint32_t REPORT_LATENCY_MS = 0;
    if (bhy2_set_virt_sensor_cfg(BHY2_SENSOR_ID_ACC, SAMPLE_RATE_HZ, REPORT_LATENCY_MS, &internal->bhy2) != BHY2_OK) {
        LOG_W(TAG, "Failed to enable accelerometer");
    }
    if (bhy2_set_virt_sensor_cfg(BHY2_SENSOR_ID_GYRO, SAMPLE_RATE_HZ, REPORT_LATENCY_MS, &internal->bhy2) != BHY2_OK) {
        LOG_W(TAG, "Failed to enable gyroscope");
    }

    internal->ready = true;
    return true;
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Bhi260apInternal*>(calloc(1, sizeof(Bhi260apInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->i2c = parent;
    internal->address = config->address;
    internal->lock = xSemaphoreCreateMutex();
    internal->fifo_buffer = static_cast<uint8_t*>(malloc(FIFO_BUFFER_SIZE));
    if (internal->lock == nullptr || internal->fifo_buffer == nullptr) {
        destroy(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    if (bosch_firmware_size == 0) {
        LOG_W(TAG, "No KLIO firmware blob present; IMU reads will report ERROR_NOT_SUPPORTED");
        LOG_W(TAG, "Run Drivers/bhi260ap-module/fetch_firmware.py and rebuild");
        device_set_driver_data(device, internal);
        return ERROR_NONE;
    }

    // The IMU is optional for boot: never fail the device start (a mandatory device failure aborts
    // the whole kernel), just keep the device alive and report ERROR_NOT_SUPPORTED for reads.
    if (!bring_up(internal, config)) {
        LOG_E(TAG, "IMU unavailable; continuing without it");
        internal->ready = false;
        device_set_driver_data(device, internal);
        return ERROR_NONE;
    }

    device_set_driver_data(device, internal);
    LOG_I(TAG, "Ready");
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Bhi260apInternal*>(device_get_driver_data(device));
    device_set_driver_data(device, nullptr);
    destroy(internal);
    return ERROR_NONE;
}

// endregion

// region ImuApi

static error_t bhi260ap_read_accel(Device* device, struct ImuAccelData* data) {
    auto* internal = static_cast<Bhi260apInternal*>(device_get_driver_data(device));
    error_t result = poll_fifo(internal);
    if (result != ERROR_NONE) {
        return result;
    }
    if (!internal->has_accel) {
        return ERROR_NOT_SUPPORTED;
    }

    data->ax = internal->ax;
    data->ay = internal->ay;
    data->az = internal->az;
    applyMountMatrix(*GET_CONFIG(device), data->ax, data->ay, data->az);
    return ERROR_NONE;
}

static error_t bhi260ap_read_gyro(Device* device, struct ImuGyroData* data) {
    auto* internal = static_cast<Bhi260apInternal*>(device_get_driver_data(device));
    error_t result = poll_fifo(internal);
    if (result != ERROR_NONE) {
        return result;
    }
    if (!internal->has_gyro) {
        return ERROR_NOT_SUPPORTED;
    }

    data->gx = internal->gx;
    data->gy = internal->gy;
    data->gz = internal->gz;
    applyMountMatrix(*GET_CONFIG(device), data->gx, data->gy, data->gz);
    return ERROR_NONE;
}

static error_t bhi260ap_read_motion(Device* device, struct ImuMotionData* data) {
    auto* internal = static_cast<Bhi260apInternal*>(device_get_driver_data(device));
    error_t result = poll_fifo(internal);
    if (result != ERROR_NONE) {
        return result;
    }
    if (!internal->has_accel) {
        return ERROR_NOT_SUPPORTED;
    }

    data->detected = internal->motion_detected;
    internal->motion_detected = false;
    return ERROR_NONE;
}

// The BHI260AP's temperature is a separate virtual sensor; not enabled here.
static const ImuApi BHI260AP_IMU_API = {
    .read_accel = bhi260ap_read_accel,
    .read_gyro = bhi260ap_read_gyro,
    .read_temperature = nullptr,
    .read_motion = bhi260ap_read_motion,
};

Driver bhi260ap_driver = {
    .name = "bhi260ap",
    .compatible = (const char*[]) { "bosch,bhi260ap", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &BHI260AP_IMU_API,
    .device_type = &IMU_TYPE,
    .internal = nullptr
};

// endregion
