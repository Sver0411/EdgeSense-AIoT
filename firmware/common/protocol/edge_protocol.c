/*
 * edge_protocol.c — EdgeSense Protocol v1 codec and stream parser.
 * See edge_protocol.h for the layout, the reuse boundaries and the wire invariants.
 */
#include "edge_protocol.h"

#include <math.h>
#include <string.h>

#include "edge_config_generated.h"
/* The vendored EventGuard-LoRa transport supplies the CRC. Reusing it rather than
 * writing a second CRC-16 is deliberate: two implementations of the same checksum
 * is two chances to disagree on the wire. */
#include "protocol.h"

_Static_assert(EDGE_CH_COUNT == 4, "status packing assumes four channels");
_Static_assert(EDGE_CH_STATUS_COUNT <= 8, "three bits per channel must hold every status");
_Static_assert(EDGE_FRAME_DATA_SIZE == 28u, "DATA size must match the documented layout");
_Static_assert(EDGE_FRAME_ACK_SIZE == 12u, "ACK size must match the documented layout");
_Static_assert(EDGE_FRAME_HEARTBEAT_SIZE == 12u, "HEARTBEAT size must match the layout");
_Static_assert(EDGE_FRAME_BEACON_SIZE == 15u, "BEACON size must match the documented layout");
_Static_assert(EDGE_MAX_FRAME_SIZE == EDGE_FRAME_DATA_SIZE, "DATA is the largest frame");

#define EDGE_STATUS_BITS_PER_CHANNEL 3u

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)((uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16));
}

static uint8_t frame_version(void)
{
    return (uint8_t)EDGE_PROTOCOL_VERSION;
}

size_t edge_protocol_frame_size(edge_frame_type_t type)
{
    switch (type) {
    case EDGE_FRAME_DATA:
        return EDGE_FRAME_DATA_SIZE;
    case EDGE_FRAME_ACK:
        return EDGE_FRAME_ACK_SIZE;
    case EDGE_FRAME_HEARTBEAT:
        return EDGE_FRAME_HEARTBEAT_SIZE;
    case EDGE_FRAME_BEACON:
        return EDGE_FRAME_BEACON_SIZE;
    }
    return 0u;
}

const char *edge_frame_type_name(edge_frame_type_t type)
{
    switch (type) {
    case EDGE_FRAME_DATA:
        return "data";
    case EDGE_FRAME_ACK:
        return "ack";
    case EDGE_FRAME_HEARTBEAT:
        return "heartbeat";
    case EDGE_FRAME_BEACON:
        return "beacon";
    }
    return "unknown";
}

int edge_protocol_version(void)
{
    return (int)EDGE_PROTOCOL_VERSION;
}

static void begin_frame(uint8_t *out, uint8_t type, uint8_t node_id)
{
    out[0] = (uint8_t)EDGE_PROTOCOL_MAGIC_0;
    out[1] = (uint8_t)EDGE_PROTOCOL_MAGIC_1;
    out[2] = frame_version();
    out[3] = type;
    out[4] = node_id;
}

/* Accepts a frame only when the magic, version, type, exact length and CRC all
 * agree. Nothing partial is ever treated as a frame. */
static bool frame_header_ok(const uint8_t *f, size_t length, edge_frame_type_t type,
                            size_t expected, size_t crc_offset)
{
    if (f == NULL || length != expected) {
        return false;
    }
    if (f[0] != (uint8_t)EDGE_PROTOCOL_MAGIC_0 || f[1] != (uint8_t)EDGE_PROTOCOL_MAGIC_1) {
        return false;
    }
    if (f[2] != frame_version() || f[3] != (uint8_t)type) {
        return false;
    }
    return get16(f + crc_offset) == eg_crc16(f, crc_offset);
}

/* ---- fixed-point conversion -------------------------------------------------
 *
 * Rounded half-away-from-zero, then range-checked. A value that does not fit is
 * refused rather than clamped: clamping would put a number on the wire that no
 * sensor produced.
 */
static int32_t round_to_int(float value)
{
    return (int32_t)(value >= 0.0f ? (value + 0.5f) : (value - 0.5f));
}

static bool encode_signed16(float value, float scale, int32_t *out)
{
    if (!isfinite(value)) {
        return false;
    }
    int32_t scaled = round_to_int(value * scale);
    if (scaled < -32768 || scaled > 32767) {
        return false;
    }
    *out = scaled;
    return true;
}

static bool encode_unsigned16(float value, float scale, int32_t *out)
{
    if (!isfinite(value)) {
        return false;
    }
    int32_t scaled = round_to_int(value * scale);
    if (scaled < 0 || scaled > 65535) {
        return false;
    }
    *out = scaled;
    return true;
}

static uint16_t pack_statuses(const edge_sensor_frame_t *frame)
{
    uint16_t packed = 0u;
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        packed |= (uint16_t)((uint16_t)(frame->status[i] & 0x7) << (EDGE_STATUS_BITS_PER_CHANNEL * (unsigned)i));
    }
    return packed;
}

static edge_channel_status_t unpack_status(uint16_t packed, int channel)
{
    unsigned shift = EDGE_STATUS_BITS_PER_CHANNEL * (unsigned)channel;
    return (edge_channel_status_t)((packed >> shift) & 0x7u);
}

/*
 * Rule W2, checked in both directions: a channel is valid if and only if its
 * status is OK. A frame that claims otherwise is a bug or a forgery, and neither
 * should be repaired silently.
 */
static bool channels_are_self_consistent(const edge_sensor_frame_t *frame)
{
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        bool valid = frame->valid[i];
        bool status_ok = (frame->status[i] == EDGE_CH_OK);
        if (valid != status_ok) {
            return false;
        }
    }
    return true;
}

static float decode_channel_value(const uint8_t *f, int channel)
{
    switch ((edge_channel_t)channel) {
    case EDGE_CH_TEMPERATURE:
        return (float)(int16_t)get16(f + 16) / EDGE_TEMP_CENTI_PER_DEGREE;
    case EDGE_CH_HUMIDITY:
        return (float)get16(f + 18) / EDGE_HUMIDITY_CENTI_PER_PERCENT;
    case EDGE_CH_LIGHT:
        return (float)get16(f + 20) / EDGE_LIGHT_LUX_PER_UNIT;
    case EDGE_CH_SOIL:
        return (float)get16(f + 22) / EDGE_SOIL_DECI_PER_PERCENT;
    case EDGE_CH_COUNT:
        break;
    }
    return 0.0f;
}

size_t edge_protocol_encode_data(const edge_data_packet_t *packet, uint8_t *out,
                                 size_t capacity)
{
    if (packet == NULL || out == NULL || capacity < EDGE_FRAME_DATA_SIZE) {
        return 0u;
    }
    if (!channels_are_self_consistent(&packet->frame)) {
        return 0u;
    }

    int32_t temperature = 0;
    int32_t humidity = 0;
    int32_t light = 0;
    int32_t soil = 0;

    if (packet->frame.valid[EDGE_CH_TEMPERATURE] &&
        !encode_signed16(packet->frame.value[EDGE_CH_TEMPERATURE],
                         EDGE_TEMP_CENTI_PER_DEGREE, &temperature)) {
        return 0u;
    }
    if (packet->frame.valid[EDGE_CH_HUMIDITY] &&
        !encode_unsigned16(packet->frame.value[EDGE_CH_HUMIDITY],
                           EDGE_HUMIDITY_CENTI_PER_PERCENT, &humidity)) {
        return 0u;
    }
    if (packet->frame.valid[EDGE_CH_LIGHT] &&
        !encode_unsigned16(packet->frame.value[EDGE_CH_LIGHT], EDGE_LIGHT_LUX_PER_UNIT,
                           &light)) {
        return 0u;
    }
    if (packet->frame.valid[EDGE_CH_SOIL] &&
        !encode_unsigned16(packet->frame.value[EDGE_CH_SOIL], EDGE_SOIL_DECI_PER_PERCENT,
                           &soil)) {
        return 0u;
    }

    uint8_t valid_mask = 0u;
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        if (packet->frame.valid[i]) {
            valid_mask = (uint8_t)(valid_mask | EDGE_CH_MASK((edge_channel_t)i));
        }
    }

    begin_frame(out, (uint8_t)EDGE_FRAME_DATA, packet->node_id);
    put32(out + 5, packet->node_ts_ms);
    put16(out + 9, packet->session_id);
    put16(out + 11, packet->seq);
    out[13] = valid_mask;
    put16(out + 14, pack_statuses(&packet->frame));
    /* Rule W1: an invalid channel's field is written as zero, whatever the caller
     * left in the struct. There is no way to put a value for an unmeasured channel
     * on the wire, and therefore no way for a receiver to read one. */
    put16(out + 16, (uint16_t)(int16_t)temperature);
    put16(out + 18, (uint16_t)humidity);
    put16(out + 20, (uint16_t)light);
    put16(out + 22, (uint16_t)soil);
    out[24] = packet->node_trust;
    out[25] = packet->change_score;
    put16(out + 26, eg_crc16(out, 26));
    return EDGE_FRAME_DATA_SIZE;
}

bool edge_protocol_decode_data(const uint8_t *f, size_t length,
                               edge_data_packet_t *out)
{
    if (out == NULL || !frame_header_ok(f, length, EDGE_FRAME_DATA, EDGE_FRAME_DATA_SIZE, 26u)) {
        return false;
    }

    uint8_t mask = f[13];
    if ((mask & (uint8_t)~EDGE_CH_MASK_ALL) != 0u) {
        return false; /* a channel bit that does not correspond to a known channel */
    }

    uint16_t packed = get16(f + 14);
    edge_frame_begin(&out->frame, get32(f + 5));

    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        edge_channel_status_t status = unpack_status(packed, i);
        bool valid = (mask & EDGE_CH_MASK((edge_channel_t)i)) != 0u;

        if ((int)status >= (int)EDGE_CH_STATUS_COUNT) {
            return false; /* unknown status code */
        }
        if (valid != (status == EDGE_CH_OK)) {
            return false; /* rule W2 */
        }
        if (valid) {
            if (!edge_frame_set(&out->frame, (edge_channel_t)i, decode_channel_value(f, i))) {
                return false;
            }
        } else {
            /* The wire value is ignored on purpose: an invalid channel has no value. */
            if (!edge_frame_set_invalid(&out->frame, (edge_channel_t)i, status)) {
                return false;
            }
        }
    }

    out->node_id = f[4];
    out->node_ts_ms = get32(f + 5);
    out->session_id = get16(f + 9);
    out->seq = get16(f + 11);
    out->node_trust = f[24];
    out->change_score = f[25];
    return true;
}

size_t edge_protocol_encode_ack(const edge_ack_packet_t *packet, uint8_t *out,
                                size_t capacity)
{
    if (packet == NULL || out == NULL || capacity < EDGE_FRAME_ACK_SIZE) {
        return 0u;
    }
    begin_frame(out, (uint8_t)EDGE_FRAME_ACK, packet->node_id);
    put16(out + 5, packet->session_id);
    put16(out + 7, packet->seq);
    out[9] = packet->status;
    put16(out + 10, eg_crc16(out, 10));
    return EDGE_FRAME_ACK_SIZE;
}

bool edge_protocol_decode_ack(const uint8_t *f, size_t length, edge_ack_packet_t *out)
{
    if (out == NULL || !frame_header_ok(f, length, EDGE_FRAME_ACK, EDGE_FRAME_ACK_SIZE, 10u)) {
        return false;
    }
    out->node_id = f[4];
    out->session_id = get16(f + 5);
    out->seq = get16(f + 7);
    out->status = f[9];
    return true;
}

size_t edge_protocol_encode_heartbeat(const edge_heartbeat_packet_t *packet,
                                      uint8_t *out, size_t capacity)
{
    if (packet == NULL || out == NULL || capacity < EDGE_FRAME_HEARTBEAT_SIZE) {
        return 0u;
    }
    if ((int)packet->sensor_state >= (int)EDGE_SENSOR_STATE_COUNT) {
        return 0u;
    }
    begin_frame(out, (uint8_t)EDGE_FRAME_HEARTBEAT, packet->node_id);
    put32(out + 5, packet->node_ts_ms);
    out[9] = (uint8_t)packet->sensor_state;
    put16(out + 10, eg_crc16(out, 10));
    return EDGE_FRAME_HEARTBEAT_SIZE;
}

bool edge_protocol_decode_heartbeat(const uint8_t *f, size_t length,
                                    edge_heartbeat_packet_t *out)
{
    if (out == NULL ||
        !frame_header_ok(f, length, EDGE_FRAME_HEARTBEAT, EDGE_FRAME_HEARTBEAT_SIZE, 10u)) {
        return false;
    }
    if ((int)f[9] >= (int)EDGE_SENSOR_STATE_COUNT) {
        return false;
    }
    out->node_id = f[4];
    out->node_ts_ms = get32(f + 5);
    out->sensor_state = (edge_sensor_state_t)f[9];
    return true;
}

size_t edge_protocol_encode_beacon(const edge_beacon_packet_t *packet, uint8_t *out,
                                   size_t capacity)
{
    if (packet == NULL || out == NULL || capacity < EDGE_FRAME_BEACON_SIZE) {
        return 0u;
    }
    begin_frame(out, (uint8_t)EDGE_FRAME_BEACON, packet->node_id);
    put32(out + 5, packet->gateway_epoch_ms);
    put16(out + 9, packet->session_id);
    put16(out + 11, packet->window_ms);
    put16(out + 13, eg_crc16(out, 13));
    return EDGE_FRAME_BEACON_SIZE;
}

bool edge_protocol_decode_beacon(const uint8_t *f, size_t length,
                                 edge_beacon_packet_t *out)
{
    if (out == NULL ||
        !frame_header_ok(f, length, EDGE_FRAME_BEACON, EDGE_FRAME_BEACON_SIZE, 13u)) {
        return false;
    }
    out->node_id = f[4];
    out->gateway_epoch_ms = get32(f + 5);
    out->session_id = get16(f + 9);
    out->window_ms = get16(f + 11);
    return true;
}

edge_seq_relation_t edge_seq_relation(uint16_t previous, uint16_t current,
                                      uint16_t *missing)
{
    if (missing != NULL) {
        *missing = 0u;
    }
    if (current == previous) {
        return EDGE_SEQ_DUPLICATE;
    }
    uint16_t forward = (uint16_t)(current - previous);
    if (forward == 1u) {
        return EDGE_SEQ_NEXT;
    }
    if (forward <= (uint16_t)EDGE_SEQ_MAX_GAP) {
        if (missing != NULL) {
            *missing = (uint16_t)(forward - 1u);
        }
        return EDGE_SEQ_GAP;
    }
    /* Either a restart (a fresh node counts from 1 again) or a corrupted field.
     * The receiver decides; this unit only refuses to call it a normal gap. */
    return EDGE_SEQ_BACKWARD;
}

/* ---- stream parser ---------------------------------------------------------
 *
 * A sliding window rather than a multi-state machine. For a frame set with a
 * two-byte magic and fixed lengths, "keep trying to read a whole frame off the
 * front of the buffer; if the front cannot start a valid frame, discard one byte"
 * is both simpler to reason about and easy to prove terminating: every iteration
 * either consumes a complete checked frame or drops exactly one byte.
 */
void edge_protocol_parser_init(edge_protocol_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
}

void edge_protocol_parser_reset(edge_protocol_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    if (parser->length > 0u) {
        parser->truncated_discards++;
    }
    parser->length = 0u;
}

static void drop_first_byte(edge_protocol_parser_t *parser)
{
    if (parser->length == 0u) {
        return;
    }
    memmove(parser->buffer, parser->buffer + 1, parser->length - 1u);
    parser->length -= 1u;
    parser->resync_drops++;
}

size_t edge_protocol_parser_feed(edge_protocol_parser_t *parser, uint8_t byte,
                                 uint8_t *out, size_t capacity)
{
    if (parser == NULL || out == NULL || capacity < EDGE_MAX_FRAME_SIZE) {
        return 0u;
    }

    parser->bytes_in++;

    if (parser->length >= EDGE_MAX_FRAME_SIZE) {
        /* Defensive: the loop below always leaves room by dropping bytes, so this
         * only triggers if that reasoning is ever broken. Drop rather than
         * overflow the buffer. */
        drop_first_byte(parser);
    }
    parser->buffer[parser->length] = byte;
    parser->length += 1u;

    for (;;) {
        if (parser->length < 2u) {
            return 0u;
        }
        if (parser->buffer[0] != (uint8_t)EDGE_PROTOCOL_MAGIC_0 ||
            parser->buffer[1] != (uint8_t)EDGE_PROTOCOL_MAGIC_1) {
            drop_first_byte(parser);
            continue;
        }
        if (parser->length < 5u) {
            return 0u; /* need version and type before the length is known */
        }
        if (parser->buffer[2] != frame_version()) {
            parser->unknown_version_drops++;
            drop_first_byte(parser);
            continue;
        }

        size_t want = edge_protocol_frame_size((edge_frame_type_t)parser->buffer[3]);
        if (want == 0u) {
            parser->unknown_type_drops++;
            drop_first_byte(parser);
            continue;
        }
        if (parser->length < want) {
            return 0u; /* a frame may still complete; wait for more bytes */
        }

        if (get16(parser->buffer + want - 2u) != eg_crc16(parser->buffer, want - 2u)) {
            parser->crc_failures++;
            drop_first_byte(parser);
            continue;
        }

        memcpy(out, parser->buffer, want);
        parser->frames_emitted++;
        parser->length -= want;
        memmove(parser->buffer, parser->buffer + want, parser->length);
        return want;
    }
}
