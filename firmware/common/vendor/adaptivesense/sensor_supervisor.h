/*
 * sensor_supervisor.h — sensor bring-up and runtime health policy.
 *
 * Decides *when* the application may talk to the sensor and *when* it should try
 * to initialise it again. Keeping this decision in a pure-C unit (no ESP-IDF
 * dependency) means the policy is unit-tested on the host rather than being
 * buried in the main loop.
 *
 * Two failure modes are covered, and they are different:
 *
 *   bring-up failure   the sensor was never usable — wrong wiring, wrong
 *                      address, absent part. Retried at a limited rate.
 *   runtime failure    the sensor WAS usable and then stopped answering, for
 *                      example because it was unplugged while the node ran.
 *
 * The second one is why `failures_before_unavailable` exists. Observed on
 * hardware: with the first version, a node whose sensor was unplugged kept
 * logging "no usable reading ... (sensor ready)" — the state said ready while
 * every read failed — and `sensor_init()` was never called again, because nothing
 * ever cleared the `initialized` flag. Recovery happened only because the driver's
 * device handle was still registered and the part reappeared; there was no
 * re-probe path and no accounting of the outage.
 *
 * The policy:
 *
 *   read fails → counted. Once `failures_before_unavailable` consecutive reads
 *                have failed, the sensor is declared unavailable, and bring-up is
 *                retried under the same rate limit as any other attempt.
 *   read works → the consecutive-failure count resets.
 *
 * A threshold rather than a single failure on purpose: one bad CRC or one timed
 * out transfer should not tear down a working driver.
 */
#ifndef ADAPTIVESENSE_SENSOR_SUPERVISOR_H
#define ADAPTIVESENSE_SENSOR_SUPERVISOR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     initialized;       /* the sensor is usable right now             */
    bool     attempted;         /* at least one init attempt has been noted   */
    double   retry_interval_s;  /* minimum spacing between init attempts      */
    double   next_attempt_t;    /* earliest time for the next init attempt    */
    unsigned failures_before_unavailable; /* consecutive read failures allowed */

    unsigned init_attempts;     /* total init attempts                        */
    unsigned init_failures;     /* attempts that did not succeed              */
    unsigned read_successes;    /* successful measurements                    */
    unsigned read_failures_consecutive;
    unsigned read_failures_total;
    unsigned unavailability_events; /* times a live sensor dropped out        */
} sensor_supervisor_t;

/*
 * Arm the supervisor. The first init attempt is allowed at `now`.
 *
 * `failures_before_unavailable` is how many consecutive read failures it takes to
 * conclude that a previously working sensor has gone away. Zero means "never drop
 * out at runtime", which reproduces the old behaviour and exists so a test can
 * show the difference.
 */
void sensor_sup_init(sensor_supervisor_t *s, double retry_interval_s,
                     unsigned failures_before_unavailable, double now);

/* May the application read the sensor? Only while it is known to be usable. */
bool sensor_sup_ready(const sensor_supervisor_t *s);

/* Should the application attempt `sensor_init()` now? */
bool sensor_sup_should_attempt(const sensor_supervisor_t *s, double now);

/*
 * Record the outcome of an init attempt made at time `now`.
 * On failure the next attempt is scheduled `retry_interval_s` later. On success
 * the runtime failure counters are cleared, because a fresh init means a fresh
 * device.
 */
void sensor_sup_note_attempt(sensor_supervisor_t *s, double now, bool ok);

/* Record a successful measurement: resets the consecutive-failure count. */
void sensor_sup_note_read_success(sensor_supervisor_t *s);

/*
 * Record a failed measurement. Returns true if this failure was the one that
 * declared the sensor unavailable — the caller should log that transition, since
 * it is the moment the node's behaviour changes. When it returns true, bring-up
 * is allowed again immediately (and rate-limited as usual from then on).
 */
bool sensor_sup_note_read_failure(sensor_supervisor_t *s, double now);

/* Human-readable state, for the log. */
const char *sensor_sup_state_name(const sensor_supervisor_t *s);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVESENSE_SENSOR_SUPERVISOR_H */
