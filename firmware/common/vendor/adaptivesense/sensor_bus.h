/*
 * sensor_bus.h — the one I2C bus this node owns.
 *
 * The SHT30, the BH1750 and the OLED are all on the same two wires, so the bus
 * must be created exactly once and shared. A sensor backend therefore never calls
 * `i2c_new_master_bus()`: it asks for the bus here and only registers its own
 * *device* handle on it. Creating a second bus on the same port fails with
 * `I2C bus id(0) has already been acquired` — which is what a bring-up retry used
 * to hit, because the previous attempt had never released it.
 *
 * The handle is created lazily on first use and deliberately never deleted: it
 * outlives any single backend's bring-up attempt, so a retry cannot lose the bus
 * and a future driver for the light channel or the display can simply share it.
 *
 * Only translated in non-mock builds. See sensor.c.
 */
#ifndef ADAPTIVESENSE_SENSOR_BUS_H
#define ADAPTIVESENSE_SENSOR_BUS_H

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return the shared bus, creating it on first call. Returns NULL if it could not
 * be created (and will try again on the next call, so a transient failure is
 * recoverable).
 */
i2c_master_bus_handle_t sensor_bus_acquire(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVESENSE_SENSOR_BUS_H */
