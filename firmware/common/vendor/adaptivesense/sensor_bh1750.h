/*
 * sensor_bh1750.h — the optional light channel: BH1750 / GY-302 on the shared bus.
 *
 * This is deliberately NOT a third entry in CONFIG_AS_SENSOR_BACKEND. That
 * option chooses which chip provides the *primary* environmental reading
 * (SHT30 or BME280); the BH1750 provides one additional channel alongside it and
 * never replaces it. On the physical build the split is
 *
 *     SHT30    temperature, humidity
 *     BH1750   light
 *     (none)   pressure, which therefore stays invalid
 *
 * A read here returns one of three outcomes, and the caller must keep them apart:
 *
 *   BH1750_READ_OK     a lux value was produced
 *   BH1750_READ_SKIP   nothing was attempted (the part is known absent and the
 *                      re-probe backoff has not elapsed)
 *   BH1750_READ_FAIL   an attempt was made and failed
 *
 * Either of the last two leaves the light channel invalid and leaves the rest of
 * the measurement untouched. This layer knows nothing about the change detector,
 * the scheduler, MQTT or events: it turns I2C bytes into lux and nothing else.
 */
#ifndef ADAPTIVESENSE_SENSOR_BH1750_H
#define ADAPTIVESENSE_SENSOR_BH1750_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BH1750_READ_OK    1
#define BH1750_READ_SKIP  0
#define BH1750_READ_FAIL (-1)

/*
 * Arm the optional channel. Does not touch the bus and cannot fail: the device is
 * registered on first use, and an absent part is discovered then rather than
 * assumed here.
 */
void sensor_bh1750_init(void);

/*
 * One light measurement. Writes `*lux` and returns BH1750_READ_OK only when a
 * value was actually produced; otherwise `*lux` is untouched.
 *
 * BH1750_READ_SKIP means no I2C traffic was generated at all, which is the point
 * of the backoff: a missing light sensor must not add latency to every sample.
 */
int sensor_bh1750_read_lux(float *lux);

/* Release the device handle. Safe to call when init never ran. */
void sensor_bh1750_teardown(void);

/* Whether the part is currently believed present, for logs. */
bool sensor_bh1750_available(void);

/* Counters, so an outage can be counted instead of inferred. Any may be NULL. */
void sensor_bh1750_counters(unsigned *reads, unsigned *failures,
                            unsigned *skips, unsigned *unavailability_events);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVESENSE_SENSOR_BH1750_H */
