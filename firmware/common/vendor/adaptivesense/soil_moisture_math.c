/*
 * soil_moisture_math.c — see soil_moisture_math.h.
 *
 * Pure C99, no ESP-IDF dependency, host-testable.
 */
#include <math.h>
#include <string.h>

#include "soil_moisture_math.h"

/* Insertion sort: the batches here are tens of samples at most, so a simple
 * in-place sort is clearer and cheaper than anything cleverer. */
static void sort_ints(int *a, int n)
{
    for (int i = 1; i < n; i++) {
        const int key = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > key) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = key;
    }
}

bool soil_moisture_calib_valid(const soil_moisture_calib_t *calib, int adc_max_raw)
{
    if (calib == NULL || adc_max_raw <= 0) {
        return false;
    }
    if (calib->dry_raw < 0 || calib->dry_raw > adc_max_raw ||
        calib->wet_raw < 0 || calib->wet_raw > adc_max_raw) {
        return false;
    }
    const int span = calib->wet_raw - calib->dry_raw;
    if (span < 0) {
        return -span >= SOIL_MIN_ENDPOINT_SPAN;
    }
    return span >= SOIL_MIN_ENDPOINT_SPAN;
}

int soil_moisture_normalize(const soil_moisture_calib_t *calib, int raw,
                            float *norm, float *unclamped)
{
    if (norm == NULL || unclamped == NULL ||
        !soil_moisture_calib_valid(calib, SOIL_ADC_MAX_RAW)) {
        return -1;
    }
    /*
     * The span is signed, so this one formula covers both module directions:
     * wet < dry and wet > dry. Validate here too, so direct callers cannot turn
     * an out-of-range or near-zero span into a plausible moisture index.
     */
    const int span = calib->wet_raw - calib->dry_raw;

    const float value = (float)(raw - calib->dry_raw) / (float)span;
    *unclamped = value;

    if (value < 0.0f) {
        *norm = 0.0f;
    } else if (value > 1.0f) {
        *norm = 1.0f;
    } else {
        *norm = value;
    }
    return 0;
}

void soil_batch_stats_compute(int *samples, int n, soil_batch_stats_t *out)
{
    if (samples == NULL || out == NULL || n < 1) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->min = samples[0];
    out->max = samples[0];

    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        if (samples[i] < out->min) {
            out->min = samples[i];
        }
        if (samples[i] > out->max) {
            out->max = samples[i];
        }
        sum += (double)samples[i];
    }
    out->mean = sum / (double)n;

    /* Sample standard deviation over n - 1; a single sample has no spread to
     * estimate, so its std is defined as 0 rather than being undefined. */
    if (n > 1) {
        double sq = 0.0;
        for (int i = 0; i < n; i++) {
            const double d = (double)samples[i] - out->mean;
            sq += d * d;
        }
        out->std = sq / (double)(n - 1);
        if (out->std > 0.0) {
            out->std = sqrt(out->std);
        }
    }

    sort_ints(samples, n);
    out->median = (n % 2 == 1)
                      ? samples[n / 2]
                      : (samples[n / 2 - 1] + samples[n / 2]) / 2;

    /* Trimmed mean: drop one value off each end once the batch is big enough for
     * that to mean something. A single-sample trim on n < 8 would discard a
     * meaningful fraction of the data. */
    if (n >= 8) {
        double t = 0.0;
        for (int i = 1; i < n - 1; i++) {
            t += (double)samples[i];
        }
        out->trimmed_mean = t / (double)(n - 2);
    } else {
        out->trimmed_mean = out->mean;
    }
}
