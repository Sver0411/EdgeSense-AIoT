/*
 * edge_nodes.c — the node roster, built from the generated configuration.
 *
 * Every value here comes from firmware/common/config/edge_config_generated.h,
 * which is rendered from config/edge_config.yaml. Nothing in this file invents a
 * number; edits belong in the YAML.
 */
#include "edge_nodes.h"

#include <stddef.h>
#include <string.h>

#include "edge_config_generated.h"

static const edge_node_def_t EDGE_NODE_TABLE[EDGE_NODE_COUNT] = {
    {
        .node_id = EDGE_NODE_A_ID,
        .name = EDGE_NODE_A_NAME,
        .role = EDGE_ROLE_GATEWAY,
        .expected_mask = EDGE_NODE_A_EXPECTED_MASK,
        .soil_enabled = (EDGE_NODE_A_SOIL_ENABLED != 0),
    },
    {
        .node_id = EDGE_NODE_B_ID,
        .name = EDGE_NODE_B_NAME,
        .role = EDGE_ROLE_SENSOR,
        .expected_mask = EDGE_NODE_B_EXPECTED_MASK,
        .soil_enabled = (EDGE_NODE_B_SOIL_ENABLED != 0),
    },
    {
        .node_id = EDGE_NODE_C_ID,
        .name = EDGE_NODE_C_NAME,
        .role = EDGE_ROLE_SENSOR,
        .expected_mask = EDGE_NODE_C_EXPECTED_MASK,
        .soil_enabled = (EDGE_NODE_C_SOIL_ENABLED != 0),
    },
};

unsigned edge_node_count(void)
{
    return (unsigned)EDGE_NODE_COUNT;
}

const edge_node_def_t *edge_node_table(void)
{
    return EDGE_NODE_TABLE;
}

const edge_node_def_t *edge_node_by_id(uint8_t node_id)
{
    for (unsigned i = 0u; i < edge_node_count(); ++i) {
        if (EDGE_NODE_TABLE[i].node_id == node_id) {
            return &EDGE_NODE_TABLE[i];
        }
    }
    return NULL;
}

const edge_node_def_t *edge_node_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (unsigned i = 0u; i < edge_node_count(); ++i) {
        if (EDGE_NODE_TABLE[i].name != NULL &&
            strcmp(EDGE_NODE_TABLE[i].name, name) == 0) {
            return &EDGE_NODE_TABLE[i];
        }
    }
    return NULL;
}

bool edge_node_is_sensor(const edge_node_def_t *node)
{
    return node != NULL && node->role == EDGE_ROLE_SENSOR;
}

const char *edge_node_role_name(edge_node_role_t role)
{
    switch (role) {
    case EDGE_ROLE_GATEWAY:
        return "gateway";
    case EDGE_ROLE_SENSOR:
        return "sensor";
    }
    return "unknown";
}
