/*
 * edge_channel.h — EdgeSense sensor frame contract.
 *
 * This is the boundary at which "the node has a reading" stops being an
 * assumption and becomes an explicit, per-channel fact.
 *
 * Why it exists
 * -------------
 * The pre-existing sensor base (see docs/vendored/adaptive-lora-iot.md) carried a
 * single frame-level `valid` flag. On real hardware that produced a demonstrable
 * defect: an unconnected capacitive soil probe sat at its rail value (raw 4095)
 * while the frame reported `valid = 1`, so a dead channel was published as a
 * healthy measurement. A single flag cannot express "temperature and humidity are
 * good, light is not, soil was never sampled".
 *
 * So validity here is per channel, and a channel that is not valid carries no
 * value at all.
 *
 * The three rules this file enforces
 * ----------------------------------
 *   R1  Every channel starts a cycle with `valid == false` and `value == 0.0f`.
 *       `edge_frame_begin()` rewrites the whole frame, so a previous cycle's
 *       value can never survive into the current one. There is deliberately no
 *       last-known-value field: v1 would rather report "unknown" than publish a
 *       stale number as if it were current.
 *
 *   R2  A non-finite value is never a measurement. `edge_frame_set()` refuses
 *       NaN and infinities and records EDGE_CH_OUT_OF_RANGE instead. A sensor
 *       driver that produces NaN is signalling a failure, not a reading.
 *
 *   R3  "Invalid" and "OK" cannot both be claimed. `edge_frame_set_invalid()`
 *       rejects EDGE_CH_OK, so a caller cannot mark a channel invalid while
 *       simultaneously claiming the read succeeded.
 *
 * Consequence for callers: the only way to obtain a channel's number is
 * `edge_frame_value_or()`, which requires an explicit fallback. Reading the raw
 * `value[]` array is possible for encoding, but the fallback-taking accessor
 * makes the "this may not be a measurement" decision visible at every call site.
 *
 * This unit is portable C11: no dynamic memory, no RTOS, no ESP-IDF, no
 * hardware headers. That is what lets it be compiled and unit-tested on a
 * workstation, and what keeps the algorithm layer testable off-device.
 */
#ifndef EDGE_CHANNEL_H
#define EDGE_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Channels EdgeSense expects on a sensor node.
 *
 * Pressure is deliberately absent: nothing on the EdgeSense build measures it.
 * (AdaptiveSense's sensor layer, which is vendored alongside this file, can
 * report pressure; the port layer simply does not map it into a frame.)
 */
typedef enum {
    EDGE_CH_TEMPERATURE = 0,
    EDGE_CH_HUMIDITY,
    EDGE_CH_LIGHT,
    EDGE_CH_SOIL,
    EDGE_CH_COUNT
} edge_channel_t;

/*
 * Why a channel carries no usable value this cycle.
 *
 * Only EDGE_CH_OK means "the value in this frame is a fresh measurement".
 * Everything else is a reason, and the reason is data: it is what lets a later
 * diagnosis say "the light channel was absent" rather than "the value looked odd".
 */
typedef enum {
    EDGE_CH_OK = 0,
    EDGE_CH_NOT_SAMPLED,   /* this cycle did not attempt the channel          */
    EDGE_CH_ABSENT,        /* no sensor for this channel on this node         */
    EDGE_CH_READ_FAILED,   /* the part answered but the transfer failed       */
    EDGE_CH_TIMEOUT,       /* the transfer did not complete in time           */
    EDGE_CH_CRC_ERROR,     /* the frame failed its checksum                   */
    EDGE_CH_OUT_OF_RANGE,  /* decoded value is not physically plausible       */
    EDGE_CH_STATUS_COUNT
} edge_channel_status_t;

#define EDGE_CH_MASK(ch) ((uint8_t)(1u << (unsigned)(ch)))
#define EDGE_CH_MASK_ALL ((uint8_t)((1u << (unsigned)EDGE_CH_COUNT) - 1u))

/* One sampling cycle from one node. Fixed size; no allocation. */
typedef struct {
    float                 value[EDGE_CH_COUNT];
    edge_channel_status_t status[EDGE_CH_COUNT];
    bool                  valid[EDGE_CH_COUNT];
    uint64_t              node_ts_ms; /* node monotonic clock, never wall time */
} edge_sensor_frame_t;

/*
 * Start a new cycle. Rule R1: every channel becomes NOT_SAMPLED / invalid / 0.0f.
 * Call this once per sampling cycle, before recording any result.
 */
void edge_frame_begin(edge_sensor_frame_t *frame, uint64_t node_ts_ms);

/*
 * Record a successful read. Returns false — and changes nothing — if the value is
 * not finite (rule R2) or the channel is out of range.
 */
bool edge_frame_set(edge_sensor_frame_t *frame, edge_channel_t ch, float value);

/*
 * Record that the channel produced no usable value, with a reason.
 * Returns false, and changes nothing, if `status` is EDGE_CH_OK (rule R3) or the
 * channel is out of range. On success the channel's value is set to 0.0f.
 */
bool edge_frame_set_invalid(edge_sensor_frame_t *frame, edge_channel_t ch,
                            edge_channel_status_t status);

/* True only when the channel was successfully read during this cycle. */
bool edge_frame_channel_ok(const edge_sensor_frame_t *frame, edge_channel_t ch);

/*
 * The value to use in a computation, or `fallback` when the channel is not
 * valid. Prefer this over reading `value[]` directly: it forces the caller to
 * state what "no measurement" means for that computation.
 */
float edge_frame_value_or(const edge_sensor_frame_t *frame, edge_channel_t ch,
                          float fallback);

/* How many of the channels selected by `mask` are valid. */
unsigned edge_frame_valid_count(const edge_sensor_frame_t *frame, uint8_t mask);

/* All / at least one of the channels selected by `mask` are valid. */
bool edge_frame_all_ok(const edge_sensor_frame_t *frame, uint8_t mask);
bool edge_frame_any_ok(const edge_sensor_frame_t *frame, uint8_t mask);

const char *edge_channel_name(edge_channel_t ch);
const char *edge_channel_status_name(edge_channel_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_CHANNEL_H */
