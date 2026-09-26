/*
 * edge_sensor_state.c — implementation of the node sensor state mapping.
 * See edge_sensor_state.h for the rationale and for why this is not a fault verdict.
 */
#include "edge_sensor_state.h"

#include <stddef.h>

edge_sensor_state_t edge_sensor_state_derive(bool supervisor_ready,
                                             bool bring_up_attempt_due,
                                             const edge_sensor_frame_t *frame,
                                             uint8_t expected_mask)
{
    if (!supervisor_ready) {
        return bring_up_attempt_due ? EDGE_SENSOR_STATE_RECOVERING
                                    : EDGE_SENSOR_STATE_RETRY_WAIT;
    }

    /* A node configured to measure nothing cannot be unhealthy; it is simply
     * not measuring anything. Refusing to report HEALTHY here would make an
     * empty configuration look like a fault. */
    if (expected_mask == 0u) {
        return EDGE_SENSOR_STATE_UNAVAILABLE;
    }

    if (edge_frame_valid_count(frame, expected_mask) == 0u) {
        /* The layer says it is ready but nothing usable came back. This is the
         * state the pre-existing supervisor's own documentation describes: the
         * driver claimed to be ready while every read failed. */
        return EDGE_SENSOR_STATE_UNAVAILABLE;
    }

    if (edge_frame_all_ok(frame, expected_mask)) {
        return EDGE_SENSOR_STATE_HEALTHY;
    }

    return EDGE_SENSOR_STATE_DEGRADED;
}

const char *edge_sensor_state_name(edge_sensor_state_t state)
{
    switch (state) {
    case EDGE_SENSOR_STATE_HEALTHY:
        return "healthy";
    case EDGE_SENSOR_STATE_DEGRADED:
        return "degraded";
    case EDGE_SENSOR_STATE_UNAVAILABLE:
        return "unavailable";
    case EDGE_SENSOR_STATE_RETRY_WAIT:
        return "retry_wait";
    case EDGE_SENSOR_STATE_RECOVERING:
        return "recovering";
    case EDGE_SENSOR_STATE_COUNT:
        break;
    }
    return "unknown";
}

bool edge_sensor_state_is_usable(edge_sensor_state_t state)
{
    return state == EDGE_SENSOR_STATE_HEALTHY || state == EDGE_SENSOR_STATE_DEGRADED;
}
