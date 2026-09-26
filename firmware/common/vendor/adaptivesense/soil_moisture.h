/*
 * soil_moisture.h — the analog soil-moisture channel: ADC acquisition only.
 *
 * This driver knows nothing about the change detector, the scheduler, MQTT, the
 * Wi-Fi, the I2C sensors or any event logic. It turns GPIO samples into three
 * numbers — raw counts, calibrated millivolts, and a normalised index — and
 * reports the batch evidence alongside them, because the index is only as good
 * as its calibration and the calibration is only as good as the evidence behind
 * it.
 *
 * What the normalised value is, and is not
 * ----------------------------------------
 * `normalized` is a *relative moisture index* between the two configured
 * endpoints: 0.0 at the calibrated dry endpoint, 1.0 at the calibrated wet
 * endpoint. It is not volumetric water content and not an absolute
 * soil-moisture percentage; a two-point calibration cannot account for soil
 * type, probe geometry, temperature or salinity. See soil_moisture_math.h.
 *
 * Until both endpoints are configured with a usable span, `normalized_valid`
 * stays false and only raw / millivolts are reported: an uncalibrated ADC is
 * still a real measurement, but a percentage built on top of it would be a
 * fiction.
 */
#ifndef ADAPTIVESENSE_SOIL_MOISTURE_H
#define ADAPTIVESENSE_SOIL_MOISTURE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   raw;               /* filtered raw counts (see the filter note below) */
    int   millivolts;        /* calibrated voltage of that raw value */
    float normalized;        /* clamped 0.0 - 1.0 relative index */
    float unclamped;         /* the index before clamping, for out-of-span visibility */
    bool  voltage_valid;     /* the calibration scheme accepted the raw value */
    bool  normalized_valid;  /* both endpoints configured and usable */
    bool  saturated;         /* filtered raw value reached an ADC endpoint */

    /* Batch evidence from the samples behind `raw`. A large spread here is the
     * honest reason to change the sample count or the filter, not a guess. */
    int   samples_taken;     /* how many ADC reads succeeded (out of the N asked) */
    int   min;
    int   max;
    double mean;
    double std;
} soil_moisture_reading_t;

/*
 * Configure the ADC (unit and channel derived from CONFIG_AS_SOIL_ADC_GPIO,
 * never hardcoded), create the calibration handle, and log what was found.
 *
 * Returns 0 when the ADC is usable. A non-zero return means soil telemetry is
 * unavailable for this boot (bad GPIO or ADC init failure); the node keeps
 * running without it. Missing voltage calibration is not fatal: raw counts
 * remain available, and voltage_valid is false.
 */
int soil_moisture_init(void);

/*
 * Take one soil measurement: CONFIG_AS_SOIL_SAMPLE_COUNT raw ADC reads, reduced
 * to a filtered raw value, converted to calibrated millivolts and normalised
 * against the configured endpoints.
 *
 * Returns 0 when a measurement was taken (even when calibration is unavailable
 * and `normalized_valid` is false), -1 when no usable sample could be read. On
 * -1 the outputs are invalid; a failed read is never reported as 0 %.
 *
 * Latency: N oneshot reads back to back, no delays between them — tens of
 * microseconds each, so the whole call is well under a millisecond. A soil
 * reading must not cost the node its sleep budget.
 */
int soil_moisture_read(soil_moisture_reading_t *out);

/* Whether both calibration endpoints are configured and usable. */
bool soil_moisture_calibrated(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVESENSE_SOIL_MOISTURE_H */
