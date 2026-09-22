/**
 * @file soldered_bmm350.c
 * @brief Implementation for the soldered-bmm350 component
 *
 * Wraps Bosch's BMM350_SensorAPI, which lives unmodified in bmm350_api/, in an
 * ESP-IDF flavoured I2C driver.
 *
 * @author Soldered Electronics
 */

#include <string.h>
#include "esp_rom_sys.h"
#include "soldered_bmm350.h"

// *****************************************************************************
// Section: Bosch Sensor API callbacks
//
// The sensor API reaches the outside world through these three callbacks. The
// interface descriptor it hands back to them is the handle itself, so that they
// can both talk over I2C and record the ESP-IDF error code.

/**
 * @brief Delay callback handed to the Bosch API
 *
 * Busy-waits the whole period rather than splitting it across vTaskDelay()
 * ticks. The driver's power-mode transitions (magnetic reset in particular)
 * rely on these delays being at least as long as requested, and
 * vTaskDelay()'s tick quantization (10ms by default) can return early enough
 * to make a 14-18ms delay come in short, which then reads the PMU command
 * status register before the chip has actually finished the command.
 *
 * @param[in] period_us Duration of the delay in microseconds
 * @param[in] intf_ptr Pointer to the handle, unused here
 */
static void bmm350_delay_us_cb(uint32_t period_us, void *intf_ptr)
{
    (void)intf_ptr;

    esp_rom_delay_us(period_us);
}

/**
 * @brief I2C write callback handed to the Bosch API
 *
 * @param[in] reg_addr Register address of the sensor
 * @param[in] reg_data Data to be written to the sensor
 * @param[in] length Length of the transfer
 * @param[in] intf_ptr Pointer to the handle
 *
 * @return BMM350_OK if successful, a Bosch API error code otherwise
 */
static BMM350_INTF_RET_TYPE bmm350_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length,
                                             void *intf_ptr)
{
    bmm350_t *dev = (bmm350_t *)intf_ptr;
    uint8_t buf[32];
    esp_err_t err;

    if ((dev == NULL) || (dev->i2c_dev == NULL)) {
        return BMM350_E_NULL_PTR;
    }

    if (length + 1 > sizeof(buf)) {
        return BMM350_E_COM_FAIL;
    }

    /* The register address and the data go out as one transaction, so build
     * them into a single buffer first. */
    buf[0] = reg_addr;
    memcpy(&buf[1], reg_data, length);

    err = i2c_master_transmit(dev->i2c_dev, buf, length + 1, SOLDERED_BMM350_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return BMM350_E_COM_FAIL;
    }

    return BMM350_OK;
}

/**
 * @brief I2C read callback handed to the Bosch API
 *
 * @param[in] reg_addr Register address of the sensor
 * @param[out] reg_data Buffer the data is read into
 * @param[in] length Length of the transfer
 * @param[in] intf_ptr Pointer to the handle
 *
 * @return BMM350_OK if successful, a Bosch API error code otherwise
 */
static BMM350_INTF_RET_TYPE bmm350_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr)
{
    bmm350_t *dev = (bmm350_t *)intf_ptr;
    esp_err_t err;

    if ((dev == NULL) || (dev->i2c_dev == NULL)) {
        return BMM350_E_NULL_PTR;
    }

    err = i2c_master_transmit_receive(dev->i2c_dev, &reg_addr, 1, reg_data, length, SOLDERED_BMM350_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return BMM350_E_COM_FAIL;
    }

    return BMM350_OK;
}

// *****************************************************************************
// Section: Initialization

esp_err_t soldered_bmm350_init(bmm350_t *dev, i2c_master_bus_handle_t bus, uint8_t i2c_addr)
{
    bmm350_config_t config = {
        .i2c_addr = i2c_addr,
        .scl_speed_hz = SOLDERED_BMM350_DEFAULT_SCL_SPEED_HZ,
    };

    return soldered_bmm350_init_with_config(dev, bus, &config);
}

esp_err_t soldered_bmm350_init_with_config(bmm350_t *dev, i2c_master_bus_handle_t bus, const bmm350_config_t *config)
{
    esp_err_t err;

    if ((dev == NULL) || (bus == NULL) || (config == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));
    dev->i2c_addr = config->i2c_addr;
    dev->status = BMM350_OK;

    i2c_device_config_t i2c_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_addr,
        .scl_speed_hz = config->scl_speed_hz ? config->scl_speed_hz : SOLDERED_BMM350_DEFAULT_SCL_SPEED_HZ,
    };

    err = i2c_master_bus_add_device(bus, &i2c_conf, &dev->i2c_dev);
    if (err != ESP_OK) {
        return err;
    }

    dev->sensor.read = bmm350_i2c_read;
    dev->sensor.write = bmm350_i2c_write;
    dev->sensor.delay_us = bmm350_delay_us_cb;
    dev->sensor.intf_ptr = dev;

    dev->status = bmm350_init(&dev->sensor);
    if (dev->status == BMM350_OK) {
        dev->status = soldered_bmm350_set_odr_performance(dev, BMM350_DATA_RATE_100HZ, BMM350_AVERAGING_4);
    }
    if (dev->status == BMM350_OK) {
        dev->status = soldered_bmm350_enable_axes(dev, true, true, true);
    }
    if (dev->status == BMM350_OK) {
        dev->status = bmm350_set_powermode(BMM350_NORMAL_MODE, &dev->sensor);
    }

    if (dev->status != BMM350_OK) {
        i2c_master_bus_rm_device(dev->i2c_dev);
        dev->i2c_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t soldered_bmm350_deinit(bmm350_t *dev)
{
    esp_err_t err;

    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dev->i2c_dev == NULL) {
        return ESP_OK;
    }

    err = i2c_master_bus_rm_device(dev->i2c_dev);
    dev->i2c_dev = NULL;

    return err;
}

// *****************************************************************************
// Section: Power mode

int8_t soldered_bmm350_soft_reset(bmm350_t *dev)
{
    dev->status = bmm350_soft_reset(&dev->sensor);

    return dev->status;
}

int8_t soldered_bmm350_set_mode(bmm350_t *dev, enum bmm350_power_modes mode)
{
    dev->status = bmm350_set_powermode(mode, &dev->sensor);

    return dev->status;
}

// *****************************************************************************
// Section: Configuration

int8_t soldered_bmm350_set_odr_performance(bmm350_t *dev, enum bmm350_data_rates odr,
                                           enum bmm350_performance_parameters avg)
{
    dev->status = bmm350_set_odr_performance(odr, avg, &dev->sensor);

    return dev->status;
}

int8_t soldered_bmm350_enable_axes(bmm350_t *dev, bool en_x, bool en_y, bool en_z)
{
    dev->status = bmm350_enable_axes(en_x ? BMM350_X_EN : BMM350_X_DIS, en_y ? BMM350_Y_EN : BMM350_Y_DIS,
                                     en_z ? BMM350_Z_EN : BMM350_Z_DIS, &dev->sensor);

    return dev->status;
}

// *****************************************************************************
// Section: Data

int8_t soldered_bmm350_get_interrupt_status(bmm350_t *dev, uint8_t *drdy_status)
{
    dev->status = bmm350_get_interrupt_status(drdy_status, &dev->sensor);

    return dev->status;
}

int8_t soldered_bmm350_enable_interrupt(bmm350_t *dev, bool enable)
{
    dev->status = bmm350_enable_interrupt(enable ? BMM350_ENABLE_INTERRUPT : BMM350_DISABLE_INTERRUPT, &dev->sensor);

    return dev->status;
}

int8_t soldered_bmm350_configure_interrupt(bmm350_t *dev, enum bmm350_intr_latch latching,
                                           enum bmm350_intr_polarity polarity, enum bmm350_intr_drive drive,
                                           bool map_to_pin)
{
    dev->status = bmm350_configure_interrupt(latching, polarity, drive,
                                             map_to_pin ? BMM350_MAP_TO_PIN : BMM350_UNMAP_FROM_PIN, &dev->sensor);

    return dev->status;
}

int8_t soldered_bmm350_get_sensor_data(bmm350_t *dev)
{
    struct bmm350_mag_temp_data data = {0};

    dev->status = bmm350_get_compensated_mag_xyz_temp_data(&data, &dev->sensor);
    if (dev->status != BMM350_OK) {
        return dev->status;
    }

    dev->data.mag_x = data.x;
    dev->data.mag_y = data.y;
    dev->data.mag_z = data.z;
    dev->data.temperature = data.temperature;

    return BMM350_OK;
}

int8_t soldered_bmm350_perform_self_test(bmm350_t *dev, struct bmm350_self_test *result)
{
    dev->status = bmm350_perform_self_test(result, &dev->sensor);

    return dev->status;
}

// *****************************************************************************
// Section: Status

int8_t soldered_bmm350_check_status(const bmm350_t *dev)
{
    if (dev->status < BMM350_OK) {
        return SOLDERED_BMM350_ERROR;
    }

    return BMM350_OK;
}

const char *soldered_bmm350_status_string(const bmm350_t *dev)
{
    switch (dev->status) {
    case BMM350_OK:
        /* Don't return a text for OK. */
        return "";
    case BMM350_E_NULL_PTR:
        return "Null pointer";
    case BMM350_E_COM_FAIL:
        return "Communication failure";
    case BMM350_E_DEV_NOT_FOUND:
        return "Sensor not found";
    case BMM350_E_INVALID_CONFIG:
        return "Invalid configuration";
    case BMM350_E_BAD_PAD_DRIVE:
        return "Bad pad drive";
    case BMM350_E_RESET_UNFINISHED:
        return "Reset unfinished";
    case BMM350_E_INVALID_INPUT:
        return "Invalid input";
    case BMM350_E_SELF_TEST_INVALID_AXIS:
        return "Invalid self-test axis";
    case BMM350_E_OTP_BOOT:
        return "OTP boot failure";
    case BMM350_E_OTP_PAGE_RD:
        return "OTP page read failure";
    case BMM350_E_OTP_PAGE_PRG:
        return "OTP page program failure";
    case BMM350_E_OTP_SIGN:
        return "OTP sign failure";
    case BMM350_E_OTP_INV_CMD:
        return "Invalid OTP command";
    case BMM350_E_OTP_UNDEFINED:
        return "Undefined OTP error";
    case BMM350_E_ALL_AXIS_DISABLED:
        return "All axes disabled";
    case BMM350_E_PMU_CMD_VALUE:
        return "Invalid PMU command value";
    default:
        return "Undefined error code";
    }
}
