/*
 * bh1750_proto.c — see bh1750_proto.h.
 *
 * Pure C99, no ESP-IDF dependency, host-testable.
 */
#include <string.h>

#include "bh1750_proto.h"

uint16_t bh1750_raw_from_frame(const uint8_t frame[BH1750_FRAME_LEN])
{
    if (frame == NULL) {
        return 0u;
    }
    /* Most significant byte first. */
    return (uint16_t)(((uint16_t)frame[0] << 8) | (uint16_t)frame[1]);
}

float bh1750_lux_from_raw(uint16_t raw)
{
    return (float)raw / BH1750_LUX_DIVISOR;
}

int bh1750_proto_measure(const bh1750_io_t *io, unsigned wait_ms, float *lux)
{
    if (io == NULL || io->write == NULL || io->read == NULL || lux == NULL) {
        return -1;
    }

    /* Recover from power-down, then start exactly one conversion. A one-shot
     * measurement leaves the part in power-down again, so both commands are
     * needed on every sample. */
    const uint8_t power_on = (uint8_t)BH1750_CMD_POWER_ON;
    if (io->write(io->ctx, &power_on, 1u) != 0) {
        return -1;
    }

    const uint8_t start = (uint8_t)BH1750_CMD_ONE_TIME_H;
    if (io->write(io->ctx, &start, 1u) != 0) {
        return -1;
    }

    /* Bounded, never a completion poll: the part has no "done" signal to read. */
    if (io->delay_ms != NULL) {
        io->delay_ms(io->ctx, wait_ms);
    }

    uint8_t frame[BH1750_FRAME_LEN] = {0u, 0u};
    const int got = io->read(io->ctx, frame, BH1750_FRAME_LEN);
    if (got != (int)BH1750_FRAME_LEN) {
        return -1; /* transport error, or a short frame: both are failures */
    }

    *lux = bh1750_lux_from_raw(bh1750_raw_from_frame(frame));
    return 0;
}

/* ------------------------------------------------------------------ */
/* availability policy                                                 */
/* ------------------------------------------------------------------ */

void bh1750_state_init(bh1750_state_t *s, unsigned long probe_interval_ms,
                       unsigned long now_ms)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->available = true;          /* optimistic: the first attempt is free */
    s->probe_interval_ms = probe_interval_ms;
    s->next_probe_ms = now_ms;
}

bool bh1750_should_attempt(const bh1750_state_t *s, unsigned long now_ms)
{
    if (s == NULL) {
        return false;
    }
    if (s->available) {
        return true;
    }
    return (long)(now_ms - s->next_probe_ms) >= 0;
}

void bh1750_note_result(bh1750_state_t *s, bool ok, unsigned long now_ms)
{
    if (s == NULL) {
        return;
    }
    if (ok) {
        s->available = true;
        s->consecutive_failures = 0u;
        s->reads++;
        return;
    }

    s->failures++;
    s->consecutive_failures++;
    if (s->consecutive_failures >= BH1750_FAILURES_BEFORE_UNAVAILABLE) {
        if (s->available) {
            /* Only count the transition, not every failed attempt inside it. */
            s->unavailability_events++;
        }
        s->available = false;
        s->next_probe_ms = now_ms + s->probe_interval_ms;
    }
}

void bh1750_note_skip(bh1750_state_t *s)
{
    if (s != NULL) {
        s->skips++;
    }
}
