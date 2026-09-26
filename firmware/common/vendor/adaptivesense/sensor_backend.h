/*
 * sensor_backend.h — the interface between the sensor layer and a concrete chip.
 *
 * AdaptiveSense is sensor-agnostic: `sensor.c` owns the public API
 * (`sensor_init` / `sensor_read`), the read contract and the shared I2C bus, and
 * delegates the chip-specific work to a backend that implements this interface.
 *
 * Two backends ship today:
 *
 *   sensor_backend_bme280   BME280, register-level, includes pressure
 *   sensor_backend_sht30    SHT30 / SHT3x, command-based, temperature + humidity
 *
 * Adding a third means adding one file plus an entry in the selection switch in
 * `sensor.c`; the change detector, the scheduler and the event logic never see
 * any of it. A backend reports what it cannot measure by leaving the channel
 * invalid in `sensor_read_t.valid`, which is how the detector already excludes
 * channels it must not score.
 *
 * Nothing in the sensor layer may change the algorithm: `sensor_read_t` carries
 * physical values in the same units and with the same channel order regardless of
 * which backend produced them.
 */
#ifndef ADAPTIVESENSE_SENSOR_BACKEND_H
#define ADAPTIVESENSE_SENSOR_BACKEND_H

#include "sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sensor_backend {
    /* Human-readable name, logged at bring-up. */
    const char *name;

    /*
     * Probe the chip, configure it and leave it ready for a measurement.
     * Returns 0 on success. Called on the shared bus that `sensor.c` owns; a
     * backend must add its own device handle, never create a bus of its own.
     */
    int (*init)(void);

    /* Take one measurement. Returns 0 on success, -1 on any failure. */
    int (*read)(sensor_read_t *out);

    /*
     * Release the backend's device handle. Must be safe to call when `init()`
     * never ran or failed, and must not delete the shared bus.
     */
    void (*teardown)(void);
} sensor_backend_t;

extern const sensor_backend_t sensor_backend_bme280;
extern const sensor_backend_t sensor_backend_sht30;

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVESENSE_SENSOR_BACKEND_H */
