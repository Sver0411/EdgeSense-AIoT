/*
 * sensor_bus.c — see sensor_bus.h.
 */

#include "sensor_bus.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config_include.h"

static const char *TAG = "sensor_bus";

static i2c_master_bus_handle_t s_bus = NULL;

/*
 * Settling time after releasing an open-drain line before trusting its level.
 * 45 kOhm against a short bus is a few microseconds of rise time; 2 ms is
 * comfortably clear of it.
 */
#define BUS_SETTLE_MS 2

/* Lines as inputs, with the internal pull-up: the only mode in which a level can
 * actually be read. See the note below. */
static void lines_as_inputs(int sda, int scl)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

/* Lines as open-drain outputs: released when written 1, driven low when written 0. */
static void lines_as_open_drain(int sda, int scl)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

/*
 * Free a bus whose lines are being held low by a slave.
 *
 * A slave that is mid-transaction when the master resets — or that has been asked
 * for clock stretching the master will not perform — can keep holding SDA or SCL.
 * Nothing on the bus works after that, and a plain probe just times out. This was
 * observed on hardware: SCL read low, all three devices (SHT30, BH1750, display)
 * timed out, and they all answered again after the sequence below.
 *
 * Doing this at bring-up matters more than it looks: without it, a node that
 * happened to reset mid-transfer would stay dead across every retry, because the
 * bus itself is what is broken.
 *
 * MEASURING vs DRIVING — the subtlety that made the first version of this
 * function wrong. `gpio_get_level()` reads the input register, and configuring a
 * pin as GPIO_MODE_OUTPUT_OD leaves its input buffer disabled, so the read
 * returns a constant 0 whether the line is released high or driven low. The first
 * version therefore diagnosed every healthy bus as wedged, at every boot. Measured
 * on the board:
 *
 *     OUTPUT_OD, released (high)   -> reads 0
 *     OUTPUT_OD, driven low        -> reads 0     (indistinguishable)
 *     INPUT with pull-up           -> reads 1     (the same line, sampled properly)
 *
 * So levels are only ever sampled with the pins configured as inputs, and the
 * pins are switched to open-drain only for the clocking.
 */
static void bus_recover_lines(void)
{
    const int sda = CONFIG_AS_SENSOR_SDA_GPIO;
    const int scl = CONFIG_AS_SENSOR_SCL_GPIO;

    /* --- measure the idle state, as inputs ---------------------------------- */
    lines_as_inputs(sda, scl);
    vTaskDelay(pdMS_TO_TICKS(BUS_SETTLE_MS));

    const int sda_idle = gpio_get_level((gpio_num_t)sda);
    const int scl_idle = gpio_get_level((gpio_num_t)scl);
    if (sda_idle != 0 && scl_idle != 0) {
        gpio_reset_pin((gpio_num_t)sda);
        gpio_reset_pin((gpio_num_t)scl);
        return; /* bus is healthy; leave it alone and say nothing */
    }

    ESP_LOGW(TAG, "i2c lines not idling high (SDA=%d SCL=%d); recovering",
             sda_idle, scl_idle);

    /* --- clock SCL by hand, as open-drain ----------------------------------- */
    lines_as_open_drain(sda, scl);
    gpio_set_level((gpio_num_t)sda, 1); /* release */
    gpio_set_level((gpio_num_t)scl, 1);
    esp_rom_delay_us(5);

    for (int i = 0; i < 16; i++) {
        gpio_set_level((gpio_num_t)scl, 0);
        esp_rom_delay_us(5);
        gpio_set_level((gpio_num_t)scl, 1);
        esp_rom_delay_us(5);
    }

    /* STOP condition: SDA rises while SCL is high. */
    gpio_set_level((gpio_num_t)sda, 0);
    esp_rom_delay_us(5);
    gpio_set_level((gpio_num_t)scl, 1);
    esp_rom_delay_us(5);
    gpio_set_level((gpio_num_t)sda, 1);

    /* --- verify, as inputs again -------------------------------------------- */
    lines_as_inputs(sda, scl);
    vTaskDelay(pdMS_TO_TICKS(BUS_SETTLE_MS));
    const int sda_after = gpio_get_level((gpio_num_t)sda);
    const int scl_after = gpio_get_level((gpio_num_t)scl);
    ESP_LOGW(TAG, "bus recovery done: SDA=%d SCL=%d (%s)", sda_after, scl_after,
             (sda_after != 0 && scl_after != 0) ? "lines released"
                                                : "still not idle high");

    /* Hand the pins back so the I2C driver can claim them. */
    gpio_reset_pin((gpio_num_t)sda);
    gpio_reset_pin((gpio_num_t)scl);
}

i2c_master_bus_handle_t sensor_bus_acquire(void)
{
    if (s_bus != NULL) {
        return s_bus;
    }

    /* Before the driver claims the pins: a wedged bus must be released, or every
     * transfer below fails and no amount of retrying helps.
     *
     * Known boundary, recorded rather than claimed away: this recovery runs only
     * here, i.e. when the bus does not exist yet — at boot, or after a failed
     * creation. A slave that wedges the bus *while the node is running* is not
     * covered: the shared bus is created once and kept, so neither a primary
     * sensor re-init nor a BH1750 retry reaches this code. Untested on purpose
     * for this round; clocking the lines mid-run while other devices share them
     * needs its own justification and test. See docs/hardware.md, "Runtime bus
     * wedge". */
    bus_recover_lines();

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_AS_SENSOR_SDA_GPIO,
        .scl_io_num = CONFIG_AS_SENSOR_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    const esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed: %s", esp_err_to_name(err));
        s_bus = NULL;
        return NULL;
    }

    ESP_LOGI(TAG, "shared i2c bus ready: SDA=GPIO%d SCL=GPIO%d (one bus for every device on it)",
             (int)CONFIG_AS_SENSOR_SDA_GPIO, (int)CONFIG_AS_SENSOR_SCL_GPIO);
    return s_bus;
}
