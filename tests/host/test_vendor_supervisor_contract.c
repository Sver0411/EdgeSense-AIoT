/*
 * test_vendor_supervisor_contract.c — EdgeSense's test of the sensor supervisor
 * it vendors.
 *
 * The upstream project ships its own tests for this unit. This file is not a
 * duplicate of them: it asserts the *specific behaviours EdgeSense depends on*,
 * against the pinned commit, so that a future re-vendor cannot silently change
 * the contract without a red test here.
 *
 * Scenario order matches the Phase 1A requirement:
 *   initial probe -> read timeout -> repeated timeout -> unavailable
 *   -> retry -> successful recovery
 *
 * Run: see scripts/test_host.sh
 */
#include <stdbool.h>
#include <stdio.h>

#include "harness.h"
#include "sensor_supervisor.h"

static const double RETRY_S = 5.0;

static void test_initial_probe_is_offered_immediately(void)
{
    sensor_supervisor_t sup;
    sensor_sup_init(&sup, RETRY_S, 3u, 0.0);

    /* A node that cannot read its sensor has nothing useful to do, so the first
     * bring-up attempt is allowed at once rather than after a delay. */
    CHECK(!sensor_sup_ready(&sup));
    CHECK(sensor_sup_should_attempt(&sup, 0.0));
    CHECK_STR_EQ(sensor_sup_state_name(&sup), "not attempted yet");
}

static void test_failed_bring_up_is_rate_limited(void)
{
    sensor_supervisor_t sup;
    sensor_sup_init(&sup, RETRY_S, 3u, 0.0);

    sensor_sup_note_attempt(&sup, 0.0, false);

    CHECK(!sensor_sup_ready(&sup));
    CHECK_UINT_EQ(sup.init_attempts, 1u);
    CHECK_UINT_EQ(sup.init_failures, 1u);
    CHECK_STR_EQ(sensor_sup_state_name(&sup), "unavailable (will retry)");

    /* Too soon, and then due. */
    CHECK(!sensor_sup_should_attempt(&sup, RETRY_S / 2.0));
    CHECK(sensor_sup_should_attempt(&sup, RETRY_S));
    CHECK(sensor_sup_should_attempt(&sup, RETRY_S * 10.0));
}

static void test_successful_bring_up_stops_retrying(void)
{
    sensor_supervisor_t sup;
    sensor_sup_init(&sup, RETRY_S, 3u, 0.0);
    sensor_sup_note_attempt(&sup, 0.0, false);
    sensor_sup_note_attempt(&sup, RETRY_S, true);

    CHECK(sensor_sup_ready(&sup));
    CHECK(!sensor_sup_should_attempt(&sup, RETRY_S * 100.0));
    CHECK_STR_EQ(sensor_sup_state_name(&sup), "ready");
}

static void test_single_read_failure_does_not_tear_down_a_working_sensor(void)
{
    sensor_supervisor_t sup;
    sensor_sup_init(&sup, RETRY_S, 3u, 0.0);
    sensor_sup_note_attempt(&sup, 0.0, true);

    CHECK(!sensor_sup_note_read_failure(&sup, 1.0));
    CHECK(!sensor_sup_note_read_failure(&sup, 2.0));
    CHECK(sensor_sup_ready(&sup));
    CHECK_UINT_EQ(sup.read_failures_consecutive, 2u);
    CHECK_STR_EQ(sensor_sup_state_name(&sup), "ready (reads failing)");

    /* A single success clears the streak. */
    sensor_sup_note_read_success(&sup);
    CHECK_UINT_EQ(sup.read_failures_consecutive, 0u);
    CHECK_STR_EQ(sensor_sup_state_name(&sup), "ready");
    CHECK_UINT_EQ(sup.read_successes, 1u);
}

static void test_repeated_timeouts_declare_the_sensor_unavailable(void)
{
    sensor_supervisor_t sup;
    sensor_sup_init(&sup, RETRY_S, 3u, 0.0);
    sensor_sup_note_attempt(&sup, 0.0, true);

    CHECK(!sensor_sup_note_read_failure(&sup, 1.0));
    CHECK(!sensor_sup_note_read_failure(&sup, 2.0));
    /* The third consecutive failure is the transition, and it reports itself so
     * the caller can log the moment the node's behaviour changes. */
    CHECK(sensor_sup_note_read_failure(&sup, 3.0));

    CHECK(!sensor_sup_ready(&sup));
    CHECK_UINT_EQ(sup.unavailability_events, 1u);
    CHECK_UINT_EQ(sup.read_failures_total, 3u);
    CHECK_STR_EQ(sensor_sup_state_name(&sup), "unavailable (will retry)");

    /* Further failures while already down are not new transitions. */
    CHECK(!sensor_sup_note_read_failure(&sup, 4.0));
    CHECK_UINT_EQ(sup.unavailability_events, 1u);
}

static void test_retry_is_offered_right_after_the_outage(void)
{
    sensor_supervisor_t sup;
    sensor_sup_init(&sup, RETRY_S, 2u, 0.0);
    sensor_sup_note_attempt(&sup, 0.0, true);
    CHECK(!sensor_sup_note_read_failure(&sup, 1.0));
    CHECK(sensor_sup_note_read_failure(&sup, 2.0));

    /* Re-probe as soon as the loop comes round again. */
    CHECK(sensor_sup_should_attempt(&sup, 2.0));
}

static void test_recovery_after_an_outage(void)
{
    sensor_supervisor_t sup;
    sensor_sup_init(&sup, RETRY_S, 2u, 0.0);
    sensor_sup_note_attempt(&sup, 0.0, true);
    CHECK(!sensor_sup_note_read_failure(&sup, 1.0));
    CHECK(sensor_sup_note_read_failure(&sup, 2.0));

    /* The part is physically back: bring-up succeeds again. */
    sensor_sup_note_attempt(&sup, 2.1, true);
    CHECK(sensor_sup_ready(&sup));
    CHECK_UINT_EQ(sup.read_failures_consecutive, 0u);
    CHECK_UINT_EQ(sup.init_attempts, 2u);
    CHECK_UINT_EQ(sup.unavailability_events, 1u);

    /* And it can drop out a second time, independently. */
    CHECK(!sensor_sup_note_read_failure(&sup, 3.0));
    CHECK(sensor_sup_note_read_failure(&sup, 4.0));
    CHECK_UINT_EQ(sup.unavailability_events, 2u);
}

static void test_threshold_zero_disables_runtime_drop_out(void)
{
    sensor_supervisor_t sup;
    sensor_sup_init(&sup, RETRY_S, 0u, 0.0);
    sensor_sup_note_attempt(&sup, 0.0, true);

    for (int i = 0; i < 50; ++i) {
        CHECK(!sensor_sup_note_read_failure(&sup, (double)i));
    }
    /* Documented escape hatch: "never drop out at runtime". It exists so a test
     * can demonstrate the difference from the counted behaviour. */
    CHECK(sensor_sup_ready(&sup));
    CHECK_UINT_EQ(sup.unavailability_events, 0u);
}

static void test_null_is_safe(void)
{
    CHECK(!sensor_sup_ready(NULL));
    CHECK(!sensor_sup_should_attempt(NULL, 0.0));
    CHECK(!sensor_sup_note_read_failure(NULL, 0.0));
    CHECK_STR_EQ(sensor_sup_state_name(NULL), "unknown");
    sensor_sup_init(NULL, RETRY_S, 1u, 0.0);
    sensor_sup_note_attempt(NULL, 0.0, true);
    sensor_sup_note_read_success(NULL);
    CHECK(true);
}

static void test_zero_retry_interval_is_clamped_and_never_deadlocks(void)
{
    sensor_supervisor_t sup;
    sensor_sup_init(&sup, 0.0, 1u, 0.0);
    sensor_sup_note_attempt(&sup, 10.0, false);
    /* With a zero interval the next attempt is simply due immediately; the
     * supervisor must not schedule itself into the future forever. */
    CHECK(sensor_sup_should_attempt(&sup, 10.0));
}

int main(void)
{
    (void)printf("test_vendor_supervisor_contract\n");
    test_initial_probe_is_offered_immediately();
    test_failed_bring_up_is_rate_limited();
    test_successful_bring_up_stops_retrying();
    test_single_read_failure_does_not_tear_down_a_working_sensor();
    test_repeated_timeouts_declare_the_sensor_unavailable();
    test_retry_is_offered_right_after_the_outage();
    test_recovery_after_an_outage();
    test_threshold_zero_disables_runtime_drop_out();
    test_null_is_safe();
    test_zero_retry_interval_is_clamped_and_never_deadlocks();
    return HARNESS_REPORT("vendor_supervisor_contract");
}
