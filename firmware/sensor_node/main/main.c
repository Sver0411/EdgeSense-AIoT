/*
 * main.c — EdgeSense sensor node entry point.
 *
 * Scope of this file, deliberately small: bring up the sensing path, take a few
 * cycles, print what each channel actually returned and why, and prove the frame
 * survives a trip through the protocol codec. It does not sample adaptively, does
 * not talk to the radio, and does not compute a trust score.
 *
 * Status of what runs here
 * ------------------------
 *   per-channel validity        implemented, host-tested
 *   sensor availability state   implemented, host-tested
 *   protocol v1 encode/decode   implemented, host-tested
 *   I2C bring-up via the vendored sensor layer   integrated, build-verified
 *   SensorTrust / adaptive sampling / LoRa        NOT integrated in this firmware
 *
 * Nothing in this file has been run on a device: no ESP32-S3 has been flashed with
 * it. "firmware-built" is not "hardware-validated".
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "edge_channel.h"
#include "edge_config_generated.h"
#include "edge_nodes.h"
#include "edge_protocol.h"
#include "edge_sensor_port.h"

static const char *TAG = "edge_node";

/* How many cycles this smoke build runs before reporting diagnostics. */
#define EDGE_MAIN_CYCLES 5

static void log_frame(const edge_sensor_frame_t *frame)
{
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        edge_channel_t channel = (edge_channel_t)i;
        if (frame->valid[i]) {
            ESP_LOGI(TAG, "  %-11s = %.3f  (%s)", edge_channel_name(channel),
                     (double)frame->value[i],
                     edge_channel_status_name(frame->status[i]));
        } else {
            /* Say why there is no number, rather than printing a zero that looks
             * like a measurement. */
            ESP_LOGW(TAG, "  %-11s = (no value: %s)", edge_channel_name(channel),
                     edge_channel_status_name(frame->status[i]));
        }
    }
}

static void log_hex(const uint8_t *bytes, size_t length)
{
    char line[3 * EDGE_MAX_FRAME_SIZE + 1];
    size_t offset = 0u;
    for (size_t i = 0; i < length && offset + 3u < sizeof(line); ++i) {
        int written = snprintf(line + offset, sizeof(line) - offset, "%02X ", bytes[i]);
        if (written <= 0) {
            break;
        }
        offset += (size_t)written;
    }
    ESP_LOGI(TAG, "  frame[%u] %s", (unsigned)length, line);
}

void app_main(void)
{
    const edge_node_def_t *node = edge_node_by_id(EDGE_NODE_B_ID);
    if (node == NULL) {
        ESP_LOGE(TAG, "node definition missing from the roster");
        return;
    }

    ESP_LOGI(TAG, "EdgeSense sensor node %s (node_id=%u) expected_mask=0x%02X",
             node->name, (unsigned)node->node_id, (unsigned)node->expected_mask);
    ESP_LOGI(TAG, "protocol v%d, fault taxonomy %s, soil_enabled=%d",
             edge_protocol_version(), EDGE_FAULT_TAXONOMY_VERSION,
             (int)node->soil_enabled);

    if (!edge_sensor_port_init(node->expected_mask)) {
        ESP_LOGE(TAG, "sensing path could not be armed; halting");
        return;
    }
    ESP_LOGI(TAG, "sensor backend: %s", edge_sensor_port_backend_name());

    for (int cycle = 0; cycle < EDGE_MAIN_CYCLES; ++cycle) {
        uint64_t node_ts_ms = (uint64_t)(esp_timer_get_time() / 1000);
        edge_sensor_frame_t frame;
        bool attempted_bring_up = false;

        edge_sensor_state_t state =
            edge_sensor_port_read(&frame, node_ts_ms, &attempted_bring_up);

        ESP_LOGI(TAG, "cycle %d @%" PRIu64 " ms: state=%s bring_up_attempted=%s", cycle,
                 node_ts_ms, edge_sensor_state_name(state),
                 attempted_bring_up ? "yes" : "no");
        log_frame(&frame);

        /* Prove the frame survives the codec. Sequence numbers here are this
         * node's own; they mean nothing to another node's timeline. */
        edge_data_packet_t packet;
        memset(&packet, 0, sizeof(packet));
        packet.node_id = node->node_id;
        packet.node_ts_ms = (uint32_t)node_ts_ms;
        packet.session_id = 0u;
        packet.seq = (uint16_t)(cycle + 1);
        packet.frame = frame;
        /* Trust and change score are inputs the sensing path does not produce yet;
         * they are zero because nothing computed them, not because they are zero. */
        packet.node_trust = 0u;
        packet.change_score = 0u;

        uint8_t encoded[EDGE_MAX_FRAME_SIZE];
        size_t length = edge_protocol_encode_data(&packet, encoded, sizeof(encoded));
        if (length == 0u) {
            ESP_LOGE(TAG, "  protocol encode refused the frame (nothing is put on the wire)");
        } else {
            log_hex(encoded, length);
            edge_data_packet_t decoded;
            if (edge_protocol_decode_data(encoded, length, &decoded)) {
                ESP_LOGI(TAG, "  codec roundtrip ok: seq=%u session=%u",
                         (unsigned)decoded.seq, (unsigned)decoded.session_id);
            } else {
                /* If this ever prints, the codec and the transport disagree and
                 * every downstream number is suspect. */
                ESP_LOGE(TAG, "  codec roundtrip FAILED");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(EDGE_PRIMARY_RQ_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "diagnostics: init_attempts=%u init_failures=%u read_failures=%u outages=%u",
             edge_sensor_port_init_attempts(), edge_sensor_port_init_failures(),
             edge_sensor_port_read_failures_total(),
             edge_sensor_port_unavailability_events());
    ESP_LOGI(TAG, "smoke run complete; this firmware does not transmit");
}
