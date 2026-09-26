/*
 * test_edge_sensor_map.c — host tests for the driver-report to frame mapping.
 *
 * This is where the sensing path's policies are enforced, so the cases below are
 * about policy rather than arithmetic: configuration wins over the driver, a
 * refused value does not become a measurement, and an unrecognised status is
 * treated as a failure rather than promoted to data.
 *
 * Run: see scripts/test_host.sh
 */
#include <math.h>
#include <stdio.h>

#include "edge_sensor_map.h"
#include "harness.h"

#define MASK_TEMP_HUM_LIGHT                                                   \
    (uint8_t)(EDGE_CH_MASK(EDGE_CH_TEMPERATURE) | EDGE_CH_MASK(EDGE_CH_HUMIDITY) | \
              EDGE_CH_MASK(EDGE_CH_LIGHT))
#define MASK_TEMP_HUM                                                         \
    (uint8_t)(EDGE_CH_MASK(EDGE_CH_TEMPERATURE) | EDGE_CH_MASK(EDGE_CH_HUMIDITY))

static void report_all_ok(edge_driver_report_t *report)
{
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        report->value[i] = 0.0f;
        report->status[i] = EDGE_CH_NOT_SAMPLED;
    }
    report->value[EDGE_CH_TEMPERATURE] = 24.5f;
    report->status[EDGE_CH_TEMPERATURE] = EDGE_CH_OK;
    report->value[EDGE_CH_HUMIDITY] = 51.25f;
    report->status[EDGE_CH_HUMIDITY] = EDGE_CH_OK;
    report->value[EDGE_CH_LIGHT] = 275.0f;
    report->status[EDGE_CH_LIGHT] = EDGE_CH_OK;
}

static void test_healthy_report_maps_every_enabled_channel(void)
{
    edge_driver_report_t report;
    edge_sensor_frame_t frame;
    report_all_ok(&report);

    CHECK(edge_sensor_map(&report, MASK_TEMP_HUM_LIGHT, 4242u, &frame));
    CHECK_UINT_EQ(frame.node_ts_ms, 4242u);
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, MASK_TEMP_HUM_LIGHT), 3u);
    CHECK(edge_frame_all_ok(&frame, MASK_TEMP_HUM_LIGHT));
    CHECK_NEAR(edge_frame_value_or(&frame, EDGE_CH_TEMPERATURE, -1.0f), 24.5, 1e-4);
    CHECK_NEAR(edge_frame_value_or(&frame, EDGE_CH_LIGHT, -1.0f), 275.0, 1e-4);
    /* Soil was not in the report and is not enabled: no value, and no pretence. */
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_SOIL));
    CHECK_INT_EQ(frame.status[EDGE_CH_SOIL], EDGE_CH_NOT_SAMPLED);
}

static void test_a_failed_channel_keeps_its_reason(void)
{
    edge_driver_report_t report;
    edge_sensor_frame_t frame;
    report_all_ok(&report);
    report.value[EDGE_CH_LIGHT] = 999.0f; /* a driver that left a number behind */
    report.status[EDGE_CH_LIGHT] = EDGE_CH_TIMEOUT;

    CHECK(edge_sensor_map(&report, MASK_TEMP_HUM_LIGHT, 10u, &frame));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_LIGHT));
    CHECK_INT_EQ(frame.status[EDGE_CH_LIGHT], EDGE_CH_TIMEOUT);
    CHECK_NEAR(frame.value[EDGE_CH_LIGHT], 0.0, 0.0);
    /* The other two are untouched: a dead light sensor does not blind the node. */
    CHECK(edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK(edge_frame_channel_ok(&frame, EDGE_CH_HUMIDITY));
}

static void test_configuration_wins_over_the_driver(void)
{
    edge_driver_report_t report;
    edge_sensor_frame_t frame;
    report_all_ok(&report);
    /* The driver claims a light reading, but this node is not configured for the
     * light channel. A value for a channel the node does not measure is not
     * evidence that it measures it. */
    CHECK(edge_sensor_map(&report, MASK_TEMP_HUM, 10u, &frame));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_LIGHT));
    CHECK_INT_EQ(frame.status[EDGE_CH_LIGHT], EDGE_CH_NOT_SAMPLED);
    CHECK_NEAR(frame.value[EDGE_CH_LIGHT], 0.0, 0.0);
    CHECK(edge_frame_all_ok(&frame, MASK_TEMP_HUM));
}

static void test_a_value_the_contract_refuses_becomes_out_of_range(void)
{
    edge_driver_report_t report;
    edge_sensor_frame_t frame;
    report_all_ok(&report);
    report.value[EDGE_CH_HUMIDITY] = NAN;

    CHECK(edge_sensor_map(&report, MASK_TEMP_HUM_LIGHT, 10u, &frame));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_HUMIDITY));
    CHECK_INT_EQ(frame.status[EDGE_CH_HUMIDITY], EDGE_CH_OUT_OF_RANGE);

    report_all_ok(&report);
    report.value[EDGE_CH_TEMPERATURE] = INFINITY;
    CHECK(edge_sensor_map(&report, MASK_TEMP_HUM_LIGHT, 10u, &frame));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK_INT_EQ(frame.status[EDGE_CH_TEMPERATURE], EDGE_CH_OUT_OF_RANGE);
}

static void test_an_unrecognised_status_is_a_failure_not_data(void)
{
    edge_driver_report_t report;
    edge_sensor_frame_t frame;
    report_all_ok(&report);
    /* A driver bug must not be able to promote itself into a measurement. */
    report.status[EDGE_CH_TEMPERATURE] = (edge_channel_status_t)99;

    CHECK(edge_sensor_map(&report, MASK_TEMP_HUM_LIGHT, 10u, &frame));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK_INT_EQ(frame.status[EDGE_CH_TEMPERATURE], EDGE_CH_READ_FAILED);
}

static void test_a_status_that_claims_ok_without_a_value_is_impossible(void)
{
    /* EDGE_CH_OK is the only status that means "there is a value"; the mapper
     * passes the value through the frame contract, which is where a non-finite
     * value is refused. A driver cannot claim OK and produce nothing usable. */
    edge_driver_report_t report;
    edge_sensor_frame_t frame;
    report_all_ok(&report);
    report.value[EDGE_CH_LIGHT] = NAN;
    report.status[EDGE_CH_LIGHT] = EDGE_CH_OK;

    CHECK(edge_sensor_map(&report, MASK_TEMP_HUM_LIGHT, 10u, &frame));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_LIGHT));
    CHECK_INT_EQ(frame.status[EDGE_CH_LIGHT], EDGE_CH_OUT_OF_RANGE);
}

static void test_empty_expectation_yields_an_empty_frame(void)
{
    edge_driver_report_t report;
    edge_sensor_frame_t frame;
    report_all_ok(&report);

    CHECK(edge_sensor_map(&report, 0u, 10u, &frame));
    CHECK_UINT_EQ(edge_frame_valid_count(&frame, EDGE_CH_MASK_ALL), 0u);
    CHECK(!edge_frame_any_ok(&frame, EDGE_CH_MASK_ALL));
    for (int i = 0; i < (int)EDGE_CH_COUNT; ++i) {
        CHECK_INT_EQ(frame.status[i], EDGE_CH_NOT_SAMPLED);
        CHECK_NEAR(frame.value[i], 0.0, 0.0);
    }
}

static void test_null_arguments_are_refused_and_leave_no_half_frame(void)
{
    edge_driver_report_t report;
    edge_sensor_frame_t frame;
    report_all_ok(&report);

    CHECK(!edge_sensor_map(NULL, MASK_TEMP_HUM_LIGHT, 10u, &frame));
    CHECK(!edge_sensor_map(&report, MASK_TEMP_HUM_LIGHT, 10u, NULL));
}

static void test_the_frame_is_rebuilt_each_call(void)
{
    edge_driver_report_t report;
    edge_sensor_frame_t frame;
    report_all_ok(&report);
    CHECK(edge_sensor_map(&report, MASK_TEMP_HUM_LIGHT, 10u, &frame));
    CHECK_NEAR(frame.value[EDGE_CH_TEMPERATURE], 24.5, 1e-4);

    /* A second call with a failing sensor must not leave the first call's values
     * in place. */
    report.status[EDGE_CH_TEMPERATURE] = EDGE_CH_TIMEOUT;
    report.value[EDGE_CH_TEMPERATURE] = 24.5f;
    CHECK(edge_sensor_map(&report, MASK_TEMP_HUM_LIGHT, 20u, &frame));
    CHECK(!edge_frame_channel_ok(&frame, EDGE_CH_TEMPERATURE));
    CHECK_NEAR(frame.value[EDGE_CH_TEMPERATURE], 0.0, 0.0);
    CHECK_UINT_EQ(frame.node_ts_ms, 20u);
}

int main(void)
{
    (void)printf("test_edge_sensor_map\n");
    test_healthy_report_maps_every_enabled_channel();
    test_a_failed_channel_keeps_its_reason();
    test_configuration_wins_over_the_driver();
    test_a_value_the_contract_refuses_becomes_out_of_range();
    test_an_unrecognised_status_is_a_failure_not_data();
    test_a_status_that_claims_ok_without_a_value_is_impossible();
    test_empty_expectation_yields_an_empty_frame();
    test_null_arguments_are_refused_and_leave_no_half_frame();
    test_the_frame_is_rebuilt_each_call();
    return HARNESS_REPORT("edge_sensor_map");
}
