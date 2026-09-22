/**
 * @file main.c
 * @brief Reads compensated magnetometer and temperature data from the
 *        BMM350 sensor over I2C
 *
 * Connect the breakout board to the I2C pins of your board, or use a Qwiic
 * cable.
 *
 * Product used is www.solde.red/333359
 *
 * @author Soldered Electronics
 */

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soldered_bmm350.h"

static const char *TAG = "BMM350_BASIC";

// Change these to match how your breakout is wired
#define PIN_NUM_SDA GPIO_NUM_8
#define PIN_NUM_SCL GPIO_NUM_9

void app_main(void)
{
    bmm350_t sensor;

    // The I2C bus belongs to the application, not to the driver, so that other
    // Qwiic devices can share it. Create it first, then hand it to the driver.
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_NUM_SDA,
        .scl_io_num = PIN_NUM_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    // Start the sensor on its default I2C address (ADSEL tied low, 0x14)
    esp_err_t err = soldered_bmm350_init(&sensor, bus, BMM350_I2C_ADSEL_SET_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BMM350 initialization failed: %s (%s)", esp_err_to_name(err),
                 soldered_bmm350_status_string(&sensor));
        return;
    }

    while (1) {
        // Get a new measurement from the sensor
        if (soldered_bmm350_get_sensor_data(&sensor) == BMM350_OK) {
            ESP_LOGI(TAG, "X: %.2f uT, Y: %.2f uT, Z: %.2f uT, Temperature: %.2f degC", sensor.data.mag_x,
                     sensor.data.mag_y, sensor.data.mag_z, sensor.data.temperature);
        } else {
            ESP_LOGE(TAG, "Failed to read sensor data: %s", soldered_bmm350_status_string(&sensor));
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
