/*
 * sensor_sht30.c — SHT30 / SHT3x backend.
 *
 * Split in two on purpose:
 *
 *   sht30_proto.c   the protocol: CRC, conversion, command sequencing. Pure C,
 *                   host-tested with a fake transport (tests/test_sensor_sht30.py).
 *   this file       the transport: register a device on the shared bus and hand
 *                   the protocol a real I2C implementation of sht30_io_t.
 *
 * The bus itself is owned by sensor_bus.c and shared with every other device on
 * those two wires, so this backend never creates or deletes one.
 *
 * Channels: temperature and humidity only. The SHT30 itself has neither a
 * pressure channel nor a light channel, so both are left invalid here; if the
 * optional BH1750 is fitted, `sensor.c` merges its reading into the light channel
 * after this backend returns (see sensor_bh1750.c). `sensor.c` has already marked
 * every channel invalid before calling read(), so the backend only has to fill in
 * what it actually measured.
 */
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config_include.h"
#include "sensor_backend.h"
#include "sensor_bus.h"
#include "sht30_proto.h"

static const char *TAG = "sht30";

static i2c_master_dev_handle_t s_dev = NULL;

/* ------------------------------------------------------------------ */
/* real transport                                                      */
/* ------------------------------------------------------------------ */
static int io_write(void *ctx, const uint8_t *cmd, size_t len)
{
    const i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)ctx;
    const esp_err_t err = i2c_master_transmit(dev, cmd, len, CONFIG_AS_I2C_TIMEOUT_MS);
    return (err == ESP_OK) ? 0 : -1;
}

static int io_read(void *ctx, uint8_t *out, size_t len)
{
    const i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)ctx;
    const esp_err_t err = i2c_master_receive(dev, out, len, CONFIG_AS_I2C_TIMEOUT_MS);
    return (err == ESP_OK) ? 0 : -1;
}

static void io_delay(void *ctx, unsigned ms)
{
    (void)ctx;
    /* Between the measurement command and the read. The protocol layer asks for
     * SHT30_MEASURE_WAIT_MS; this is where the time is actually spent. */
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static const sht30_io_t SHT30_IO = {
    .write = io_write,
    .read = io_read,
    .delay_ms = io_delay,
    .ctx = NULL, /* filled in per call with the device handle */
};

/* ------------------------------------------------------------------ */
/* backend interface                                                   */
/* ------------------------------------------------------------------ */
static void sht30_teardown(void)
{
    if (s_dev != NULL) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}

static int sht30_init(void)
{
    i2c_master_bus_handle_t bus = sensor_bus_acquire();
    if (bus == NULL) {
        return -1;
    }

    /* A retry must not inherit the previous attempt's device handle. */
    sht30_teardown();

    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_AS_SHT30_I2C_ADDR,
        .scl_speed_hz = CONFIG_AS_I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev_config, &s_dev) != ESP_OK) {
        s_dev = NULL;
        ESP_LOGE(TAG, "i2c device add failed for 0x%02x",
                 (unsigned)CONFIG_AS_SHT30_I2C_ADDR);
        return -1;
    }

    const esp_err_t probe = i2c_master_probe(bus, CONFIG_AS_SHT30_I2C_ADDR,
                                             CONFIG_AS_I2C_TIMEOUT_MS);
    if (probe != ESP_OK) {
        ESP_LOGE(TAG, "no SHT30 at 0x%02x: %s", (unsigned)CONFIG_AS_SHT30_I2C_ADDR,
                 esp_err_to_name(probe));
        sht30_teardown();
        return -1;
    }

    /* Address alone is not identity: confirm the SHT3x command set answers with a
     * valid status checksum before trusting this device with measurements. */
    sht30_io_t io = SHT30_IO;
    io.ctx = s_dev;
    if (sht30_proto_identify(&io) != 0) {
        ESP_LOGE(TAG, "device at 0x%02x did not answer the SHT3x status command "
                      "with a valid checksum; refusing to use it",
                 (unsigned)CONFIG_AS_SHT30_I2C_ADDR);
        sht30_teardown();
        return -1;
    }

    ESP_LOGI(TAG, "SHT30 ready at 0x%02x (single shot, high repeatability, "
                  "no clock stretching; channels: temperature, humidity)",
             (unsigned)CONFIG_AS_SHT30_I2C_ADDR);
    return 0;
}

static int sht30_read(sensor_read_t *out)
{
    if (out == NULL || s_dev == NULL) {
        return -1;
    }

    sht30_io_t io = SHT30_IO;
    io.ctx = s_dev;

    float temperature_c = 0.0f;
    float humidity_pct = 0.0f;
    if (sht30_proto_measure(&io, &temperature_c, &humidity_pct) != 0) {
        /* Either the transfer failed or a CRC did not match. Both mean there is no
         * trustworthy measurement, so nothing is written: the caller sees -1 and
         * every channel stays invalid. A wrong-by-a-bit reading is worse than no
         * reading, because the change detector cannot tell the difference. */
        return -1;
    }

    out->value[SEN_CH_TEMPERATURE] = temperature_c;
    out->valid[SEN_CH_TEMPERATURE] = true;
    out->value[SEN_CH_HUMIDITY] = humidity_pct;
    out->valid[SEN_CH_HUMIDITY] = true;

    /* This part measures only temperature and humidity; pressure has no device
     * behind it, and light, when a BH1750 is fitted, is merged by sensor.c. */
    out->valid[SEN_CH_PRESSURE] = false;
    out->valid[SEN_CH_LIGHT] = false;
    return 0;
}

const sensor_backend_t sensor_backend_sht30 = {
    .name = "SHT30 (temperature + humidity)",
    .init = sht30_init,
    .read = sht30_read,
    .teardown = sht30_teardown,
};
