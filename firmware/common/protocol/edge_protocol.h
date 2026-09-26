/*
 * edge_protocol.h — EdgeSense Protocol v1.
 *
 * What this file is responsible for: turning a sensor frame into bytes that can
 * survive a radio link, and turning those bytes back into a frame without ever
 * inventing a value. What it is not responsible for: deciding when to send, how
 * often, or what a reading means.
 *
 * Reuse and its limits
 * --------------------
 * The CRC-16 used here is NOT reimplemented: it is eg_crc16() from the vendored
 * EventGuard-LoRa transport, and the frame convention (magic, version, type,
 * node_id, CRC over the whole frame) follows the same proven layout.
 *
 * EventGuard's stream parser was NOT adopted verbatim. It derives a frame's length
 * from its own fixed two-frame scheme (one 26-byte DATA, one 13-byte ACK), so
 * reusing it would have forced EdgeSense's payload into that scheme. Its
 * resynchronising approach is followed instead, in edge_protocol_parser below.
 *
 * EventGuard's research semantics are deliberately absent: there is no
 * importance, no copy index and no copy count in any EdgeSense frame. Those
 * fields belong to its redundancy-budget question, not to this one.
 *
 * Layout (all multi-byte fields little-endian)
 * -------------------------------------------
 *   common header, 5 bytes:  'E' 'D' version type node_id
 *
 *   DATA, 28 bytes total
 *     5..8    node_ts_ms            u32   node monotonic clock, never wall time
 *     9..10   session_id            u16   pairs samples with one gateway session
 *     11..12  seq                   u16   node-local; dedup and gap detection only
 *     13      channel_valid_mask    u8    bit per edge_channel_t
 *     14..15  channel_status        u16   3 bits per channel, packed
 *     16..17  temperature_centi     i16
 *     18..19  humidity_centi        u16
 *     20..21  light_lux             u16
 *     22..23  soil_deci_percent     u16
 *     24      node_trust            u8    0..100
 *     25      change_score          u8    0..255, scaled
 *     26..27  crc16                 u16   over bytes 0..25
 *
 *   ACK, 12 bytes        session_id, seq, status, crc16
 *   HEARTBEAT, 12 bytes  node_ts_ms, sensor_state, crc16
 *   BEACON, 15 bytes     gateway_epoch_ms, session_id, window_ms, crc16
 *
 * Two invariants the wire format enforces
 * ---------------------------------------
 *   W1  A channel marked invalid carries the value 0 on the wire. There is no way
 *       to transmit a number for a channel that was not measured, so the
 *       frame-level validity defect of the old base cannot reappear on the link.
 *
 *   W2  The valid bit and the status field must agree: a channel is valid if and
 *       only if its status is EDGE_CH_OK. A frame that claims otherwise is
 *       rejected rather than repaired. This is rule R3 (see edge_channel.h)
 *       promoted from a programming rule to a wire invariant.
 *
 * A third rule concerns the encoder: if a *valid* channel's value cannot be
 * represented in its field, encoding fails and returns 0. Silently clamping or
 * truncating a measurement would put a number on the wire that no sensor
 * produced.
 *
 * Portable C11, no allocation, no ESP-IDF.
 */
#ifndef EDGE_PROTOCOL_H
#define EDGE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge_sensor_state.h" /* brings edge_channel.h with it */

#ifdef __cplusplus
extern "C" {
#endif

#define EDGE_PROTOCOL_MAGIC_0 'E'
#define EDGE_PROTOCOL_MAGIC_1 'D'

#define EDGE_FRAME_DATA_SIZE 28u
#define EDGE_FRAME_ACK_SIZE 12u
#define EDGE_FRAME_HEARTBEAT_SIZE 12u
#define EDGE_FRAME_BEACON_SIZE 15u
#define EDGE_MAX_FRAME_SIZE EDGE_FRAME_DATA_SIZE

typedef enum {
    EDGE_FRAME_DATA = 1,
    EDGE_FRAME_ACK = 2,
    EDGE_FRAME_HEARTBEAT = 3,
    EDGE_FRAME_BEACON = 4
} edge_frame_type_t;

/* Wire scales. Fixed points on purpose: no float formatting on the link. */
#define EDGE_TEMP_CENTI_PER_DEGREE 100.0f
#define EDGE_HUMIDITY_CENTI_PER_PERCENT 100.0f
#define EDGE_LIGHT_LUX_PER_UNIT 1.0f
#define EDGE_SOIL_DECI_PER_PERCENT 10.0f

typedef struct {
    uint8_t             node_id;
    uint32_t            node_ts_ms;
    uint16_t            session_id;
    uint16_t            seq;
    edge_sensor_frame_t frame;
    uint8_t             node_trust;   /* 0..100 */
    uint8_t             change_score; /* 0..255 */
} edge_data_packet_t;

typedef struct {
    uint8_t  node_id;
    uint16_t session_id;
    uint16_t seq;
    uint8_t  status;
} edge_ack_packet_t;

typedef struct {
    uint8_t              node_id;
    uint32_t             node_ts_ms;
    edge_sensor_state_t  sensor_state;
} edge_heartbeat_packet_t;

typedef struct {
    uint8_t  node_id; /* the gateway */
    uint32_t gateway_epoch_ms;
    uint16_t session_id;
    uint16_t window_ms;
} edge_beacon_packet_t;

/* 0 for an unknown type. */
size_t edge_protocol_frame_size(edge_frame_type_t type);
const char *edge_frame_type_name(edge_frame_type_t type);
int edge_protocol_version(void);

/* All encoders return the number of bytes written, or 0 on any refusal. */
size_t edge_protocol_encode_data(const edge_data_packet_t *packet, uint8_t *out,
                                 size_t capacity);
size_t edge_protocol_encode_ack(const edge_ack_packet_t *packet, uint8_t *out,
                                size_t capacity);
size_t edge_protocol_encode_heartbeat(const edge_heartbeat_packet_t *packet,
                                      uint8_t *out, size_t capacity);
size_t edge_protocol_encode_beacon(const edge_beacon_packet_t *packet,
                                   uint8_t *out, size_t capacity);

/* All decoders require an exact length and a valid CRC. */
bool edge_protocol_decode_data(const uint8_t *frame, size_t length,
                               edge_data_packet_t *out);
bool edge_protocol_decode_ack(const uint8_t *frame, size_t length,
                              edge_ack_packet_t *out);
bool edge_protocol_decode_heartbeat(const uint8_t *frame, size_t length,
                                    edge_heartbeat_packet_t *out);
bool edge_protocol_decode_beacon(const uint8_t *frame, size_t length,
                                 edge_beacon_packet_t *out);

/*
 * Sequence helpers. The sequence number is node-local: it exists so the receiver
 * can tell a gap or a repeat from one node, and for nothing else. It is
 * explicitly NOT a cross-node pairing key (see the frozen design's pairing rules:
 * a node's sequence number says nothing about another node's timeline).
 *
 * EDGE_SEQ_MAX_GAP separates a plausible loss burst from a sequence that has
 * jumped far enough to indicate a sender restart or a corrupted field. The caller
 * decides what to do with BACKWARD; this unit only classifies.
 */
#define EDGE_SEQ_MAX_GAP 1000u

typedef enum {
    EDGE_SEQ_NEXT = 0,  /* exactly the next expected value            */
    EDGE_SEQ_DUPLICATE, /* same value as before                       */
    EDGE_SEQ_GAP,       /* forward jump; *missing reports how many    */
    EDGE_SEQ_BACKWARD   /* not forward and not a small jump           */
} edge_seq_relation_t;

edge_seq_relation_t edge_seq_relation(uint16_t previous, uint16_t current,
                                      uint16_t *missing);

/*
 * Stream parser.
 *
 * Feed it bytes as they arrive from the radio; it hands back complete,
 * CRC-checked frames. It tolerates garbage, truncated frames, back-to-back
 * frames, unknown versions and unknown types without losing the stream, and it
 * never drops a frame for having a sequence number it has seen before —
 * deduplication belongs to the receiver's logic, not here.
 */
typedef struct {
    uint8_t  buffer[EDGE_MAX_FRAME_SIZE];
    size_t   length;
    uint64_t bytes_in;
    uint32_t frames_emitted;
    uint32_t crc_failures;
    uint32_t unknown_version_drops;
    uint32_t unknown_type_drops;
    uint32_t resync_drops;      /* bytes discarded while hunting for a frame start */
    uint32_t truncated_discards;/* partial frame dropped by a reset */
} edge_protocol_parser_t;

void edge_protocol_parser_init(edge_protocol_parser_t *parser);

/*
 * Feed one byte. Returns the length of a decoded frame (and copies it into `out`)
 * when a frame completes, otherwise 0. `out` must have room for
 * EDGE_MAX_FRAME_SIZE bytes.
 */
size_t edge_protocol_parser_feed(edge_protocol_parser_t *parser, uint8_t byte,
                                 uint8_t *out, size_t capacity);

/* Discard any partial frame, counting it. Call on link reset or session change. */
void edge_protocol_parser_reset(edge_protocol_parser_t *parser);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_PROTOCOL_H */
