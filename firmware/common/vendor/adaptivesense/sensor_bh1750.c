/*
 * sensor_bh1750.c — BH1750 / GY-302 on the shared I2C bus.
 *
 * Transport only: register a device on the bus that sensor_bus.c owns and hand
 * bh1750_proto.c a real I2C implementation. No bus is created here — the SHT30,
 * this part and the display all sit on the same two wires, and creating a second
 * bus on the same port fails outright.
 *
 * The availability policy lives in bh1750_proto.c (pure C, host-tested); this file
 * only supplies the clock it needs and the I2C it runs on.
 */
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config_include.h"
#include "bh1750_proto.h"
#include "sensor_bh1750.h"
#include "sensor_bus.h"

static const char *TAG = "bh1750";

static i2c_master_dev_handle_t s_dev = NULL;
static bh1750_state_t s_state;
static bool s_armed = false;

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
    /* The protocol expects an exact byte count; a short frame must not look like
     * success. */
    return (err == ESP_OK) ? (int)len : -1;
}

static void io_delay(void *ctx, unsigned ms)
{
    (void)ctx;
    /* The conversion wait. Bounded by BH1750_MEAS_WAIT_MS; the caller never
     * polls, so this is the only place the time is spent. */
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static const bh1750_io_t BH1750_IO = {
    .write = io_write,
    .read = io_read,
    .delay_ms = io_delay,
    .ctx = NULL,
};

static unsigned long now_ms(void)
{
    return (unsigned long)(esp_timer_get_time() / 1000LL);
}

/* ------------------------------------------------------------------ */
/* device registration                                                 */
/* ------------------------------------------------------------------ */
static void bh1750_teardown(void)
{
    if (s_dev != NULL) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}

static bool ensure_device(void)
{
    if (s_dev != NULL) {
        return true;
    }

    i2c_master_bus_handle_t bus = sensor_bus_acquire();
    if (bus == NULL) {
        return false;
    }

    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_AS_BH1750_I2C_ADDR,
        .scl_speed_hz = CONFIG_AS_I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev_config, &s_dev) != ESP_OK) {
        s_dev = NULL;
        ESP_LOGE(TAG, "i2c device add failed for 0x%02x",
                 (unsigned)CONFIG_AS_BH1750_I2C_ADDR);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */
void sensor_bh1750_init(void)
{
    bh1750_state_init(&s_state,
                      (unsigned long)CONFIG_AS_MIN_INTERVAL_S * 1000UL,
                      now_ms());
    s_armed = true;
    /*
     * The backoff is a *minimum*, not a rate: the policy is only consulted when
     * the main loop takes a sample, so with the node sampling every 60 s an
     * absent light sensor is re-probed on the next sample after the backoff
     * elapses, not on a 5 s timer. Following the normal sampling cadence is the
     * point — a re-probe is an attempt at a measurement, and there is nothing to
     * gain from a dedicated task that would keep the chip awake to run it.
     */
    ESP_LOGI(TAG, "optional light channel enabled: BH1750 at 0x%02x "
                  "(one-shot H-resolution, %u ms conversion; re-probe backoff "
                  ">= %us, checked on sensor samples)",
             (unsigned)CONFIG_AS_BH1750_I2C_ADDR,
             (unsigned)CONFIG_AS_BH1750_MEAS_TIME_MS,
             (unsigned)CONFIG_AS_MIN_INTERVAL_S);
}

int sensor_bh1750_read_lux(float *lux)
{
    if (!s_armed || lux == NULL) {
        return BH1750_READ_FAIL;
    }

    const unsigned long t = now_ms();

    /* Absent and still backing off: no bus traffic at all this sample. */
    if (!bh1750_should_attempt(&s_state, t)) {
        bh1750_note_skip(&s_state);
        return BH1750_READ_SKIP;
    }

    if (!ensure_device()) {
        bh1750_note_result(&s_state, false, t);
        return BH1750_READ_FAIL;
    }

    bh1750_io_t io = BH1750_IO;
    io.ctx = s_dev;

    float value = 0.0f;
    const int rc = bh1750_proto_measure(&io, CONFIG_AS_BH1750_MEAS_TIME_MS, &value);
    bh1750_note_result(&s_state, rc == 0, t);

    if (rc != 0) {
        /* Keep the device handle: the part may come back, and re-registering it
         * on every sample would be churn. Only a re-probe can clear this. */
        return BH1750_READ_FAIL;
    }

    *lux = value;
    return BH1750_READ_OK;
}

void sensor_bh1750_teardown(void)
{
    bh1750_teardown();
}

bool sensor_bh1750_available(void)
{
    return s_armed && s_state.available;
}

void sensor_bh1750_counters(unsigned *reads, unsigned *failures,
                            unsigned *skips, unsigned *unavailability_events)
{
    if (reads != NULL) {
        *reads = s_state.reads;
    }
    if (failures != NULL) {
        *failures = s_state.failures;
    }
    if (skips != NULL) {
        *skips = s_state.skips;
    }
    if (unavailability_events != NULL) {
        *unavailability_events = s_state.unavailability_events;
    }
}
