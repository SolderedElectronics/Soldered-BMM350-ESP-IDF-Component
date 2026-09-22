/**
 * @file soldered_bmm350.h
 * @brief Public API for the soldered-bmm350 component
 *
 * ESP-IDF driver for the Soldered BMM350 geomagnetic sensor breakout board
 * over I2C. It is a thin, snake_case wrapper around Bosch's
 * BMM350_SensorAPI, which lives unmodified in bmm350_api/. Functions here are
 * prefixed soldered_bmm350_ rather than bmm350_ to keep naming consistent
 * with the rest of Soldered's ESP-IDF components.
 *
 * The I2C bus belongs to the application, not to this driver, so that other
 * Qwiic devices can share it. Create it with i2c_new_master_bus() and hand the
 * handle to soldered_bmm350_init().
 *
 * @author Soldered Electronics
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "bmm350.h"
#include "bmm350_defs.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/** Returned by soldered_bmm350_check_status() when the last call failed */
#define SOLDERED_BMM350_ERROR INT8_C(-1)

/** I2C clock the sensor is driven at unless soldered_bmm350_init_with_config() says otherwise */
#define SOLDERED_BMM350_DEFAULT_SCL_SPEED_HZ 400000

/** How long a single I2C transaction may take before it is given up on */
#define SOLDERED_BMM350_I2C_TIMEOUT_MS 1000

/**
 * @brief One magnetometer/temperature reading
 */
typedef struct {
    float mag_x;       /**< Compensated magnetometer X reading in microtesla */
    float mag_y;       /**< Compensated magnetometer Y reading in microtesla */
    float mag_z;       /**< Compensated magnetometer Z reading in microtesla */
    float temperature; /**< Temperature in degrees Celsius */
} bmm350_data_t;

/**
 * @brief Optional settings of a BMM350, passed to soldered_bmm350_init_with_config()
 */
typedef struct {
    uint8_t i2c_addr;      /**< I2C address, BMM350_I2C_ADSEL_SET_LOW or BMM350_I2C_ADSEL_SET_HIGH */
    uint32_t scl_speed_hz; /**< I2C clock in Hz, 0 for ::SOLDERED_BMM350_DEFAULT_SCL_SPEED_HZ */
} bmm350_config_t;

/**
 * @brief Handle for one BMM350 breakout board
 *
 * Create one per breakout board. All fields are managed by the driver; treat
 * the struct as opaque and read state through the accessor functions.
 */
typedef struct {
    i2c_master_dev_handle_t i2c_dev; /**< I2C device handle, created by soldered_bmm350_init() */
    uint8_t i2c_addr;                /**< I2C address the sensor answers on */

    int8_t status; /**< Bosch Sensor API result code of the last executed call */

    struct bmm350_dev sensor; /**< Bosch API device structure */
    bmm350_data_t data;       /**< Last reading fetched by soldered_bmm350_get_sensor_data() */
} bmm350_t;

/**
 * @brief Attach a BMM350 to an already initialized I2C bus
 *
 * Adds the sensor as a device on `bus`, initializes it, and leaves it in
 * normal power mode with 100Hz output data rate, 4x averaging and all axes
 * enabled. The bus itself must already exist, created with
 * i2c_new_master_bus(); this leaves it free to be shared with other devices.
 *
 * Runs at ::SOLDERED_BMM350_DEFAULT_SCL_SPEED_HZ; use
 * soldered_bmm350_init_with_config() to pick a different clock.
 *
 * @param[out] dev Handle to initialize
 * @param[in] bus I2C bus the breakout is wired to, previously initialized with
 *                i2c_new_master_bus()
 * @param[in] i2c_addr I2C address of the sensor, BMM350_I2C_ADSEL_SET_LOW or
 *                     BMM350_I2C_ADSEL_SET_HIGH depending on the state of the ADSEL pin
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL argument,
 *         ESP_ERR_NOT_FOUND if the sensor did not answer, or the error
 *         returned by i2c_master_bus_add_device(). On ESP_ERR_NOT_FOUND the
 *         Bosch API result is left in `dev->status`
 */
esp_err_t soldered_bmm350_init(bmm350_t *dev, i2c_master_bus_handle_t bus, uint8_t i2c_addr);

/**
 * @brief Attach a BMM350 to an already initialized I2C bus with custom settings
 *
 * Same as soldered_bmm350_init(), but lets you pick the I2C clock.
 *
 * @param[out] dev Handle to initialize
 * @param[in] bus I2C bus the breakout is wired to, previously initialized with
 *                i2c_new_master_bus()
 * @param[in] config Settings to apply, see ::bmm350_config_t
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL argument,
 *         ESP_ERR_NOT_FOUND if the sensor did not answer, or the error
 *         returned by i2c_master_bus_add_device()
 */
esp_err_t soldered_bmm350_init_with_config(bmm350_t *dev, i2c_master_bus_handle_t bus, const bmm350_config_t *config);

/**
 * @brief Detach the sensor from the I2C bus
 *
 * Does not deinitialize the bus itself, since the bus is owned by the caller.
 *
 * @param[in,out] dev Handle previously initialized with soldered_bmm350_init()
 *
 * @return ESP_OK on success, or the error returned by i2c_master_bus_rm_device()
 */
esp_err_t soldered_bmm350_deinit(bmm350_t *dev);

/**
 * @brief Trigger a soft reset of the sensor
 *
 * All settings are lost, so the sensor has to be reconfigured afterwards.
 *
 * @param[in,out] dev Handle
 *
 * @return BMM350_OK on success, a Bosch API error code otherwise
 */
int8_t soldered_bmm350_soft_reset(bmm350_t *dev);

/**
 * @brief Set the power mode
 *
 * @param[in,out] dev Handle
 * @param[in] mode BMM350_SUSPEND_MODE, BMM350_NORMAL_MODE, BMM350_FORCED_MODE
 *                 or BMM350_FORCED_MODE_FAST
 *
 * @return BMM350_OK on success, a Bosch API error code otherwise
 */
int8_t soldered_bmm350_set_mode(bmm350_t *dev, enum bmm350_power_modes mode);

/**
 * @brief Set output data rate and averaging (performance) configuration
 *
 * @param[in,out] dev Handle
 * @param[in] odr One of the BMM350_DATA_RATE_* enum values
 * @param[in] avg One of BMM350_NO_AVERAGING, BMM350_AVERAGING_2, BMM350_AVERAGING_4 or BMM350_AVERAGING_8
 *
 * @return BMM350_OK on success, a Bosch API error code otherwise
 */
int8_t soldered_bmm350_set_odr_performance(bmm350_t *dev, enum bmm350_data_rates odr,
                                           enum bmm350_performance_parameters avg);

/**
 * @brief Enable or disable individual measurement axes
 *
 * @param[in,out] dev Handle
 * @param[in] en_x Enable the X axis
 * @param[in] en_y Enable the Y axis
 * @param[in] en_z Enable the Z axis
 *
 * @return BMM350_OK on success, a Bosch API error code otherwise
 */
int8_t soldered_bmm350_enable_axes(bmm350_t *dev, bool en_x, bool en_y, bool en_z);

/**
 * @brief Get the data-ready interrupt status
 *
 * Useful whether you're polling it directly or checking it after the
 * sensor's physical interrupt pin fires.
 *
 * @param[in,out] dev Handle
 * @param[out] drdy_status Data-ready interrupt status
 *
 * @return BMM350_OK on success, a Bosch API error code otherwise
 */
int8_t soldered_bmm350_get_interrupt_status(bmm350_t *dev, uint8_t *drdy_status);

/**
 * @brief Enable or disable the data-ready interrupt
 *
 * @param[in,out] dev Handle
 * @param[in] enable Whether to enable the interrupt
 *
 * @return BMM350_OK on success, a Bosch API error code otherwise
 */
int8_t soldered_bmm350_enable_interrupt(bmm350_t *dev, bool enable);

/**
 * @brief Configure the behavior of the sensor's physical interrupt pin
 *
 * Does not enable the interrupt itself, see soldered_bmm350_enable_interrupt().
 *
 * @param[in,out] dev Handle
 * @param[in] latching BMM350_PULSED or BMM350_LATCHED
 * @param[in] polarity BMM350_ACTIVE_LOW or BMM350_ACTIVE_HIGH
 * @param[in] drive BMM350_INTR_OPEN_DRAIN or BMM350_INTR_PUSH_PULL
 * @param[in] map_to_pin Whether to map the data-ready interrupt to the physical pin
 *
 * @return BMM350_OK on success, a Bosch API error code otherwise
 */
int8_t soldered_bmm350_configure_interrupt(bmm350_t *dev, enum bmm350_intr_latch latching,
                                           enum bmm350_intr_polarity polarity, enum bmm350_intr_drive drive,
                                           bool map_to_pin);

/**
 * @brief Read a new compensated magnetometer/temperature measurement into `dev->data`
 *
 * @param[in,out] dev Handle
 *
 * @return BMM350_OK on success, a Bosch API error code otherwise
 */
int8_t soldered_bmm350_get_sensor_data(bmm350_t *dev);

/**
 * @brief Run the sensor's built-in self-test
 *
 * @param[in,out] dev Handle
 * @param[out] result Self-test result
 *
 * @return BMM350_OK on success, a Bosch API error code otherwise
 */
int8_t soldered_bmm350_perform_self_test(bmm350_t *dev, struct bmm350_self_test *result);

/**
 * @brief Check whether the last call failed
 *
 * @param[in] dev Handle
 *
 * @return ::SOLDERED_BMM350_ERROR if the last call failed, BMM350_OK otherwise
 */
int8_t soldered_bmm350_check_status(const bmm350_t *dev);

/**
 * @brief Get a brief text description of the last status code
 *
 * @param[in] dev Handle
 *
 * @return String describing the status code, an empty string when it is
 *         BMM350_OK. The string is static and does not have to be freed
 */
const char *soldered_bmm350_status_string(const bmm350_t *dev);

#ifdef __cplusplus
}
#endif
