/*
 * test_edge_channel.c — host tests for the EdgeSense sensor frame contract.
 *
 * The tests below are grouped by the three rules stated in edge_channel.h, plus
 * the data-integrity case that motivated the whole design: a channel that failed
 * this cycle must not carry the previous cycle's value.
 *
 * Run: see scripts/test_host.sh
 */
#include <math.h>
#include <string.h>

#include "edge_channel.h"
#include "harness.h"

#define ALL_MEASURED EDGE_CH_MASK_ALL

/* Two masks used throughout. Named for exactly what they select, because the
 * whole point of these tests is that a mask decides whether a missing channel
 * counts as a problem. */
#define MASK_TEMP_HUM                                                        \
    (uint8_t)(EDGE_CH_MASK(EDGE_CH_TEMPERATURE) | EDGE_CH_MASK(EDGE_CH_HUMIDITY))
#define MASK_TEMP_HUM_LIGHT (uint8_t)(MASK_TEMP_HUM | EDGE_CH_MASK(EDGE_CH_LIGHT))

static void test_begin_clears_everything(void)
{
    edge_sensor_frame_t frame;
    /* Poison the frame so "begin did nothing" cannot pass by accident. */
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        frame.value[i] = 999.0f;
        frame.status[i] = EDGE_CH_OK;
        frame.valid[i] = true;
    }
    frame.node_ts_ms = 1u;

    edge_frame_begin(&frame, 12345u);

    CHECK_UINT_EQ(frame.node_ts_ms, 12345u);
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        CHECK_NEAR(frame.value[i], 0.0, 0.0);
        CHECK(!frame.valid[i]);
        CHECK_INT_EQ(frame.status[i], EDGE_CH_NOT_SAMPLED);
    }
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, ALL_MEASURED), 0u);
    CHECK(!edge_frame_any_ok(&frame, ALL_MEASURED));
    CHECK(!edge_frame_all_ok(&frame, ALL_MEASURED));
}

static void test_begin_is_null_safe(void)
{
    edge_frame_begin(NULL, 1u); /* must not crash */
    CHECK(true);
}

static void test_set_records_a_valid_reading(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);

    CHECK(edge_frame_set(&frame, EDGE_CH_TEMPERATURE, 24.1f));
    CHECK(edge_frame_set(&frame, EDGE_CH_HUMIDITY, 55.5f));

    CHECK(edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK(edge_frame_channel_ok(&frame, EDGE_CH_HUMIDITY));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_LIGHT));
    CHECK_INT_EQ(frame.status[EDGE_CH_TEMPERATURE], EDGE_CH_OK);
    CHECK_NEAR(edge_frame_value_or(&frame, EDGE_CH_TEMPERATURE, -1.0f), 24.1, 1e-4);
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, ALL_MEASURED), 2u);
}

static void test_rule_r2_non_finite_is_never_a_measurement(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);

    CHECK(!edge_frame_set(&frame, EDGE_CH_HUMIDITY, NAN));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_HUMIDITY));
    CHECK_INT_EQ(frame.status[EDGE_CH_HUMIDITY], EDGE_CH_OUT_OF_RANGE);
    CHECK_NEAR(frame.value[EDGE_CH_HUMIDITY], 0.0, 0.0);

    CHECK(!edge_frame_set(&frame, EDGE_CH_LIGHT, INFINITY));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_LIGHT));
    CHECK_INT_EQ(frame.status[EDGE_CH_LIGHT], EDGE_CH_OUT_OF_RANGE);

    CHECK(!edge_frame_set(&frame, EDGE_CH_LIGHT, -INFINITY));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_LIGHT));

    /* A refused write must not have made the channel usable. */
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, ALL_MEASURED), 0u);
}

static void test_rule_r3_cannot_be_invalid_and_ok(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);

    CHECK(edge_frame_set(&frame, EDGE_CH_TEMPERATURE, 24.0f));
    CHECK(!edge_frame_set_invalid(&frame, EDGE_CH_TEMPERATURE, EDGE_CH_OK));
    /* Refused: the channel keeps its successful reading rather than becoming
     * invalid-with-no-reason. */
    CHECK(edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK_INT_EQ(frame.status[EDGE_CH_TEMPERATURE], EDGE_CH_OK);
    CHECK_NEAR(frame.value[EDGE_CH_TEMPERATURE], 24.0, 1e-6);
}

static void test_set_invalid_records_reason_and_zeroes_value(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);

    CHECK(edge_frame_set(&frame, EDGE_CH_LIGHT, 92.5f));
    CHECK(edge_frame_set_invalid(&frame, EDGE_CH_LIGHT, EDGE_CH_TIMEOUT));

    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_LIGHT));
    CHECK_INT_EQ(frame.status[EDGE_CH_LIGHT], EDGE_CH_TIMEOUT);
    CHECK_NEAR(frame.value[EDGE_CH_LIGHT], 0.0, 0.0);
    /* The value must not be the last successful reading. */
    CHECK_NEAR(edge_frame_value_or(&frame, EDGE_CH_LIGHT, -1.0f), -1.0, 0.0);
}

/*
 * The case the whole contract exists for: a failed read must not republish the
 * previous cycle's number as if it were current.
 */
static void test_r1_no_stale_value_crosses_a_cycle(void)
{
    edge_sensor_frame_t frame;
    float seen;

    edge_frame_begin(&frame, 100u);
    CHECK(edge_frame_set(&frame, EDGE_CH_TEMPERATURE, 24.1f));
    CHECK(edge_frame_set(&frame, EDGE_CH_HUMIDITY, 55.0f));
    seen = edge_frame_value_or(&frame, EDGE_CH_TEMPERATURE, -999.0f);
    CHECK_NEAR(seen, 24.1, 1e-4);

    /* Next cycle: the SHT30 times out, only the light channel answers. */
    edge_frame_begin(&frame, 200u);
    CHECK(edge_frame_set_invalid(&frame, EDGE_CH_TEMPERATURE, EDGE_CH_TIMEOUT));
    CHECK(edge_frame_set_invalid(&frame, EDGE_CH_HUMIDITY, EDGE_CH_TIMEOUT));
    CHECK(edge_frame_set(&frame, EDGE_CH_LIGHT, 91.0f));

    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK_NEAR(frame.value[EDGE_CH_TEMPERATURE], 0.0, 0.0);
    CHECK_NEAR(edge_frame_value_or(&frame, EDGE_CH_TEMPERATURE, -999.0f), -999.0, 0.0);

    /* And simply forgetting to record anything must not resurrect 24.1 either. */
    edge_frame_begin(&frame, 300u);
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK_NEAR(edge_frame_value_or(&frame, EDGE_CH_TEMPERATURE, -999.0f), -999.0, 0.0);
}

static void test_one_dead_sensor_does_not_invalidate_the_others(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);

    /* SHT30 failed, BH1750 fine — the case a frame-level flag could not express. */
    CHECK(edge_frame_set_invalid(&frame, EDGE_CH_TEMPERATURE, EDGE_CH_READ_FAILED));
    CHECK(edge_frame_set_invalid(&frame, EDGE_CH_HUMIDITY, EDGE_CH_READ_FAILED));
    CHECK(edge_frame_set(&frame, EDGE_CH_LIGHT, 88.0f));

    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_HUMIDITY));
    CHECK(edge_frame_channel_ok(&frame, EDGE_CH_LIGHT));
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, ALL_MEASURED), 1u);
    CHECK(edge_frame_any_ok(&frame, ALL_MEASURED));
    CHECK(!edge_frame_all_ok(&frame, ALL_MEASURED));

    /* Now the other way round: BH1750 gone, SHT30 fine. */
    edge_frame_begin(&frame, 20u);
    CHECK(edge_frame_set(&frame, EDGE_CH_TEMPERATURE, 25.0f));
    CHECK(edge_frame_set(&frame, EDGE_CH_HUMIDITY, 50.0f));
    CHECK(edge_frame_set_invalid(&frame, EDGE_CH_LIGHT, EDGE_CH_ABSENT));

    CHECK(edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_LIGHT));
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, ALL_MEASURED), 2u);
    /* The soil channel is optional and simply never attempted on this node. */
    CHECK_INT_EQ(frame.status[EDGE_CH_SOIL], EDGE_CH_NOT_SAMPLED);
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_SOIL));
}

static void test_soil_is_optional_and_never_imputed(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);

    /* The historical defect: an unconnected probe published its rail value
     * (raw 4095) with a valid flag. Here the channel must be ABSENT and carry
     * no value at all. */
    CHECK(edge_frame_set_invalid(&frame, EDGE_CH_SOIL, EDGE_CH_ABSENT));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_SOIL));
    CHECK_NEAR(frame.value[EDGE_CH_SOIL], 0.0, 0.0);
    CHECK_NEAR(edge_frame_value_or(&frame, EDGE_CH_SOIL, -1.0f), -1.0, 0.0);
    CHECK_INT_EQ(frame.status[EDGE_CH_SOIL], EDGE_CH_ABSENT);
}

static void test_all_sensors_fail(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        CHECK(edge_frame_set_invalid(&frame, (edge_channel_t)i, EDGE_CH_READ_FAILED));
    }

    CHECK_UINT_EQ(edge_frame_valid_count(&frame, ALL_MEASURED), 0u);
    CHECK(!edge_frame_any_ok(&frame, ALL_MEASURED));
    CHECK(!edge_frame_all_ok(&frame, ALL_MEASURED));
}

static void test_masks_select_the_expected_channels_only(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);
    CHECK(edge_frame_set(&frame, EDGE_CH_TEMPERATURE, 24.0f));
    CHECK(edge_frame_set(&frame, EDGE_CH_HUMIDITY, 50.0f));
    CHECK(edge_frame_set_invalid(&frame, EDGE_CH_LIGHT, EDGE_CH_TIMEOUT));

    /* A node whose expected set excludes light is satisfied. */
    CHECK(edge_frame_all_ok(&frame, MASK_TEMP_HUM));
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, MASK_TEMP_HUM), 2u);

    /* One that includes light is not. */
    uint8_t with_light = MASK_TEMP_HUM_LIGHT;
    CHECK(!edge_frame_all_ok(&frame, with_light));
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, with_light), 2u);

    /* An empty mask is vacuously satisfied but selects nothing. */
    CHECK(edge_frame_all_ok(&frame, 0u));
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, 0u), 0u);
    CHECK(!edge_frame_any_ok(&frame, 0u));
}

static void test_null_frame_arguments_are_safe(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);

    CHECK(!edge_frame_set(NULL, EDGE_CH_SOIL, 1.0f));
    CHECK(!edge_frame_set_invalid(NULL, EDGE_CH_SOIL, EDGE_CH_ABSENT));
    CHECK(!edge_frame_channel_ok(NULL, EDGE_CH_SOIL));
    CHECK_NEAR(edge_frame_value_or(NULL, EDGE_CH_SOIL, 7.0f), 7.0, 0.0);
    CHECK_UINT_EQ(edge_frame_valid_count(NULL, ALL_MEASURED), 0u);
    CHECK(!edge_frame_all_ok(NULL, 0u));
    CHECK(!edge_frame_any_ok(NULL, ALL_MEASURED));
}

static void test_out_of_range_channel_is_refused(void)
{
    edge_sensor_frame_t frame;
    edge_frame_begin(&frame, 10u);

    CHECK(!edge_frame_set(&frame, EDGE_CH_COUNT, 1.0f));
    CHECK(!edge_frame_set_invalid(&frame, EDGE_CH_COUNT, EDGE_CH_ABSENT));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_COUNT));
    CHECK_NEAR(edge_frame_value_or(&frame, EDGE_CH_COUNT, 3.0f), 3.0, 0.0);
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, ALL_MEASURED), 0u);
}

static void test_names_are_stable_identifiers(void)
{
    CHECK_STR_EQ(edge_channel_name(EDGE_CH_TEMPERATURE), "temperature");
    CHECK_STR_EQ(edge_channel_name(EDGE_CH_HUMIDITY), "humidity");
    CHECK_STR_EQ(edge_channel_name(EDGE_CH_LIGHT), "light");
    CHECK_STR_EQ(edge_channel_name(EDGE_CH_SOIL), "soil");
    CHECK_STR_EQ(edge_channel_name(EDGE_CH_COUNT), "unknown");

    CHECK_STR_EQ(edge_channel_status_name(EDGE_CH_OK), "ok");
    CHECK_STR_EQ(edge_channel_status_name(EDGE_CH_NOT_SAMPLED), "not_sampled");
    CHECK_STR_EQ(edge_channel_status_name(EDGE_CH_ABSENT), "absent");
    CHECK_STR_EQ(edge_channel_status_name(EDGE_CH_READ_FAILED), "read_failed");
    CHECK_STR_EQ(edge_channel_status_name(EDGE_CH_TIMEOUT), "timeout");
    CHECK_STR_EQ(edge_channel_status_name(EDGE_CH_CRC_ERROR), "crc_error");
    CHECK_STR_EQ(edge_channel_status_name(EDGE_CH_OUT_OF_RANGE), "out_of_range");

    /* Every enumerator must have a name; a missing case would return "unknown". */
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        CHECK(strcmp(edge_channel_name((edge_channel_t)i), "unknown") != 0);
    }
    for (int i = 0; i < (int)EDGE_CH_STATUS_COUNT; ++i) {
        CHECK(strcmp(edge_channel_status_name((edge_channel_status_t)i), "unknown") != 0);
    }
}

int main(void)
{
    (void)printf("test_edge_channel\n");
    test_begin_clears_everything();
    test_begin_is_null_safe();
    test_set_records_a_valid_reading();
    test_rule_r2_non_finite_is_never_a_measurement();
    test_rule_r3_cannot_be_invalid_and_ok();
    test_set_invalid_records_reason_and_zeroes_value();
    test_r1_no_stale_value_crosses_a_cycle();
    test_one_dead_sensor_does_not_invalidate_the_others();
    test_soil_is_optional_and_never_imputed();
    test_all_sensors_fail();
    test_masks_select_the_expected_channels_only();
    test_null_frame_arguments_are_safe();
    test_out_of_range_channel_is_refused();
    test_names_are_stable_identifiers();
    return HARNESS_REPORT("edge_channel");
}
