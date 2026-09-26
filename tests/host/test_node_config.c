/*
 * test_node_config.c — host tests for the node roster.
 *
 * The B/C symmetry assertion here is the firmware-side half of Phase 1A exit
 * criterion F. scripts/check_config_parity.py checks the YAML; this checks the
 * values the firmware will actually compile with, so a change that passes the
 * YAML check but breaks the generated roster is still caught.
 *
 * Run: see scripts/test_host.sh
 */
#include <stdio.h>

#include "edge_channel.h"
#include "edge_config_generated.h"
#include "edge_nodes.h"
#include "harness.h"

static void test_roster_shape(void)
{
    CHECK_UINT_EQ(edge_node_count(), 3u);
    CHECK(edge_node_table() != NULL);
}

static void test_lookup_by_id(void)
{
    const edge_node_def_t *a = edge_node_by_id(EDGE_NODE_A_ID);
    const edge_node_def_t *b = edge_node_by_id(EDGE_NODE_B_ID);
    const edge_node_def_t *c = edge_node_by_id(EDGE_NODE_C_ID);

    CHECK(a != NULL);
    CHECK(b != NULL);
    CHECK(c != NULL);
    if (a != NULL) {
        CHECK_STR_EQ(a->name, "A");
        CHECK_INT_EQ(a->role, EDGE_ROLE_GATEWAY);
        CHECK(!edge_node_is_sensor(a));
    }
    if (b != NULL) {
        CHECK_STR_EQ(b->name, "B");
        CHECK_INT_EQ(b->role, EDGE_ROLE_SENSOR);
        CHECK(edge_node_is_sensor(b));
    }
    if (c != NULL) {
        CHECK_STR_EQ(c->name, "C");
        CHECK_INT_EQ(c->role, EDGE_ROLE_SENSOR);
        CHECK(edge_node_is_sensor(c));
    }
}

static void test_unknown_lookups_return_null(void)
{
    CHECK(edge_node_by_id(200u) == NULL);
    CHECK(edge_node_by_name("Z") == NULL);
    CHECK(edge_node_by_name("") == NULL);
    CHECK(edge_node_by_name(NULL) == NULL);
}

static void test_gateway_measures_nothing(void)
{
    const edge_node_def_t *a = edge_node_by_id(EDGE_NODE_A_ID);
    CHECK(a != NULL);
    if (a != NULL) {
        /* The gateway is deliberately sensor-free: it aggregates, it does not
         * witness the environment. */
        CHECK_UINT_EQ(a->expected_mask, 0u);
        CHECK(!a->soil_enabled);
    }
}

static void test_sensor_nodes_expect_the_documented_channels(void)
{
    const uint8_t expected = (uint8_t)(EDGE_CH_MASK(EDGE_CH_TEMPERATURE) |
                                       EDGE_CH_MASK(EDGE_CH_HUMIDITY) |
                                       EDGE_CH_MASK(EDGE_CH_LIGHT));
    const edge_node_def_t *b = edge_node_by_id(EDGE_NODE_B_ID);
    const edge_node_def_t *c = edge_node_by_id(EDGE_NODE_C_ID);

    CHECK(b != NULL && c != NULL);
    if (b != NULL) {
        CHECK_UINT_EQ(b->expected_mask, expected);
    }
    if (c != NULL) {
        CHECK_UINT_EQ(c->expected_mask, expected);
    }
}

/*
 * The point of the whole roster: B and C are redundant witnesses. If they differ
 * in anything but identity, "the nodes disagree" stops being attributable.
 */
static void test_bc_symmetry(void)
{
    const edge_node_def_t *b = edge_node_by_id(EDGE_NODE_B_ID);
    const edge_node_def_t *c = edge_node_by_id(EDGE_NODE_C_ID);
    CHECK(b != NULL && c != NULL);
    if (b == NULL || c == NULL) {
        return;
    }

    CHECK_INT_EQ(b->role, c->role);
    CHECK_UINT_EQ(b->expected_mask, c->expected_mask);
    CHECK_INT_EQ(b->soil_enabled, c->soil_enabled);

    /* Identity must differ, otherwise the roster is two names for one node. */
    CHECK(b->node_id != c->node_id);
    CHECK(strcmp(b->name, c->name) != 0);
}

static void test_masks_use_only_defined_channels(void)
{
    for (unsigned i = 0u; i < edge_node_count(); ++i) {
        const edge_node_def_t *node = &edge_node_table()[i];
        CHECK((node->expected_mask & (uint8_t)~EDGE_CH_MASK_ALL) == 0u);
        /* A sensor node with an empty mask would never be HEALTHY; that is a
         * configuration mistake, not a runtime condition. */
        if (node->role == EDGE_ROLE_SENSOR) {
            CHECK(node->expected_mask != 0u);
        }
    }
}

static void test_version_markers_are_present(void)
{
    /* These end up in every frame and every experiment manifest, so they have to
     * exist as compiled values rather than only in prose. */
    CHECK_INT_EQ(EDGE_PROTOCOL_VERSION, 1);
    CHECK_STR_EQ(EDGE_FAULT_TAXONOMY_VERSION, "phase-0.2");
    CHECK_UINT_EQ(EDGE_PRIMARY_RQ_INTERVAL_MS, 5000u);
    CHECK_UINT_EQ(EDGE_ADAPTIVE_LADDER_COUNT, 5u);
    CHECK_INT_EQ(EDGE_ADAPTIVE_LADDER_MS[0], 60000);
    CHECK_INT_EQ(EDGE_ADAPTIVE_LADDER_MS[EDGE_ADAPTIVE_LADDER_COUNT - 1], 5000);
}

static void test_soil_is_not_a_shared_event_channel(void)
{
    /* Frozen decision: soil moisture is too spatially local for two probes 30-50
     * cm apart to be comparable witnesses, so it is off in the shipped config. */
    CHECK_INT_EQ(EDGE_SOIL_ENABLED, 0);
    CHECK_INT_EQ(EDGE_SOIL_REQUIRE_CALIBRATION, 1);
}

static void test_role_names(void)
{
    CHECK_STR_EQ(edge_node_role_name(EDGE_ROLE_GATEWAY), "gateway");
    CHECK_STR_EQ(edge_node_role_name(EDGE_ROLE_SENSOR), "sensor");
}

int main(void)
{
    (void)printf("test_node_config\n");
    test_roster_shape();
    test_lookup_by_id();
    test_unknown_lookups_return_null();
    test_gateway_measures_nothing();
    test_sensor_nodes_expect_the_documented_channels();
    test_bc_symmetry();
    test_masks_use_only_defined_channels();
    test_version_markers_are_present();
    test_soil_is_not_a_shared_event_channel();
    test_role_names();
    return HARNESS_REPORT("node_config");
}
