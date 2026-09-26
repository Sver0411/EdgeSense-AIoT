/*
 * test_edge_protocol.c — host tests for EdgeSense Protocol v1.
 *
 * Covers the cases the frozen design requires of a parser and a codec: roundtrip,
 * CRC validity, truncation, oversize, garbage, back-to-back frames, duplicates,
 * gaps, unknown version and unknown type. Plus the two wire invariants:
 *
 *   W1  an invalid channel cannot carry a value across the link, in either direction
 *   W2  the valid bit and the status field must agree, or the frame is rejected
 *
 * Run: see scripts/test_host.sh
 */
#include <stdio.h>
#include <string.h>

#include "edge_protocol.h"
#include "harness.h"
#include "protocol.h" /* vendored EventGuard-LoRa: eg_crc16, used to craft valid frames */

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* Recompute the stored CRC after deliberately corrupting a frame. */
static void reseal(uint8_t *frame, size_t crc_offset)
{
    put16(frame + crc_offset, eg_crc16(frame, crc_offset));
}

static void build_full_data_frame(edge_data_packet_t *packet, uint16_t seq)
{
    memset(packet, 0, sizeof(*packet));
    packet->node_id = 1;
    packet->node_ts_ms = 123456u;
    packet->session_id = 7u;
    packet->seq = seq;
    packet->node_trust = 92u;
    packet->change_score = 17u;
    edge_frame_begin(&packet->frame, packet->node_ts_ms);
    (void)edge_frame_set(&packet->frame, EDGE_CH_TEMPERATURE, 24.37f);
    (void)edge_frame_set(&packet->frame, EDGE_CH_HUMIDITY, 55.42f);
    (void)edge_frame_set(&packet->frame, EDGE_CH_LIGHT, 310.0f);
    (void)edge_frame_set(&packet->frame, EDGE_CH_SOIL, 41.5f);
}

static void test_frame_sizes_and_names(void)
{
    CHECK_UINT_EQ(edge_protocol_frame_size(EDGE_FRAME_DATA), EDGE_FRAME_DATA_SIZE);
    CHECK_UINT_EQ(edge_protocol_frame_size(EDGE_FRAME_ACK), EDGE_FRAME_ACK_SIZE);
    CHECK_UINT_EQ(edge_protocol_frame_size(EDGE_FRAME_HEARTBEAT), EDGE_FRAME_HEARTBEAT_SIZE);
    CHECK_UINT_EQ(edge_protocol_frame_size(EDGE_FRAME_BEACON), EDGE_FRAME_BEACON_SIZE);
    CHECK_UINT_EQ(edge_protocol_frame_size((edge_frame_type_t)99), 0u);

    CHECK_STR_EQ(edge_frame_type_name(EDGE_FRAME_DATA), "data");
    CHECK_STR_EQ(edge_frame_type_name(EDGE_FRAME_ACK), "ack");
    CHECK_STR_EQ(edge_frame_type_name(EDGE_FRAME_HEARTBEAT), "heartbeat");
    CHECK_STR_EQ(edge_frame_type_name(EDGE_FRAME_BEACON), "beacon");
    CHECK_STR_EQ(edge_frame_type_name((edge_frame_type_t)99), "unknown");
    CHECK_INT_EQ(edge_protocol_version(), 1);
}

static void test_data_roundtrip_full(void)
{
    edge_data_packet_t packet;
    edge_data_packet_t decoded;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    build_full_data_frame(&packet, 42u);

    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));
    CHECK_UINT_EQ(n, EDGE_FRAME_DATA_SIZE);
    /* EdgeSense's own magic, so an EventGuard parser can never mistake this frame
     * for one of its own. */
    CHECK_INT_EQ(frame[0], 'E');
    CHECK_INT_EQ(frame[1], 'D');
    CHECK_INT_EQ(frame[2], edge_protocol_version());
    CHECK_INT_EQ(frame[3], EDGE_FRAME_DATA);

    CHECK(edge_protocol_decode_data(frame, n, &decoded));
    CHECK_UINT_EQ(decoded.node_id, 1u);
    CHECK_UINT_EQ(decoded.session_id, 7u);
    CHECK_UINT_EQ(decoded.seq, 42u);
    CHECK_UINT_EQ(decoded.node_ts_ms, 123456u);
    CHECK_UINT_EQ(decoded.node_trust, 92u);
    CHECK_UINT_EQ(decoded.change_score, 17u);
    CHECK_NEAR(edge_frame_value_or(&decoded.frame, EDGE_CH_TEMPERATURE, -1.0f), 24.37, 0.005);
    CHECK_NEAR(edge_frame_value_or(&decoded.frame, EDGE_CH_HUMIDITY, -1.0f), 55.42, 0.005);
    CHECK_NEAR(edge_frame_value_or(&decoded.frame, EDGE_CH_LIGHT, -1.0f), 310.0, 0.005);
    CHECK_NEAR(edge_frame_value_or(&decoded.frame, EDGE_CH_SOIL, -1.0f), 41.5, 0.005);
}

static void test_data_roundtrip_preserves_per_channel_validity_and_reason(void)
{
    edge_data_packet_t packet;
    edge_data_packet_t decoded;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    build_full_data_frame(&packet, 1u);

    /* The case a frame-level flag could not express. */
    (void)edge_frame_set_invalid(&packet.frame, EDGE_CH_LIGHT, EDGE_CH_ABSENT);
    (void)edge_frame_set_invalid(&packet.frame, EDGE_CH_SOIL, EDGE_CH_NOT_SAMPLED);

    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));
    CHECK_UINT_EQ(n, EDGE_FRAME_DATA_SIZE);
    CHECK(edge_protocol_decode_data(frame, n, &decoded));

    CHECK(edge_frame_channel_ok(&decoded.frame, EDGE_CH_TEMPERATURE));
    CHECK(edge_frame_channel_ok(&decoded.frame, EDGE_CH_HUMIDITY));
    CHECK(!edge_frame_channel_ok(&decoded.frame, EDGE_CH_LIGHT));
    CHECK(!edge_frame_channel_ok(&decoded.frame, EDGE_CH_SOIL));
    /* The reason survives the link, so a diagnosis can say "absent", not "odd". */
    CHECK_INT_EQ(decoded.frame.status[EDGE_CH_LIGHT], EDGE_CH_ABSENT);
    CHECK_INT_EQ(decoded.frame.status[EDGE_CH_SOIL], EDGE_CH_NOT_SAMPLED);
    CHECK_NEAR(edge_frame_value_or(&decoded.frame, EDGE_CH_LIGHT, -1.0f), -1.0, 0.0);
}

static void test_w1_no_value_for_an_invalid_channel_reaches_the_wire(void)
{
    edge_data_packet_t packet;
    edge_data_packet_t decoded;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    build_full_data_frame(&packet, 5u);

    /* Simulate a caller that wrote a value straight into the array while leaving
     * the channel invalid. The encoder must not put that number on the wire. */
    packet.frame.valid[EDGE_CH_LIGHT] = false;
    packet.frame.status[EDGE_CH_LIGHT] = EDGE_CH_TIMEOUT;
    packet.frame.value[EDGE_CH_LIGHT] = 9999.0f;

    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));
    CHECK_UINT_EQ(n, EDGE_FRAME_DATA_SIZE);
    /* The light field is at offset 20..21 and must be zero. */
    CHECK_UINT_EQ(frame[20], 0u);
    CHECK_UINT_EQ(frame[21], 0u);

    CHECK(edge_protocol_decode_data(frame, n, &decoded));
    CHECK(!edge_frame_channel_ok(&decoded.frame, EDGE_CH_LIGHT));
    CHECK_NEAR(decoded.frame.value[EDGE_CH_LIGHT], 0.0, 0.0);
}

static void test_w1_decoder_ignores_a_value_for_an_invalid_channel(void)
{
    edge_data_packet_t packet;
    edge_data_packet_t decoded;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    build_full_data_frame(&packet, 6u);

    /* Drop light from the valid mask, keep its status non-OK, but leave a value
     * in the field — exactly what a buggy or hostile sender would do. */
    (void)edge_frame_set_invalid(&packet.frame, EDGE_CH_LIGHT, EDGE_CH_TIMEOUT);
    packet.frame.value[EDGE_CH_LIGHT] = 900.0f;
    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));
    CHECK_UINT_EQ(n, EDGE_FRAME_DATA_SIZE);

    /* Rewrite the light field directly and reseal, so the CRC is valid. */
    frame[20] = (uint8_t)(900 & 0xFF);
    frame[21] = (uint8_t)(900 >> 8);
    reseal(frame, 26);

    CHECK(edge_protocol_decode_data(frame, n, &decoded));
    CHECK(!edge_frame_channel_ok(&decoded.frame, EDGE_CH_LIGHT));
    CHECK_NEAR(decoded.frame.value[EDGE_CH_LIGHT], 0.0, 0.0);
    CHECK_NEAR(edge_frame_value_or(&decoded.frame, EDGE_CH_LIGHT, -1.0f), -1.0, 0.0);
}

static void test_w2_encoder_refuses_valid_and_ok_disagreement(void)
{
    edge_data_packet_t packet;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    build_full_data_frame(&packet, 7u);

    /* valid bit cleared but the status still says OK */
    packet.frame.valid[EDGE_CH_HUMIDITY] = false;
    CHECK_UINT_EQ(edge_protocol_encode_data(&packet, frame, sizeof(frame)), 0u);

    build_full_data_frame(&packet, 7u);
    /* valid bit set but the status is a failure code */
    packet.frame.status[EDGE_CH_HUMIDITY] = EDGE_CH_TIMEOUT;
    CHECK_UINT_EQ(edge_protocol_encode_data(&packet, frame, sizeof(frame)), 0u);
}

static void test_w2_decoder_rejects_valid_and_ok_disagreement(void)
{
    edge_data_packet_t packet;
    edge_data_packet_t decoded;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    build_full_data_frame(&packet, 8u);

    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));
    CHECK_UINT_EQ(n, EDGE_FRAME_DATA_SIZE);

    /* Set humidity's status (channel 1, bits 3..5 of the packed word) to a failure
     * code while leaving its valid bit set. Resealed, so only the invariant can
     * reject it. */
    uint16_t packed = (uint16_t)((uint16_t)frame[14] | ((uint16_t)frame[15] << 8));
    packed = (uint16_t)((packed & ~(uint16_t)(0x7u << 3)) | (uint16_t)((uint16_t)EDGE_CH_TIMEOUT << 3));
    frame[14] = (uint8_t)(packed & 0xFF);
    frame[15] = (uint8_t)(packed >> 8);
    reseal(frame, 26);

    CHECK(!edge_protocol_decode_data(frame, n, &decoded));

    /* Now the opposite: clear the valid bit but leave the status OK. */
    build_full_data_frame(&packet, 8u);
    n = edge_protocol_encode_data(&packet, frame, sizeof(frame));
    frame[13] = (uint8_t)(frame[13] & ~EDGE_CH_MASK(EDGE_CH_HUMIDITY));
    reseal(frame, 26);
    CHECK(!edge_protocol_decode_data(frame, n, &decoded));
}

static void test_encoder_refuses_unrepresentable_or_non_finite_values(void)
{
    edge_data_packet_t packet;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];

    /* A valid temperature of 400 C does not fit in a signed centi-degree field.
     * Clamping it would put a number on the wire that no sensor produced. */
    build_full_data_frame(&packet, 9u);
    packet.frame.value[EDGE_CH_TEMPERATURE] = 400.0f;
    CHECK_UINT_EQ(edge_protocol_encode_data(&packet, frame, sizeof(frame)), 0u);

    /* Negative humidity cannot be represented either. */
    build_full_data_frame(&packet, 9u);
    packet.frame.value[EDGE_CH_HUMIDITY] = -1.0f;
    CHECK_UINT_EQ(edge_protocol_encode_data(&packet, frame, sizeof(frame)), 0u);

    /* Light beyond 65535 lux. */
    build_full_data_frame(&packet, 9u);
    packet.frame.value[EDGE_CH_LIGHT] = 70000.0f;
    CHECK_UINT_EQ(edge_protocol_encode_data(&packet, frame, sizeof(frame)), 0u);

    /* A negative temperature IS representable and must be accepted. */
    build_full_data_frame(&packet, 9u);
    packet.frame.value[EDGE_CH_TEMPERATURE] = -12.34f;
    CHECK_UINT_EQ(edge_protocol_encode_data(&packet, frame, sizeof(frame)),
                  EDGE_FRAME_DATA_SIZE);
}

static void test_encode_capacity_and_null_guards(void)
{
    edge_data_packet_t packet;
    uint8_t frame[64];
    build_full_data_frame(&packet, 10u);

    CHECK_UINT_EQ(edge_protocol_encode_data(&packet, frame, EDGE_FRAME_DATA_SIZE - 1u), 0u);
    CHECK_UINT_EQ(edge_protocol_encode_data(NULL, frame, sizeof(frame)), 0u);
    CHECK_UINT_EQ(edge_protocol_encode_data(&packet, NULL, sizeof(frame)), 0u);
}

static void test_decode_rejects_malformed_frames(void)
{
    edge_data_packet_t packet;
    edge_data_packet_t decoded;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    uint8_t copy[EDGE_MAX_FRAME_SIZE];
    build_full_data_frame(&packet, 11u);
    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));
    CHECK_UINT_EQ(n, EDGE_FRAME_DATA_SIZE);

    /* exact length is required */
    CHECK(!edge_protocol_decode_data(frame, n - 1u, &decoded));
    CHECK(!edge_protocol_decode_data(frame, n + 1u, &decoded));

    memcpy(copy, frame, n);
    copy[0] = 'X';
    CHECK(!edge_protocol_decode_data(copy, n, &decoded));

    memcpy(copy, frame, n);
    copy[2] = (uint8_t)(edge_protocol_version() + 1);
    CHECK(!edge_protocol_decode_data(copy, n, &decoded));

    memcpy(copy, frame, n);
    copy[3] = (uint8_t)EDGE_FRAME_ACK;
    CHECK(!edge_protocol_decode_data(copy, n, &decoded));

    memcpy(copy, frame, n);
    copy[24] ^= 0xFFu; /* one payload byte, CRC now stale */
    CHECK(!edge_protocol_decode_data(copy, n, &decoded));

    /* A channel bit that maps to no known channel. */
    memcpy(copy, frame, n);
    copy[13] = (uint8_t)(copy[13] | 0x80u);
    reseal(copy, 26);
    CHECK(!edge_protocol_decode_data(copy, n, &decoded));

    CHECK(!edge_protocol_decode_data(NULL, n, &decoded));
    CHECK(!edge_protocol_decode_data(frame, n, NULL));
}

static void test_ack_roundtrip_and_guards(void)
{
    edge_ack_packet_t ack;
    edge_ack_packet_t decoded;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    memset(&ack, 0, sizeof(ack));
    ack.node_id = 0u;
    ack.session_id = 3u;
    ack.seq = 77u;
    ack.status = 1u;

    size_t n = edge_protocol_encode_ack(&ack, frame, sizeof(frame));
    CHECK_UINT_EQ(n, EDGE_FRAME_ACK_SIZE);
    CHECK(edge_protocol_decode_ack(frame, n, &decoded));
    CHECK_UINT_EQ(decoded.session_id, 3u);
    CHECK_UINT_EQ(decoded.seq, 77u);
    CHECK_UINT_EQ(decoded.status, 1u);

    CHECK(!edge_protocol_decode_ack(frame, n - 1u, &decoded));
    /* An ACK frame must not decode as DATA and vice versa. */
    edge_data_packet_t data;
    CHECK(!edge_protocol_decode_data(frame, n, &data));
}

static void test_heartbeat_roundtrip_and_state_validation(void)
{
    edge_heartbeat_packet_t beat;
    edge_heartbeat_packet_t decoded;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    memset(&beat, 0, sizeof(beat));
    beat.node_id = 2u;
    beat.node_ts_ms = 987654u;
    beat.sensor_state = EDGE_SENSOR_STATE_DEGRADED;

    size_t n = edge_protocol_encode_heartbeat(&beat, frame, sizeof(frame));
    CHECK_UINT_EQ(n, EDGE_FRAME_HEARTBEAT_SIZE);
    CHECK(edge_protocol_decode_heartbeat(frame, n, &decoded));
    CHECK_UINT_EQ(decoded.node_ts_ms, 987654u);
    CHECK_INT_EQ(decoded.sensor_state, EDGE_SENSOR_STATE_DEGRADED);

    /* An out-of-range state must not be encoded, and must not be accepted. */
    beat.sensor_state = (edge_sensor_state_t)EDGE_SENSOR_STATE_COUNT;
    CHECK_UINT_EQ(edge_protocol_encode_heartbeat(&beat, frame, sizeof(frame)), 0u);

    beat.sensor_state = EDGE_SENSOR_STATE_HEALTHY;
    n = edge_protocol_encode_heartbeat(&beat, frame, sizeof(frame));
    frame[9] = 99u;
    reseal(frame, 10);
    CHECK(!edge_protocol_decode_heartbeat(frame, n, &decoded));
}

static void test_beacon_roundtrip(void)
{
    edge_beacon_packet_t beacon;
    edge_beacon_packet_t decoded;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    memset(&beacon, 0, sizeof(beacon));
    beacon.node_id = 0u;
    beacon.gateway_epoch_ms = 5000000u;
    beacon.session_id = 12u;
    beacon.window_ms = 30000u;

    size_t n = edge_protocol_encode_beacon(&beacon, frame, sizeof(frame));
    CHECK_UINT_EQ(n, EDGE_FRAME_BEACON_SIZE);
    CHECK(edge_protocol_decode_beacon(frame, n, &decoded));
    CHECK_UINT_EQ(decoded.gateway_epoch_ms, 5000000u);
    CHECK_UINT_EQ(decoded.session_id, 12u);
    CHECK_UINT_EQ(decoded.window_ms, 30000u);
}

static void test_sequence_relation(void)
{
    uint16_t missing = 0u;
    CHECK_INT_EQ(edge_seq_relation(10u, 11u, &missing), EDGE_SEQ_NEXT);
    CHECK_UINT_EQ(missing, 0u);
    CHECK_INT_EQ(edge_seq_relation(10u, 10u, &missing), EDGE_SEQ_DUPLICATE);

    CHECK_INT_EQ(edge_seq_relation(10u, 15u, &missing), EDGE_SEQ_GAP);
    CHECK_UINT_EQ(missing, 4u);

    CHECK_INT_EQ(edge_seq_relation(10u, 12u, &missing), EDGE_SEQ_GAP);
    CHECK_UINT_EQ(missing, 1u);

    /* A node that restarted counts from 1 again; that is not a 65000-packet gap. */
    CHECK_INT_EQ(edge_seq_relation(631u, 1u, &missing), EDGE_SEQ_BACKWARD);
    CHECK_UINT_EQ(missing, 0u);

    /* Wraparound is still a normal step. */
    CHECK_INT_EQ(edge_seq_relation(65535u, 0u, &missing), EDGE_SEQ_NEXT);

    /* A null out-parameter must be tolerated. */
    CHECK_INT_EQ(edge_seq_relation(10u, 15u, NULL), EDGE_SEQ_GAP);
}

/* ---- stream parser --------------------------------------------------------- */

static size_t feed_frame(edge_protocol_parser_t *parser, const uint8_t *frame,
                         size_t length, uint8_t *out, uint32_t *emitted)
{
    size_t last = 0u;
    for (size_t i = 0; i < length; ++i) {
        size_t n = edge_protocol_parser_feed(parser, frame[i], out, EDGE_MAX_FRAME_SIZE);
        if (n != 0u) {
            last = n;
            if (emitted != NULL) {
                *emitted += 1u;
            }
        }
    }
    return last;
}

static void test_parser_extracts_a_frame_after_garbage(void)
{
    edge_data_packet_t packet;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_t parser;
    build_full_data_frame(&packet, 100u);
    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));

    edge_protocol_parser_init(&parser);
    const uint8_t garbage[] = {0x00u, 0xFFu, 'E', 0x00u, 'G', 'G', 'E', 'D', 0x99u};

    for (size_t i = 0; i < sizeof(garbage); ++i) {
        CHECK_UINT_EQ(edge_protocol_parser_feed(&parser, garbage[i], out, sizeof(out)), 0u);
    }
    uint32_t emitted = 0u;
    size_t got = feed_frame(&parser, frame, n, out, &emitted);
    CHECK_UINT_EQ(got, EDGE_FRAME_DATA_SIZE);
    CHECK_UINT_EQ(emitted, 1u);
    CHECK(parser.resync_drops > 0u);

    edge_data_packet_t decoded;
    CHECK(edge_protocol_decode_data(out, got, &decoded));
    CHECK_UINT_EQ(decoded.seq, 100u);
}

static void test_parser_handles_back_to_back_frames(void)
{
    edge_data_packet_t packet;
    uint8_t f1[EDGE_MAX_FRAME_SIZE];
    uint8_t f2[EDGE_MAX_FRAME_SIZE];
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_t parser;

    build_full_data_frame(&packet, 200u);
    size_t n1 = edge_protocol_encode_data(&packet, f1, sizeof(f1));
    build_full_data_frame(&packet, 201u);
    size_t n2 = edge_protocol_encode_data(&packet, f2, sizeof(f2));

    edge_protocol_parser_init(&parser);
    uint32_t emitted = 0u;
    (void)feed_frame(&parser, f1, n1, out, &emitted);
    (void)feed_frame(&parser, f2, n2, out, &emitted);

    CHECK_UINT_EQ(emitted, 2u);
    CHECK_UINT_EQ(parser.frames_emitted, 2u);
    CHECK_UINT_EQ(parser.crc_failures, 0u);
}

static void test_parser_holds_a_partial_frame_until_it_completes(void)
{
    edge_data_packet_t packet;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_t parser;
    build_full_data_frame(&packet, 300u);
    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));

    edge_protocol_parser_init(&parser);
    for (size_t i = 0; i + 1u < n; ++i) {
        CHECK_UINT_EQ(edge_protocol_parser_feed(&parser, frame[i], out, sizeof(out)), 0u);
    }
    size_t got = edge_protocol_parser_feed(&parser, frame[n - 1u], out, sizeof(out));
    CHECK_UINT_EQ(got, EDGE_FRAME_DATA_SIZE);
}

static void test_parser_resynchronises_after_a_truncated_frame(void)
{
    edge_data_packet_t packet;
    uint8_t full[EDGE_MAX_FRAME_SIZE];
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_t parser;
    build_full_data_frame(&packet, 400u);
    size_t n = edge_protocol_encode_data(&packet, full, sizeof(full));

    edge_protocol_parser_init(&parser);
    /* Half a frame, then a complete good one. A parser that trusted the header
     * length without re-checking the CRC would emit a corrupt frame here. */
    uint32_t emitted = 0u;
    (void)feed_frame(&parser, full, n / 2u, out, &emitted);
    CHECK_UINT_EQ(emitted, 0u);

    size_t got = feed_frame(&parser, full, n, out, &emitted);
    CHECK_UINT_EQ(got, EDGE_FRAME_DATA_SIZE);
    CHECK_UINT_EQ(emitted, 1u);

    edge_data_packet_t decoded;
    CHECK(edge_protocol_decode_data(out, got, &decoded));
    CHECK_UINT_EQ(decoded.seq, 400u);
}

static void test_parser_counts_unknown_version_and_type(void)
{
    edge_data_packet_t packet;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_t parser;
    build_full_data_frame(&packet, 500u);
    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));

    edge_protocol_parser_init(&parser);
    uint8_t bad_version[EDGE_MAX_FRAME_SIZE];
    memcpy(bad_version, frame, n);
    bad_version[2] = (uint8_t)(edge_protocol_version() + 3);
    uint32_t emitted = 0u;
    (void)feed_frame(&parser, bad_version, n, out, &emitted);
    CHECK_UINT_EQ(emitted, 0u);
    CHECK(parser.unknown_version_drops > 0u);

    edge_protocol_parser_init(&parser);
    uint8_t bad_type[EDGE_MAX_FRAME_SIZE];
    memcpy(bad_type, frame, n);
    bad_type[3] = 200u;
    emitted = 0u;
    (void)feed_frame(&parser, bad_type, n, out, &emitted);
    CHECK_UINT_EQ(emitted, 0u);
    CHECK(parser.unknown_type_drops > 0u);
}

static void test_parser_does_not_drop_a_duplicate_sequence(void)
{
    edge_data_packet_t packet;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_t parser;
    build_full_data_frame(&packet, 600u);
    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));

    /* Deduplication belongs to the receiver's logic, which can tell a radio
     * duplicate from a retransmission. A parser that silently swallowed the
     * second copy would hide that decision from it. */
    edge_protocol_parser_init(&parser);
    uint32_t emitted = 0u;
    (void)feed_frame(&parser, frame, n, out, &emitted);
    (void)feed_frame(&parser, frame, n, out, &emitted);
    CHECK_UINT_EQ(emitted, 2u);
}

static void test_parser_counts_crc_failures_and_recovers(void)
{
    edge_data_packet_t packet;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_t parser;
    build_full_data_frame(&packet, 700u);
    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));

    uint8_t corrupt[EDGE_MAX_FRAME_SIZE];
    memcpy(corrupt, frame, n);
    corrupt[20] ^= 0xFFu;

    edge_protocol_parser_init(&parser);
    uint32_t emitted = 0u;
    (void)feed_frame(&parser, corrupt, n, out, &emitted);
    (void)feed_frame(&parser, frame, n, out, &emitted);

    CHECK(parser.crc_failures > 0u);
    CHECK_UINT_EQ(emitted, 1u); /* the good frame still got through */
}

static void test_parser_reset_accounts_for_a_partial_frame(void)
{
    edge_protocol_parser_t parser;
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_init(&parser);
    CHECK_UINT_EQ(edge_protocol_parser_feed(&parser, 'E', out, sizeof(out)), 0u);
    CHECK_UINT_EQ(edge_protocol_parser_feed(&parser, 'D', out, sizeof(out)), 0u);
    CHECK_UINT_EQ(parser.truncated_discards, 0u);
    edge_protocol_parser_reset(&parser);
    CHECK_UINT_EQ(parser.truncated_discards, 1u);
    CHECK_UINT_EQ(parser.length, 0u);
}

static void test_parser_never_crashes_on_garbage_and_still_recovers(void)
{
    edge_protocol_parser_t parser;
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_init(&parser);

    /* Deterministic pseudo-random byte stream. */
    uint32_t state = 0x12345678u;
    for (int i = 0; i < 4096; ++i) {
        state = state * 1103515245u + 12345u;
        uint8_t byte = (uint8_t)((state >> 16) & 0xFFu);
        (void)edge_protocol_parser_feed(&parser, byte, out, sizeof(out));
    }
    CHECK_UINT_EQ(parser.bytes_in, 4096u);

    /* The buffer must never have overrun its fixed size. */
    CHECK(parser.length <= EDGE_MAX_FRAME_SIZE);

    /* And a real frame fed afterwards must still come out intact. */
    edge_data_packet_t packet;
    uint8_t frame[EDGE_MAX_FRAME_SIZE];
    build_full_data_frame(&packet, 800u);
    size_t n = edge_protocol_encode_data(&packet, frame, sizeof(frame));
    uint32_t emitted = 0u;
    size_t got = feed_frame(&parser, frame, n, out, &emitted);
    CHECK_UINT_EQ(got, EDGE_FRAME_DATA_SIZE);
    CHECK_UINT_EQ(emitted, 1u);
}

static void test_parser_guards_and_null(void)
{
    edge_protocol_parser_t parser;
    uint8_t out[EDGE_MAX_FRAME_SIZE];
    edge_protocol_parser_init(&parser);

    CHECK_UINT_EQ(edge_protocol_parser_feed(NULL, 'E', out, sizeof(out)), 0u);
    CHECK_UINT_EQ(edge_protocol_parser_feed(&parser, 'E', NULL, sizeof(out)), 0u);
    /* A caller-supplied buffer that cannot hold the largest frame is refused
     * rather than written past. */
    CHECK_UINT_EQ(edge_protocol_parser_feed(&parser, 'E', out, 4u), 0u);

    edge_protocol_parser_init(NULL); /* must not crash */
    edge_protocol_parser_reset(NULL);
    CHECK(true);
}

int main(void)
{
    (void)printf("test_edge_protocol\n");
    test_frame_sizes_and_names();
    test_data_roundtrip_full();
    test_data_roundtrip_preserves_per_channel_validity_and_reason();
    test_w1_no_value_for_an_invalid_channel_reaches_the_wire();
    test_w1_decoder_ignores_a_value_for_an_invalid_channel();
    test_w2_encoder_refuses_valid_and_ok_disagreement();
    test_w2_decoder_rejects_valid_and_ok_disagreement();
    test_encoder_refuses_unrepresentable_or_non_finite_values();
    test_encode_capacity_and_null_guards();
    test_decode_rejects_malformed_frames();
    test_ack_roundtrip_and_guards();
    test_heartbeat_roundtrip_and_state_validation();
    test_beacon_roundtrip();
    test_sequence_relation();
    test_parser_extracts_a_frame_after_garbage();
    test_parser_handles_back_to_back_frames();
    test_parser_holds_a_partial_frame_until_it_completes();
    test_parser_resynchronises_after_a_truncated_frame();
    test_parser_counts_unknown_version_and_type();
    test_parser_does_not_drop_a_duplicate_sequence();
    test_parser_counts_crc_failures_and_recovers();
    test_parser_reset_accounts_for_a_partial_frame();
    test_parser_never_crashes_on_garbage_and_still_recovers();
    test_parser_guards_and_null();
    return HARNESS_REPORT("edge_protocol");
}
