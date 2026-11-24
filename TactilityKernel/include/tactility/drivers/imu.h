// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ImuAccelData {
    float ax, ay, az; // acceleration, in g
};

struct ImuGyroData {
    float gx, gy, gz; // angular rate, in °/s
};

/** A movement event reported by an IMU that can detect motion on its own. */
struct ImuMotionData {
    /** True when the sensor saw movement since the previous read. */
    bool detected;
};

/**
 * @brief API for IMU (accelerometer + gyroscope) drivers.
 *
 * @note read_accel(), read_gyro() and read_temperature() are each a single-register-window I2C
 * transaction. All three are optional per driver: a driver that can't service one leaves it
 * NULL, and the matching imu_read_*() wrapper returns ERROR_NOT_SUPPORTED.
 */
struct ImuApi {
    /**
     * @brief Reads only the current accelerometer data.
     * @param[in] device the IMU device
     * @param[out] data pointer to store the current reading
     * @retval ERROR_NOT_SUPPORTED if the driver has no accel-only read path
     * @retval ERROR_NONE on success
     */
    error_t (*read_accel)(struct Device* device, struct ImuAccelData* data);

    /**
     * @brief Reads only the current gyroscope data.
     * @param[in] device the IMU device
     * @param[out] data pointer to store the current reading
     * @retval ERROR_NOT_SUPPORTED if the driver has no gyro-only read path
     * @retval ERROR_NONE on success
     */
    error_t (*read_gyro)(struct Device* device, struct ImuGyroData* data);

    /**
     * @brief Reads the chip's on-die temperature.
     * @param[in] device the IMU device
     * @param[out] temperature_c pointer to store the current reading, in °C
     * @retval ERROR_NOT_SUPPORTED if the driver has no temperature sensor/register
     * @retval ERROR_NONE on success
     */
    error_t (*read_temperature)(struct Device* device, float* temperature_c);

    /**
     * @brief Reads and clears the movement latch of the sensor, if it implements one.
     *
     * A driver can set this when the sensor detects movement by itself (for example a hardware
     * motion/wake gesture engine). Training the sensor and reading its FIFO happens inside the
     * driver, so callers only poll this at a low rate.
     *
     * @param[in] device the IMU device
     * @param[out] data set to whether movement was detected since the previous read
     * @retval ERROR_NOT_SUPPORTED if the driver has no movement detection
     * @retval ERROR_NONE on success
     */
    error_t (*read_motion)(struct Device* device, struct ImuMotionData* data);
};

/**
 * @brief Reads only the current accelerometer data using the specified IMU device.
 * @retval ERROR_NOT_SUPPORTED if the driver has no accel-only read path
 */
error_t imu_read_accel(struct Device* device, struct ImuAccelData* data);

/**
 * @brief Reads only the current gyroscope data using the specified IMU device.
 * @retval ERROR_NOT_SUPPORTED if the driver has no gyro-only read path
 */
error_t imu_read_gyro(struct Device* device, struct ImuGyroData* data);

/**
 * @brief Reads the chip's on-die temperature using the specified IMU device, in °C.
 * @retval ERROR_NOT_SUPPORTED if the driver has no temperature sensor/register
 */
error_t imu_read_temperature(struct Device* device, float* temperature_c);

/**
 * @brief Reads and clears the movement latch of the IMU, if it has movement detection.
 * @retval ERROR_NOT_SUPPORTED if the driver has no movement detection
 */
error_t imu_read_motion(struct Device* device, struct ImuMotionData* data);

/** @return true when the IMU driver implements movement detection */
bool imu_supports_motion(struct Device* device);

extern const struct DeviceType IMU_TYPE;

#ifdef __cplusplus
}
#endif
