/*
 * edge_channel.c — implementation of the EdgeSense sensor frame contract.
 * See edge_channel.h for the contract and the three rules it enforces.
 */
#include "edge_channel.h"

#include <math.h>
#include <stddef.h>

static bool channel_in_range(edge_channel_t ch)
{
    return (int)ch >= 0 && (int)ch < (int)EDGE_CH_COUNT;
}

static bool status_in_range(edge_channel_status_t status)
{
    return (int)status >= 0 && (int)status < (int)EDGE_CH_STATUS_COUNT;
}

void edge_frame_begin(edge_sensor_frame_t *frame, uint64_t node_ts_ms)
{
    if (frame == NULL) {
        return;
    }
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        frame->value[i] = 0.0f;
        frame->status[i] = EDGE_CH_NOT_SAMPLED;
        frame->valid[i] = false;
    }
    frame->node_ts_ms = node_ts_ms;
}

bool edge_frame_set(edge_sensor_frame_t *frame, edge_channel_t ch, float value)
{
    if (frame == NULL || !channel_in_range(ch)) {
        return false;
    }
    if (!isfinite(value)) {
        /* Rule R2: NaN and infinities are failures, not measurements. Record the
         * reason rather than dropping the attempt silently. */
        frame->value[(int)ch] = 0.0f;
        frame->status[(int)ch] = EDGE_CH_OUT_OF_RANGE;
        frame->valid[(int)ch] = false;
        return false;
    }
    frame->value[(int)ch] = value;
    frame->status[(int)ch] = EDGE_CH_OK;
    frame->valid[(int)ch] = true;
    return true;
}

bool edge_frame_set_invalid(edge_sensor_frame_t *frame, edge_channel_t ch,
                            edge_channel_status_t status)
{
    if (frame == NULL || !channel_in_range(ch)) {
        return false;
    }
    if (!status_in_range(status) || status == EDGE_CH_OK) {
        /* Rule R3: you cannot declare a channel invalid and OK at the same time. */
        return false;
    }
    frame->value[(int)ch] = 0.0f;
    frame->status[(int)ch] = status;
    frame->valid[(int)ch] = false;
    return true;
}

bool edge_frame_channel_ok(const edge_sensor_frame_t *frame, edge_channel_t ch)
{
    if (frame == NULL || !channel_in_range(ch)) {
        return false;
    }
    return frame->valid[(int)ch];
}

float edge_frame_value_or(const edge_sensor_frame_t *frame, edge_channel_t ch,
                          float fallback)
{
    if (frame == NULL || !channel_in_range(ch) || !frame->valid[(int)ch]) {
        return fallback;
    }
    return frame->value[(int)ch];
}

unsigned edge_frame_valid_count(const edge_sensor_frame_t *frame, uint8_t mask)
{
    if (frame == NULL) {
        return 0u;
    }
    unsigned count = 0u;
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        if ((mask & EDGE_CH_MASK((edge_channel_t)i)) != 0u && frame->valid[i]) {
            count += 1u;
        }
    }
    return count;
}

bool edge_frame_all_ok(const edge_sensor_frame_t *frame, uint8_t mask)
{
    if (frame == NULL) {
        return false;
    }
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        if ((mask & EDGE_CH_MASK((edge_channel_t)i)) != 0u && !frame->valid[i]) {
            return false;
        }
    }
    return true;
}

bool edge_frame_any_ok(const edge_sensor_frame_t *frame, uint8_t mask)
{
    return edge_frame_valid_count(frame, mask) > 0u;
}

const char *edge_channel_name(edge_channel_t ch)
{
    switch (ch) {
    case EDGE_CH_TEMPERATURE:
        return "temperature";
    case EDGE_CH_HUMIDITY:
        return "humidity";
    case EDGE_CH_LIGHT:
        return "light";
    case EDGE_CH_SOIL:
        return "soil";
    case EDGE_CH_COUNT:
        break;
    }
    return "unknown";
}

const char *edge_channel_status_name(edge_channel_status_t status)
{
    switch (status) {
    case EDGE_CH_OK:
        return "ok";
    case EDGE_CH_NOT_SAMPLED:
        return "not_sampled";
    case EDGE_CH_ABSENT:
        return "absent";
    case EDGE_CH_READ_FAILED:
        return "read_failed";
    case EDGE_CH_TIMEOUT:
        return "timeout";
    case EDGE_CH_CRC_ERROR:
        return "crc_error";
    case EDGE_CH_OUT_OF_RANGE:
        return "out_of_range";
    case EDGE_CH_STATUS_COUNT:
        break;
    }
    return "unknown";
}
