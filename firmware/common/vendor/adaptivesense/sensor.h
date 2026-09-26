/*
 * sensor.h — Sensor Layer.
 *
 * Abstracts the physical transducer(s) behind a small interface so the rest of
 * the system never depends on the specific chip.
 *
 * Two independent things are configured here, and it matters that they are not
 * the same thing:
 *
 *   the primary environmental backend   CONFIG_AS_SENSOR_BACKEND, one of SHT30
 *                                       (temperature + humidity) or BME280
 *                                       (temperature + humidity + pressure)
 *
 *   the optional light channel          CONFIG_AS_USE_BH1750, a BH1750 / GY-302
 *                                       that provides `light` alongside whatever
 *                                       the primary backend produced
 *
 * The light sensor is not a third backend: it never replaces the primary one, it
 * adds a channel to it. On the physical build the SHT30 supplies temperature and
 * humidity, the BH1750 supplies light, and pressure stays invalid because nothing
 * on the board measures it.
 *
 * When CONFIG_AS_USE_MOCK_SENSOR is enabled a deterministic mock is used instead,
 * which lets the firmware logic be exercised without hardware. A mock is clearly
 * labeled: it is never reported as a real measurement.
 */
#ifndef ADAPTIVESENSE_SENSOR_H
#define ADAPTIVESENSE_SENSOR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SEN_CH_TEMPERATURE = 0,
    SEN_CH_HUMIDITY,
    SEN_CH_PRESSURE,
    SEN_CH_LIGHT,
    SEN_CH_COUNT
} sen_channel_t;

typedef struct {
    float value[SEN_CH_COUNT];   /* temperature C, humidity %RH, pressure hPa,
                                      light lux (BH1750) */
    bool  valid[SEN_CH_COUNT];
} sensor_read_t;

/*
 * Initialise the sensor subsystem. Must succeed before sensor_read() will produce
 * a reading.
 *
 * Which chip this drives is a configuration choice
 * (CONFIG_AS_SENSOR_BACKEND): a BME280 or an SHT30/SHT3x, or the mock
 * when CONFIG_AS_USE_MOCK_SENSOR is set. The caller does not need to know which:
 * a backend reports the channels it cannot measure by leaving them invalid.
 */
int sensor_init(void);

/* True once sensor_init() has succeeded. */
bool sensor_is_initialized(void);

/*
 * Take a single measurement. Returns 0 on success, -1 on failure.
 *
 * On success, read `valid[]` to see which channels carry a real value: the
 * primary backend fills in only what its chip can measure (for example an SHT30
 * has no pressure channel), and the optional light channel fills `light` only if
 * the BH1750 answered. A channel with `valid == false` must be ignored by the
 * caller — its `value` is zero and carries no meaning.
 *
 * A failing light sensor does **not** fail the measurement: temperature and
 * humidity are still reported and only `light` is marked invalid. A failing
 * primary backend does fail it. See `apply_optional_light()` in sensor.c.
 *
 * On failure the return value is -1 and **no** channel is marked valid, so a
 * caller that ignores the return value cannot mistake the buffer for a
 * measurement. Before a successful init the buffer is not written to at all.
 */
int sensor_read(sensor_read_t *out);

/* Human-readable name of the active backend, for logs and the boot banner. */
const char *sensor_backend_name(void);

#if CONFIG_AS_USE_MOCK_SENSOR
/*
 * Mock-only test seams.
 *
 * Declared inside CONFIG_AS_USE_MOCK_SENSOR so they cannot be compiled into a
 * normal device build. They exist because the contract's failure path — a failed
 * re-init must leave the subsystem consistently unusable — is otherwise only
 * reachable with hardware that can be unplugged mid-run.
 */
void sensor_mock_set_init_failure(bool should_fail);

/* What the mock backend has seen. `backend_live` mirrors the device build's
 * `s_active != NULL`, so a test can assert that a failed re-init left no handle
 * behind for a subsequent read to use. */
typedef struct {
    unsigned init_calls;
    unsigned init_failures;
    unsigned read_calls;   /* reads that got past the contract */
    bool     backend_live;
} sensor_mock_stats_t;

void sensor_mock_stats(sensor_mock_stats_t *out);

/* Make the next sensor_read() fail at the primary backend. This is the case that
 * must take the whole measurement down, unlike a failure of the optional light
 * channel. */
void sensor_mock_set_read_failure(bool should_fail);

/*
 * Control the optional light channel: 0 = present and measuring,
 * 1 = attempted and failed, 2 = absent (no attempt made). All three leave the
 * primary channels alone, which is what the merge tests assert.
 */
void sensor_mock_set_bh1750(int mode);
#endif

/* Human-readable name of a channel, for logging/MQTT build-up. */
const char *sensor_channel_name(sen_channel_t c);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVESENSE_SENSOR_H */