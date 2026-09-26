/*
 * edge_sensor_state.h — how the node answers "is my sensing working right now?"
 *
 * Two different things decide that, and collapsing them loses information:
 *
 *   1. availability — can the node talk to its sensors at all? That is the
 *      supervisor's job (vendored from AdaptiveSense, see
 *      firmware/common/vendor/adaptivesense/sensor_supervisor.h). It owns bring-up
 *      retry and the consecutive-failure threshold that declares a previously
 *      working sensor gone.
 *
 *   2. channel coverage — of the channels this node is supposed to measure, how
 *      many produced a usable value this cycle? That is the frame's job
 *      (see edge_channel.h).
 *
 * A node with a working SHT30 and an unplugged BH1750 is neither "healthy" nor
 * "down": it is DEGRADED, and saying so is more useful than either extreme. That
 * state is exactly the thing the earlier frame-level `valid` flag could not express.
 *
 * This unit is portable C11 with no ESP-IDF dependency, so the mapping is
 * unit-tested on the host rather than inferred from device logs.
 *
 * What this state is NOT
 * ----------------------
 * It is not a fault verdict. DEGRADED means "this node is not measuring
 * everything it was asked to measure"; it does not mean the sensor is faulty and
 * it says nothing about whether the environment changed. Deciding between a
 * sensor fault and an environmental event is the experiment layer's question
 * (truth lives in experiments/EXP-xxxx/truth/episodes.csv and is never visible
 * to running code). Reporting the symptom is all this unit does.
 */
#ifndef EDGE_SENSOR_STATE_H
#define EDGE_SENSOR_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "edge_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* Every channel this node is expected to measure produced a fresh value. */
    EDGE_SENSOR_STATE_HEALTHY = 0,

    /* At least one expected channel is usable, but not all of them. */
    EDGE_SENSOR_STATE_DEGRADED,

    /* The layer is ready but no expected channel produced a usable value. */
    EDGE_SENSOR_STATE_UNAVAILABLE,

    /* Not usable; the next bring-up attempt is not due yet. */
    EDGE_SENSOR_STATE_RETRY_WAIT,

    /* Not usable, and a bring-up attempt is due now — the node is trying. */
    EDGE_SENSOR_STATE_RECOVERING,

    EDGE_SENSOR_STATE_COUNT
} edge_sensor_state_t;

/*
 * Derive the state.
 *
 *   supervisor_ready    the sensor layer says a read is allowed
 *   bring_up_attempt_due lifecycle is due a (re-)initialisation attempt now
 *   frame               the most recent frame; may be NULL
 *   expected_mask       channels this node is configured to measure
 *
 * When the supervisor is not ready the frame is ignored on purpose: a frame from
 * before an outage says nothing about whether the node can read now.
 */
edge_sensor_state_t edge_sensor_state_derive(bool supervisor_ready,
                                             bool bring_up_attempt_due,
                                             const edge_sensor_frame_t *frame,
                                             uint8_t expected_mask);

/* Stable identifier for logs, telemetry and the dashboard. Never localised. */
const char *edge_sensor_state_name(edge_sensor_state_t state);

/* True for the two states in which at least some measurement is available. */
bool edge_sensor_state_is_usable(edge_sensor_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_SENSOR_STATE_H */
