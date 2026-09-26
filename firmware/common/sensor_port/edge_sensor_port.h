/*
 * edge_sensor_port.h — the device-side glue between the vendored sensor layer and
 * an EdgeSense sensor frame.
 *
 * This is the only unit in the sensing path that may touch ESP-IDF. It owns the
 * availability supervisor (vendored), calls the vendored driver, fills an
 * edge_driver_report_t, and hands it to the pure mapper. All the policy that could
 * be got wrong lives in edge_sensor_map.c, which is unit-tested on the host; what
 * remains here is I2C plumbing and lifecycle.
 *
 * Not covered yet: the soil channel. EDGE_SOIL_ENABLED is 0, the port therefore
 * never samples it, and the mapper records it as NOT_SAMPLED. Wiring the vendored
 * soil module in is a separate task that needs a calibrated probe first.
 */
#ifndef EDGE_SENSOR_PORT_H
#define EDGE_SENSOR_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "edge_channel.h"
#include "edge_sensor_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Arm the supervisor and prepare the sensor subsystem. `enabled_mask` is the node's
 * expected channel set (from the node roster). Returns false only if the arguments
 * are unusable; a missing sensor is not an initialisation failure here, it is a
 * condition the supervisor reports and retries.
 */
bool edge_sensor_port_init(uint8_t enabled_mask);

/*
 * Take one reading cycle.
 *
 * Always produces a frame: when the sensor is unreachable, every enabled channel is
 * invalid with a reason, which is exactly what the frame contract is for. Returns
 * the derived node sensor state. `attempted_bring_up` reports whether this cycle
 * spent time re-probing, which is what distinguishes RETRY_WAIT from RECOVERING.
 */
edge_sensor_state_t edge_sensor_port_read(edge_sensor_frame_t *out,
                                          uint64_t node_ts_ms,
                                          bool *attempted_bring_up);

/* Human-readable name of the active vendored backend, for the boot banner. */
const char *edge_sensor_port_backend_name(void);

/* Diagnostics from the vendored supervisor, for the status log. */
unsigned edge_sensor_port_init_attempts(void);
unsigned edge_sensor_port_init_failures(void);
unsigned edge_sensor_port_read_failures_total(void);
unsigned edge_sensor_port_unavailability_events(void);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_SENSOR_PORT_H */
