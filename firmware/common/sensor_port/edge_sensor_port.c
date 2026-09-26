/*
 * edge_sensor_port.c — device-side glue. See edge_sensor_port.h for the boundary.
 */
#include "edge_sensor_port.h"

#include <string.h>

#include "edge_config_generated.h"
#include "edge_sensor_map.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sensor.h"
#include "sensor_supervisor.h"

static const char *TAG = "edge_sensor";

/*
 * Bring-up retry spacing. Deliberately not the sampling interval: a node whose
 * sensor is unplugged should re-probe at a modest fixed rate rather than on every
 * cycle, so a missing part cannot turn into a busy loop.
 */
#define EDGE_SENSOR_RETRY_INTERVAL_S 5.0

/*
 * Consecutive read failures before a previously working sensor is declared gone.
 * A threshold rather than one failure: a single bad transfer should not tear down
 * a driver that is otherwise working.
 */
#define EDGE_SENSOR_FAILURES_BEFORE_UNAVAILABLE 3u

static sensor_supervisor_t s_supervisor;
static uint8_t s_enabled_mask;
static bool s_armed;

static double port_now_s(void)
{
    return (double)esp_timer_get_time() / 1000000.0;
}

bool edge_sensor_port_init(uint8_t enabled_mask)
{
    if (enabled_mask == 0u) {
        /* A node expected to measure nothing cannot be brought up meaningfully.
         * Refuse rather than arm a supervisor that can never reach HEALTHY. */
        return false;
    }
    s_enabled_mask = enabled_mask;
    sensor_sup_init(&s_supervisor, EDGE_SENSOR_RETRY_INTERVAL_S,
                    EDGE_SENSOR_FAILURES_BEFORE_UNAVAILABLE, port_now_s());
    s_armed = true;
    return true;
}

const char *edge_sensor_port_backend_name(void)
{
    return sensor_backend_name();
}

unsigned edge_sensor_port_init_attempts(void)
{
    return s_supervisor.init_attempts;
}

unsigned edge_sensor_port_init_failures(void)
{
    return s_supervisor.init_failures;
}

unsigned edge_sensor_port_read_failures_total(void)
{
    return s_supervisor.read_failures_total;
}

unsigned edge_sensor_port_unavailability_events(void)
{
    return s_supervisor.unavailability_events;
}

edge_sensor_state_t edge_sensor_port_read(edge_sensor_frame_t *out,
                                          uint64_t node_ts_ms,
                                          bool *attempted_bring_up)
{
    if (attempted_bring_up != NULL) {
        *attempted_bring_up = false;
    }
    if (out == NULL) {
        return EDGE_SENSOR_STATE_UNAVAILABLE;
    }
    if (!s_armed) {
        edge_frame_begin(out, node_ts_ms);
        return EDGE_SENSOR_STATE_UNAVAILABLE;
    }

    double now = port_now_s();
    bool attempted = false;

    if (sensor_sup_should_attempt(&s_supervisor, now)) {
        int rc = sensor_init();
        sensor_sup_note_attempt(&s_supervisor, now, rc == 0);
        attempted = true;
        if (rc == 0) {
            ESP_LOGI(TAG, "sensor backend ready: %s", sensor_backend_name());
        } else {
            ESP_LOGW(TAG, "sensor bring-up failed; will retry in %.0f s",
                     EDGE_SENSOR_RETRY_INTERVAL_S);
        }
    }

    /* Pessimistic default: assume nothing was measured. Each channel is promoted
     * only by an actual successful read below, so a skipped path cannot leave a
     * stale "OK" behind. */
    edge_driver_report_t report;
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        report.value[i] = 0.0f;
        report.status[i] = EDGE_CH_READ_FAILED;
    }

    if (sensor_sup_ready(&s_supervisor)) {
        sensor_read_t reading;
        memset(&reading, 0, sizeof(reading));
        if (sensor_read(&reading) == 0) {
            sensor_sup_note_read_success(&s_supervisor);

            report.value[EDGE_CH_TEMPERATURE] = reading.value[SEN_CH_TEMPERATURE];
            report.status[EDGE_CH_TEMPERATURE] =
                reading.valid[SEN_CH_TEMPERATURE] ? EDGE_CH_OK : EDGE_CH_READ_FAILED;

            report.value[EDGE_CH_HUMIDITY] = reading.value[SEN_CH_HUMIDITY];
            report.status[EDGE_CH_HUMIDITY] =
                reading.valid[SEN_CH_HUMIDITY] ? EDGE_CH_OK : EDGE_CH_READ_FAILED;

            report.value[EDGE_CH_LIGHT] = reading.value[SEN_CH_LIGHT];
            report.status[EDGE_CH_LIGHT] =
                reading.valid[SEN_CH_LIGHT] ? EDGE_CH_OK : EDGE_CH_READ_FAILED;

            /* The vendored layer can report pressure; EdgeSense has no pressure
             * channel, so it is left out of the report entirely (and the enabled
             * mask has no bit for it either). */
        } else {
            if (sensor_sup_note_read_failure(&s_supervisor, now)) {
                ESP_LOGW(TAG,
                         "sensor declared unavailable after %u consecutive read failures",
                         EDGE_SENSOR_FAILURES_BEFORE_UNAVAILABLE);
            }
        }
    }

    bool bring_up_due = sensor_sup_should_attempt(&s_supervisor, port_now_s());
    if (attempted_bring_up != NULL) {
        *attempted_bring_up = attempted;
    }

    if (!edge_sensor_map(&report, s_enabled_mask, node_ts_ms, out)) {
        edge_frame_begin(out, node_ts_ms);
    }

    return edge_sensor_state_derive(sensor_sup_ready(&s_supervisor), bring_up_due, out,
                                   s_enabled_mask);
}
