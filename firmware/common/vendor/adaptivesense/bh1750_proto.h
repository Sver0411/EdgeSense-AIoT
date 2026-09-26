/*
 * bh1750_proto.h — BH1750 / GY-302 ambient light: protocol and availability
 * policy.
 *
 * Pure C99, no ESP-IDF dependency, so both halves can be unit-tested on a
 * workstation with a fake transport (tests/test_bh1750.py).
 *
 * Two things live here, and the split is deliberate:
 *
 *   1. the protocol    command bytes, frame decoding, the raw -> lux conversion
 *                      and the one-shot measurement sequence;
 *   2. the policy      a small state machine that decides whether a measurement
 *                      should be attempted at all, so an absent light sensor does
 *                      not add I2C traffic or latency to every sample.
 *
 * The second is here rather than in the driver for the same reason as the first:
 * it takes `now_ms` as an argument instead of reading a clock, which makes the
 * "unplugged, then plugged back in" behaviour testable without hardware. It is
 * deliberately small — three fields of state — because the light channel is
 * optional and must never be able to take the node down with it.
 *
 * The transport reports how many bytes it actually read, not just success: a
 * short frame is a real failure mode for a protocol that expects exactly two
 * bytes, and collapsing it into "success" is how a plausible-looking wrong value
 * reaches the change detector.
 */
#ifndef ADAPTIVESENSE_BH1750_PROTO_H
#define ADAPTIVESENSE_BH1750_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */

#define BH1750_CMD_POWER_DOWN   0x00u
#define BH1750_CMD_POWER_ON     0x01u
#define BH1750_CMD_RESET        0x07u

/*
 * One Time H-Resolution Mode: start one conversion, then return to power-down by
 * itself. This is what the driver uses; see bh1750_proto_measure() for why.
 */
#define BH1750_CMD_ONE_TIME_H   0x20u

/*
 * Continuous H-Resolution Mode: convert forever at ~120 ms intervals. NOT used by
 * this driver — it is listed only so the choice is visible in the code rather
 * than hidden. See bh1750_proto_measure() for the reasoning.
 */
#define BH1750_CMD_CONT_H       0x10u

/*
 * H-Resolution with the default MTreg (69): 1 lx resolution, 120 ms typical and
 * 180 ms maximum conversion time. The wait below is the datasheet maximum, not
 * the typical value, because waiting for the typical value and then retrying is
 * strictly worse than waiting once for the bound.
 */
#define BH1750_MEAS_WAIT_MS     180u

/* One measurement is exactly two bytes, most significant first. */
#define BH1750_FRAME_LEN        2u

/*
 * lux = raw / 1.2, for the default MTreg of 69. This is the datasheet's
 * conversion factor, not a fitted constant. With a non-default MTreg the result
 * scales by 69 / MTreg; this driver never changes MTreg, so the factor is fixed.
 *
 * Where this factor is verified from, and how far that goes: it is the
 * datasheet's formula, not a fitted constant. One hardware observation is
 * consistent with it — in the first hardware session the part returned raw = 66,
 * which this formula turns into 55.0 lx, plausible for the indoor conditions
 * observed. That is a sanity check against a plausible reading, not a
 * calibration: no independent lux meter or calibrated reference instrument was
 * used, and none is claimed.
 */
#define BH1750_LUX_DIVISOR      1.2f

/* Largest lux the 16-bit register can express: 65535 / 1.2. */
#define BH1750_LUX_MAX          54612.5f

/*
 * How many consecutive failed measurements declare the light sensor gone. Two,
 * not one: a single failed transfer should not drop the channel (it recovers on
 * the next sample anyway, and the channel is optional), while a part that has
 * actually been unplugged fails deterministically on the second attempt too.
 */
#define BH1750_FAILURES_BEFORE_UNAVAILABLE 2u

/* ------------------------------------------------------------------ */
/* transport                                                           */
/* ------------------------------------------------------------------ */

/*
 * Supplied by the caller. `read` returns the number of bytes delivered
 * (>= 0) or a negative value on error — a short read is reported as a short
 * count rather than as success, so the caller can reject it.
 */
typedef struct {
    int (*write)(void *ctx, const uint8_t *cmd, size_t len);
    int (*read)(void *ctx, uint8_t *out, size_t len);
    void (*delay_ms)(void *ctx, unsigned ms);
    void *ctx;
} bh1750_io_t;

/* ------------------------------------------------------------------ */
/* conversion                                                          */
/* ------------------------------------------------------------------ */

/* Big-endian 16-bit register value from a two-byte frame. */
uint16_t bh1750_raw_from_frame(const uint8_t frame[BH1750_FRAME_LEN]);

/*
 * raw -> lux. Always finite, never negative, never NaN: the input is a 16-bit
 * unsigned count and the divisor is a positive constant, so the range is
 * [0, BH1750_LUX_MAX] by construction. A raw count of 0 is a real reading
 * (dark), not an error.
 */
float bh1750_lux_from_raw(uint16_t raw);

/*
 * One measurement, end to end: power on, start a one-shot H-resolution
 * conversion, wait `wait_ms`, read two bytes, convert.
 *
 * Returns 0 on success and only then writes `*lux`; -1 on any failure
 * (transport error, short read), leaving `*lux` untouched. `io` may not be NULL.
 *
 * Why one-shot rather than continuous (BH1750_CMD_CONT_H):
 *
 *   AdaptiveSense samples every 5-60 s. In continuous mode the part re-converts
 *   about every 120 ms and draws its measurement current the whole time — on the
 *   order of ten thousand conversions between two samples, of which all but one
 *   are discarded, and the current keeps flowing while the ESP32 is in light
 *   sleep because the board's 3.3 V rail does not go away. That would spend
 *   power on a channel we look at once a minute, and it would show up in exactly
 *   the sleep statistics this project reports.
 *
 *   One-shot converts only when asked and returns to power-down by itself. Its
 *   cost is a bounded wait (BH1750_MEAS_WAIT_MS, the datasheet maximum) per
 *   sample: at most 3.6 % of a 5 s interval and 0.3 % of a 60 s one.
 *
 * The wait is a fixed constant and is never a poll-for-completion loop, so it
 * cannot block indefinitely; every failure path returns -1.
 */
int bh1750_proto_measure(const bh1750_io_t *io, unsigned wait_ms, float *lux);

/* ------------------------------------------------------------------ */
/* availability policy                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    bool     available;              /* a measurement is expected to work */
    unsigned consecutive_failures;
    unsigned long next_probe_ms;     /* earliest re-probe, while unavailable */
    unsigned long probe_interval_ms; /* spacing between re-probes */
    /* Counters, so an outage can be reported rather than inferred. */
    unsigned reads;                  /* measurements that produced a lux value */
    unsigned failures;               /* measurements that were attempted and failed */
    unsigned skips;                  /* samples that did not attempt one */
    unsigned unavailability_events;
} bh1750_state_t;

/* Arm the policy. The first attempt is allowed immediately. */
void bh1750_state_init(bh1750_state_t *s, unsigned long probe_interval_ms,
                       unsigned long now_ms);

/*
 * Should a measurement be attempted at `now_ms`?
 *
 * True while the sensor is believed present, and — once it has been declared
 * gone — only when the backoff interval has elapsed. A "probe" is simply an
 * attempt at a measurement: this part has no identity register worth reading, so
 * the cheapest way to find out whether it is back is to ask it for a value.
 *
 * Note what this does *not* promise: it is a minimum backoff, not a timer. The
 * caller decides when to ask, and in this firmware that is on the node's normal
 * sampling cadence — so with the node sampling every 60 s, an absent light sensor
 * is re-probed on the first sample after the backoff elapses, which may be much
 * later than the backoff itself. That is deliberate: re-probing is an I2C
 * transaction, and running it from its own task would keep the chip awake for a
 * channel that is optional.
 */
bool bh1750_should_attempt(const bh1750_state_t *s, unsigned long now_ms);

/*
 * Record the outcome of an attempt made at `now_ms`. On success the sensor is
 * marked present and the failure streak resets; on failure the streak grows, and
 * once it reaches BH1750_FAILURES_BEFORE_UNAVAILABLE the sensor is marked absent
 * and the next attempt is pushed out by `probe_interval_ms`.
 */
void bh1750_note_result(bh1750_state_t *s, bool ok, unsigned long now_ms);

/* Record that no attempt was made this sample (sensor known absent, backing off). */
void bh1750_note_skip(bh1750_state_t *s);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVESENSE_BH1750_PROTO_H */
