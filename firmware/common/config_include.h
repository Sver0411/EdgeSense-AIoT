/*
 * config_include.h — EdgeSense's configuration values under the names the
 * vendored AdaptiveSense sensor layer expects.
 *
 * Why this file exists
 * --------------------
 * The vendored sensor sources were written against a configuration header named
 * `config_include.h` exposing CONFIG_AS_* macros. Vendored code is never edited,
 * so instead of rewriting those names EdgeSense provides them here by aliasing its
 * own values.
 *
 * The rule this file must never break: it may RENAME a value, it may not INVENT
 * one that EdgeSense has not decided. Everything that comes from
 * config/edge_config.yaml is aliased; the handful of vendor-side parameters that
 * EdgeSense genuinely owns (bus timing, a measurement time) are set here explicitly
 * with the reasoning next to them, and appear nowhere else.
 */
#ifndef EDGE_CONFIG_INCLUDE_H
#define EDGE_CONFIG_INCLUDE_H

#include "edge_config_generated.h"

/* ---- aliases of EdgeSense configuration ---------------------------------- */

#define CONFIG_AS_SENSOR_SDA_GPIO EDGE_I2C_SDA_GPIO
#define CONFIG_AS_SENSOR_SCL_GPIO EDGE_I2C_SCL_GPIO
#define CONFIG_AS_SHT30_I2C_ADDR EDGE_SHT30_I2C_ADDR
#define CONFIG_AS_BH1750_I2C_ADDR EDGE_BH1750_I2C_ADDR
#define CONFIG_AS_SOIL_ADC_GPIO EDGE_SOIL_ADC_GPIO
#define CONFIG_AS_USE_SOIL_SENSOR EDGE_SOIL_ENABLED

/* Backend selection as the vendored header defines it: 1 = BME280, 2 = SHT30.
 * EdgeSense's build has an SHT30, so 2. */
#define CONFIG_AS_SENSOR_BACKEND 2

/* No mocks in a device build: a mock is never reported as a measurement, and the
 * device build should not be able to produce one at all. */
#define CONFIG_AS_USE_MOCK_SENSOR 0

/* The light channel is real hardware (BH1750) and is part of the sensor nodes'
 * expected channel set. */
#define CONFIG_AS_USE_BH1750 1
#define CONFIG_AS_USE_LIGHT 1

/* ---- bus timing and vendor-side parameters EdgeSense owns ---------------- */

/* 100 kHz standard mode. Chosen over 400 kHz on purpose: SHT30 and BH1750 share
 * one bus on jumper wires, and a slower clock is the cheaper side of that trade
 * when the sampling interval is seconds, not milliseconds. */
#define CONFIG_AS_I2C_FREQ_HZ 100000

/* Per-transfer timeout. Must stay well below the sampling interval so a stuck
 * transfer cannot stretch a cycle. */
#define CONFIG_AS_I2C_TIMEOUT_MS 100

/* BH1750 H-resolution conversion time, per the part's datasheet. */
#define CONFIG_AS_BH1750_MEAS_TIME_MS 180

/* Shortest interval the light driver will honour; matches the primary reference
 * cadence so a read is never scheduled faster than the bus is configured for. */
#define CONFIG_AS_MIN_INTERVAL_S 5

/* ---- soil moisture: disabled, and calibrated by nobody ------------------- */
/*
 * EDGE_SOIL_ENABLED is 0 (see config/edge_config.yaml): soil moisture is too
 * spatially local to serve as a shared-event channel, and no probe has been
 * calibrated on EdgeSense hardware.
 *
 * The vendored soil module still needs these constants to compile. They are NOT
 * calibration results and must not be used as any: EDGE_SOIL_REQUIRE_CALIBRATION
 * is 1, which is the flag that forbids publishing a soil value until a real
 * two-point calibration has been recorded (see docs/hardware/wiring.md).
 *
 * CONFIG_AS_SOIL_DRY_RAW records the value actually observed with no medium in
 * the pre-existing project's data: the probe sat at its rail.
 */
#define CONFIG_AS_SOIL_ADC_ATTEN ADC_ATTEN_DB_12
#define CONFIG_AS_SOIL_SAMPLE_COUNT 8
#define CONFIG_AS_SOIL_DRY_RAW 4095
#define CONFIG_AS_SOIL_WET_RAW 1500 /* UNCALIBRATED PLACEHOLDER — see above */

#endif /* EDGE_CONFIG_INCLUDE_H */
