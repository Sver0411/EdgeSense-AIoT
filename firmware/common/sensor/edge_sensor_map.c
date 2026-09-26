/*
 * edge_sensor_map.c — see edge_sensor_map.h for the policies this applies.
 */
#include "edge_sensor_map.h"

#include <stddef.h>

bool edge_sensor_map(const edge_driver_report_t *report, uint8_t enabled_mask,
                     uint64_t node_ts_ms, edge_sensor_frame_t *out)
{
    if (report == NULL || out == NULL) {
        return false;
    }

    /* Start from a clean frame so a caller cannot observe a half-built result. */
    edge_frame_begin(out, node_ts_ms);

    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        edge_channel_t channel = (edge_channel_t)i;

        if ((enabled_mask & EDGE_CH_MASK(channel)) == 0u) {
            /* Configuration decides which channels exist on this node. A report
             * that still carries a value for a disabled channel is not evidence
             * that the node measures it. */
            (void)edge_frame_set_invalid(out, channel, EDGE_CH_NOT_SAMPLED);
            continue;
        }

        edge_channel_status_t status = report->status[i];
        if (status == EDGE_CH_OK) {
            if (!edge_frame_set(out, channel, report->value[i])) {
                /* The frame contract refused the value (not finite). Record the
                 * reason instead of accepting it as a measurement. */
                (void)edge_frame_set_invalid(out, channel, EDGE_CH_OUT_OF_RANGE);
            }
            continue;
        }

        if ((int)status >= (int)EDGE_CH_STATUS_COUNT || status == EDGE_CH_OK) {
            /* An unrecognised status is treated as a failed read, not as a
             * measurement, so a driver bug cannot promote itself into data. */
            (void)edge_frame_set_invalid(out, channel, EDGE_CH_READ_FAILED);
            continue;
        }

        (void)edge_frame_set_invalid(out, channel, status);
    }

    return true;
}
