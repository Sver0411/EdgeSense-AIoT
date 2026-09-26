/*
 * edge_sensor_map.h — the pure step between a driver reading and a sensor frame.
 *
 * The driver layer (edge_sensor_port) knows about I2C, chip addresses and
 * ESP-IDF. This unit knows none of that: it takes a per-channel driver report and
 * produces an edge_sensor_frame_t under explicit policies. Keeping it separate is
 * what makes the mapping testable on a workstation, and the mapping is where the
 * rules that matter actually live:
 *
 *   * a channel the driver reports as absent or failed becomes an invalid channel
 *     with that reason — not a zero, and not a stale value;
 *   * a value the frame contract refuses (NaN, infinity) becomes OUT_OF_RANGE
 *     rather than being passed through;
 *   * a channel the configuration disables is NOT_SAMPLED whatever the driver
 *     says, so an unconfigured sensor cannot quietly become part of the data.
 *
 * Portable C11, no allocation, no ESP-IDF.
 */
#ifndef EDGE_SENSOR_MAP_H
#define EDGE_SENSOR_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "edge_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the driver layer managed to get for one channel this cycle.
 * EDGE_CH_OK means "value holds a fresh measurement"; any other status means the
 * value field is meaningless and must be ignored.
 */
typedef struct {
    float                 value[EDGE_CH_COUNT];
    edge_channel_status_t status[EDGE_CH_COUNT];
} edge_driver_report_t;

/*
 * Map a driver report into a frame.
 *
 * `enabled_mask` lists the channels this node's configuration enables. Channels
 * outside it are recorded as EDGE_CH_NOT_SAMPLED with no value, regardless of what
 * the report claims. Returns false if any argument is NULL; on failure `out` is
 * left cleared rather than half-filled.
 */
bool edge_sensor_map(const edge_driver_report_t *report, uint8_t enabled_mask,
                     uint64_t node_ts_ms, edge_sensor_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_SENSOR_MAP_H */
