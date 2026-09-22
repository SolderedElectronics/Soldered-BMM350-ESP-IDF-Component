/**
 * @file main.c
 * @brief Runs the BMM350's built-in self-test
 *
 * The self-test generates an internal ~130 uT magnetic field on the X and Y
 * channels and reports the resulting field difference; per the datasheet
 * (section 5.1.6), a channel is considered working if its reported value is
 * >= 130 uT.
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

static const char *TAG = "BMM350_SELF_TEST";

// Change these to match how your breakout is wired
#define PIN_NUM_SDA GPIO_NUM_8
#define PIN_NUM_SCL GPIO_NUM_9

// Minimum self-test field difference to consider a channel passing,
// per the datasheet's self-test section
#define SELF_TEST_THRESHOLD_UT 130.0f

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

    // Run the self-test. The sensor automatically returns to normal mode
    // afterwards since soldered_bmm350_init() already left it there.
    struct bmm350_self_test result;
    if (soldered_bmm350_perform_self_test(&sensor, &result) == BMM350_OK) {
        ESP_LOGI(TAG, "X axis field: %.2f uT -> %s", result.out_ust_x,
                 result.out_ust_x >= SELF_TEST_THRESHOLD_UT ? "PASS" : "FAIL");
        ESP_LOGI(TAG, "Y axis field: %.2f uT -> %s", result.out_ust_y,
                 result.out_ust_y >= SELF_TEST_THRESHOLD_UT ? "PASS" : "FAIL");
    } else {
        ESP_LOGE(TAG, "Self-test failed to run: %s", soldered_bmm350_status_string(&sensor));
    }
}
