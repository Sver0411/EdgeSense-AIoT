/*
 * test_edge_sensor_state.c — host tests for the node sensor state mapping.
 *
 * These tests pin down the distinction the frame-level flag could not make:
 * "some of my sensors are missing" (DEGRADED) is not the same as "I cannot
 * measure anything" (UNAVAILABLE), and neither is the same as "I am waiting to
 * retry" (RETRY_WAIT / RECOVERING).
 *
 * Run: see scripts/test_host.sh
 */
#include <stdio.h>

#include "edge_channel.h"
#include "edge_sensor_state.h"
#include "harness.h"

#define EXPECTED_ENV                                                          \
    (uint8_t)(EDGE_CH_MASK(EDGE_CH_TEMPERATURE) | EDGE_CH_MASK(EDGE_CH_HUMIDITY))

static void frame_full(edge_sensor_frame_t *frame, uint64_t ts)
{
    edge_frame_begin(frame, ts);
    (void)edge_frame_set(frame, EDGE_CH_TEMPERATURE, 24.0f);
    (void)edge_frame_set(frame, EDGE_CH_HUMIDITY, 50.0f);
    (void)edge_frame_set(frame, EDGE_CH_LIGHT, 90.0f);
}

static void test_healthy_when_every_expected_channel_is_valid(void)
{
    edge_sensor_frame_t frame;
    frame_full(&frame, 10u);
    CHECK_INT_EQ(edge_sensor_state_derive(true, false, &frame, EXPECTED_ENV),
                 EDGE_SENSOR_STATE_HEALTHY);
    CHECK(edge_sensor_state_is_usable(EDGE_SENSOR_STATE_HEALTHY));
}

static void test_degraded_when_some_expected_channels_are_missing(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);
    (void)edge_frame_set(&frame, EDGE_CH_TEMPERATURE, 24.0f);
    (void)edge_frame_set_invalid(&frame, EDGE_CH_HUMIDITY, EDGE_CH_CRC_ERROR);

    CHECK_INT_EQ(edge_sensor_state_derive(true, false, &frame, EXPECTED_ENV),
                 EDGE_SENSOR_STATE_DEGRADED);
    CHECK(edge_sensor_state_is_usable(EDGE_SENSOR_STATE_DEGRADED));
}

static void test_unavailable_when_nothing_usable_came_back(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);
    (void)edge_frame_set_invalid(&frame, EDGE_CH_TEMPERATURE, EDGE_CH_TIMEOUT);
    (void)edge_frame_set_invalid(&frame, EDGE_CH_HUMIDITY, EDGE_CH_TIMEOUT);

    /* The layer claims to be ready while every read failed. This is the exact
     * state the vendored supervisor's header documents as an observed defect:
     * the log said "sensor ready" while no reading ever succeeded. */
    CHECK_INT_EQ(edge_sensor_state_derive(true, false, &frame, EXPECTED_ENV),
                 EDGE_SENSOR_STATE_UNAVAILABLE);
    CHECK(!edge_sensor_state_is_usable(EDGE_SENSOR_STATE_UNAVAILABLE));
}

static void test_retry_wait_and_recovering_come_from_the_lifecycle(void)
{
    edge_sensor_frame_t frame;
    frame_full(&frame, 10u);

    /* Not ready, and no attempt due yet. The frame is deliberately ignored: a
     * frame captured before an outage says nothing about the present. */
    CHECK_INT_EQ(edge_sensor_state_derive(false, false, &frame, EXPECTED_ENV),
                 EDGE_SENSOR_STATE_RETRY_WAIT);
    CHECK(!edge_sensor_state_is_usable(EDGE_SENSOR_STATE_RETRY_WAIT));

    /* Not ready, but a bring-up attempt is due now. */
    CHECK_INT_EQ(edge_sensor_state_derive(false, true, &frame, EXPECTED_ENV),
                 EDGE_SENSOR_STATE_RECOVERING);
    CHECK(!edge_sensor_state_is_usable(EDGE_SENSOR_STATE_RECOVERING));
}

static void test_a_stale_frame_cannot_report_healthy_while_not_ready(void)
{
    edge_sensor_frame_t frame;
    frame_full(&frame, 10u); /* a perfectly good frame from before the outage */

    CHECK_INT_EQ(edge_sensor_state_derive(false, false, &frame, EXPECTED_ENV),
                 EDGE_SENSOR_STATE_RETRY_WAIT);
    CHECK_INT_EQ(edge_sensor_state_derive(false, true, &frame, EXPECTED_ENV),
                 EDGE_SENSOR_STATE_RECOVERING);
}

static void test_unexpected_channel_failure_does_not_degrade(void)
{
    edge_sensor_frame_t frame;
    /* Light fails, but this node is not configured to measure light. */
    edge_frame_begin(&frame, 10u);
    (void)edge_frame_set(&frame, EDGE_CH_TEMPERATURE, 24.0f);
    (void)edge_frame_set(&frame, EDGE_CH_HUMIDITY, 50.0f);
    (void)edge_frame_set_invalid(&frame, EDGE_CH_LIGHT, EDGE_CH_ABSENT);
    (void)edge_frame_set_invalid(&frame, EDGE_CH_SOIL, EDGE_CH_ABSENT);

    CHECK_INT_EQ(edge_sensor_state_derive(true, false, &frame, EXPECTED_ENV),
                 EDGE_SENSOR_STATE_HEALTHY);

    /* Include light in the expected set and the same frame becomes DEGRADED. */
    uint8_t with_light = (uint8_t)(EXPECTED_ENV | EDGE_CH_MASK(EDGE_CH_LIGHT));
    CHECK_INT_EQ(edge_sensor_state_derive(true, false, &frame, with_light),
                 EDGE_SENSOR_STATE_DEGRADED);
}

static void test_null_frame_and_empty_expectation(void)
{
    /* Ready, but there is no frame at all: nothing usable is known. */
    CHECK_INT_EQ(edge_sensor_state_derive(true, false, NULL, EXPECTED_ENV),
                 EDGE_SENSOR_STATE_UNAVAILABLE);

    edge_sensor_frame_t frame;
    frame_full(&frame, 10u);
    /* A node asked to measure nothing cannot be healthy or faulty; it is simply
     * not measuring. Reporting HEALTHY here would make a misconfiguration look
     * like a working node. */
    CHECK_INT_EQ(edge_sensor_state_derive(true, false, &frame, 0u),
                 EDGE_SENSOR_STATE_UNAVAILABLE);
}

static void test_names_and_usability_cover_every_state(void)
{
    CHECK_STR_EQ(edge_sensor_state_name(EDGE_SENSOR_STATE_HEALTHY), "healthy");
    CHECK_STR_EQ(edge_sensor_state_name(EDGE_SENSOR_STATE_DEGRADED), "degraded");
    CHECK_STR_EQ(edge_sensor_state_name(EDGE_SENSOR_STATE_UNAVAILABLE), "unavailable");
    CHECK_STR_EQ(edge_sensor_state_name(EDGE_SENSOR_STATE_RETRY_WAIT), "retry_wait");
    CHECK_STR_EQ(edge_sensor_state_name(EDGE_SENSOR_STATE_RECOVERING), "recovering");

    for (int i = 0; i < (int)EDGE_SENSOR_STATE_COUNT; ++i) {
        const char *name = edge_sensor_state_name((edge_sensor_state_t)i);
        CHECK(name != NULL);
        if (name != NULL) {
            CHECK(strcmp(name, "unknown") != 0);
        }
    }
    CHECK(edge_sensor_state_is_usable(EDGE_SENSOR_STATE_HEALTHY));
    CHECK(edge_sensor_state_is_usable(EDGE_SENSOR_STATE_DEGRADED));
    CHECK(!edge_sensor_state_is_usable(EDGE_SENSOR_STATE_UNAVAILABLE));
    CHECK(!edge_sensor_state_is_usable(EDGE_SENSOR_STATE_RETRY_WAIT));
    CHECK(!edge_sensor_state_is_usable(EDGE_SENSOR_STATE_RECOVERING));
}

int main(void)
{
    (void)printf("test_edge_sensor_state\n");
    test_healthy_when_every_expected_channel_is_valid();
    test_degraded_when_some_expected_channels_are_missing();
    test_unavailable_when_nothing_usable_came_back();
    test_retry_wait_and_recovering_come_from_the_lifecycle();
    test_a_stale_frame_cannot_report_healthy_while_not_ready();
    test_unexpected_channel_failure_does_not_degrade();
    test_null_frame_and_empty_expectation();
    test_names_and_usability_cover_every_state();
    return HARNESS_REPORT("edge_sensor_state");
}
