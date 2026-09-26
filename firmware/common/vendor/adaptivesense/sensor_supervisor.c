/*
 * sensor_supervisor.c — see sensor_supervisor.h.
 *
 * Pure C99, no ESP-IDF dependency, host-testable.
 */

#include "sensor_supervisor.h"

#include <string.h>

void sensor_sup_init(sensor_supervisor_t *s, double retry_interval_s,
                     unsigned failures_before_unavailable, double now)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->retry_interval_s = (retry_interval_s > 0.0) ? retry_interval_s : 0.0;
    s->failures_before_unavailable = failures_before_unavailable;
    /* Allow the first attempt immediately: a node that cannot read its sensor
     * has nothing useful to do, so there is no reason to wait. */
    s->next_attempt_t = now;
}

bool sensor_sup_ready(const sensor_supervisor_t *s)
{
    return (s != NULL) && s->initialized;
}

bool sensor_sup_should_attempt(const sensor_supervisor_t *s, double now)
{
    if (s == NULL) {
        return false;
    }
    if (s->initialized) {
        return false; /* up and healthy: nothing to bring up */
    }
    if (!s->attempted) {
        return true; /* first attempt */
    }
    return now >= s->next_attempt_t;
}

void sensor_sup_note_attempt(sensor_supervisor_t *s, double now, bool ok)
{
    if (s == NULL) {
        return;
    }
    s->attempted = true;
    s->init_attempts++;
    if (ok) {
        s->initialized = true;
        /* A fresh init means a fresh device: the runtime failure history is no
         * longer about the thing we are talking to. */
        s->read_failures_consecutive = 0;
        return;
    }
    s->init_failures++;
    s->next_attempt_t = now + s->retry_interval_s;
}

void sensor_sup_note_read_success(sensor_supervisor_t *s)
{
    if (s == NULL) {
        return;
    }
    s->read_successes++;
    s->read_failures_consecutive = 0;
}

bool sensor_sup_note_read_failure(sensor_supervisor_t *s, double now)
{
    if (s == NULL) {
        return false;
    }
    s->read_failures_total++;
    s->read_failures_consecutive++;

    /* Already considered unavailable: there is no transition left to make. */
    if (!s->initialized) {
        return false;
    }

    /* A threshold of 0 disables runtime drop-out entirely. */
    if (s->failures_before_unavailable == 0) {
        return false;
    }
    if (s->read_failures_consecutive < s->failures_before_unavailable) {
        return false; /* tolerated: one bad transfer is not a dead sensor */
    }

    s->initialized = false;
    s->unavailability_events++;
    /* Re-probe as soon as the loop comes round again; from then on the attempt
     * rate is limited by retry_interval_s like any other bring-up. */
    s->next_attempt_t = now;
    return true;
}

const char *sensor_sup_state_name(const sensor_supervisor_t *s)
{
    if (s == NULL) {
        return "unknown";
    }
    if (s->initialized) {
        /* Say so when a live sensor is missing reads: the previous version
         * reported a bare "ready" here, so the log read
         * "no usable reading ... (sensor ready)" — which was simply untrue. */
        return (s->read_failures_consecutive > 0) ? "ready (reads failing)"
                                                  : "ready";
    }
    if (!s->attempted) {
        return "not attempted yet";
    }
    return "unavailable (will retry)";
}
