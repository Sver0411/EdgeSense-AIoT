/*
 * soil_moisture_math.h — soil ADC maths: batch statistics and normalisation.
 *
 * Pure C99, no ESP-IDF dependency, host-testable (tests/test_soil_moisture_math.py).
 *
 * What this unit deliberately does NOT claim
 * ------------------------------------------
 * The normalised value it produces is a **relative moisture index** between the
 * two configured endpoints: 0.0 is "at the dry endpoint as calibrated", 1.0 is
 * "at the wet endpoint as calibrated". It is not volumetric water content and not
 * an absolute soil-moisture percentage. A resistive/capacitive soil module's
 * output depends on soil type, probe geometry, contact, temperature and salinity;
 * none of those are calibrated for here, and a two-point calibration cannot make
 * them so.
 *
 * Direction
 * ---------
 * Different modules move in different directions as soil wets up (some output
 * fall, some rise). The normalisation below needs no direction flag, because
 *
 *     norm = (raw - dry) / (wet - dry)
 *
 * already handles both: with dry=3000 / wet=1000 it maps 3000 -> 0.0 and
 * 1000 -> 1.0, and with dry=1000 / wet=3000 it maps 1000 -> 0.0 and 3000 -> 1.0.
 * The caller supplies the endpoints in the units they were measured in; the sign
 * of the span does the rest.
 */
#ifndef ADAPTIVESENSE_SOIL_MOISTURE_MATH_H
#define ADAPTIVESENSE_SOIL_MOISTURE_MATH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The two calibration endpoints, in raw ADC counts. Both must lie inside the
 * ADC's input range and differ by at least SOIL_MIN_ENDPOINT_SPAN; anything
 * closer than that would amplify noise into the index.
 */
typedef struct {
    int dry_raw;
    int wet_raw;
} soil_moisture_calib_t;

/* The endpoints must differ by at least this many counts for the index to be
 * worth computing: a narrower span means every count of noise is a meaningful
 * fraction of the whole scale. */
#define SOIL_MIN_ENDPOINT_SPAN 50
#define SOIL_ADC_MAX_RAW 4095

/*
 * Is this calibration usable?
 *
 * `adc_max_raw` is the largest raw value the ADC can produce (4095 at 12 bit).
 * Fails when either endpoint is outside [0, adc_max_raw], or when the endpoints
 * coincide or are too close (SOIL_MIN_ENDPOINT_SPAN) — a division by (nearly)
 * zero is exactly the bug this check exists to prevent.
 */
bool soil_moisture_calib_valid(const soil_moisture_calib_t *calib, int adc_max_raw);

/*
 * Normalise one raw reading against the endpoints.
 *
 * Writes `*norm` (clamped to [0, 1]) and `*unclamped` (the same value before
 * clamping, so a reading outside the calibrated span stays observable instead of
 * silently piling up at an endpoint). Returns 0 on success, -1 when the
 * calibration is unusable for the 12-bit ADC, in which case neither output is
 * written.
 */
int soil_moisture_normalize(const soil_moisture_calib_t *calib, int raw,
                            float *norm, float *unclamped);

/* ------------------------------------------------------------------ */
/* batch statistics                                                    */
/* ------------------------------------------------------------------ */

/*
 * Statistics over one batch of raw ADC samples. All of them are reported — not
 * just the chosen filter — because the choice of filter (mean vs median vs
 * trimmed mean) is an empirical decision that should be made from data, and
 * because the spread of the individual samples is what tells you how large a
 * batch needs to be in the first place.
 */
typedef struct {
    int    min;
    int    max;
    double mean;
    double std;           /* sample standard deviation (n - 1); 0 when n == 1 */
    int    median;
    double trimmed_mean;  /* extremes trimmed: one value off each end for
                             n >= 8, none below that */
} soil_batch_stats_t;

/*
 * Compute the statistics of `n` samples. `samples` may be reordered by this
 * call; `n` must be >= 1. `out` may not be NULL.
 */
void soil_batch_stats_compute(int *samples, int n, soil_batch_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVESENSE_SOIL_MOISTURE_MATH_H */
