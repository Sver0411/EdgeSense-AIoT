/*
 * edge_nodes.h — read-only access to the node roster.
 *
 * The roster is generated from config/edge_config.yaml (see
 * firmware/common/config/edge_config_generated.h). This unit wraps it in a small
 * portable API so firmware, host tests and tooling read the same list rather than
 * each embedding its own copy of the numbers.
 *
 * Portable C11: no ESP-IDF, no allocation.
 */
#ifndef EDGE_NODES_H
#define EDGE_NODES_H

#include <stdbool.h>
#include <stdint.h>

#include "edge_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EDGE_ROLE_GATEWAY = 0,
    EDGE_ROLE_SENSOR
} edge_node_role_t;

typedef struct {
    uint8_t          node_id;
    const char      *name;
    edge_node_role_t role;
    /* Channels this node is expected to measure. Used to decide whether a missing
     * channel is a degradation or simply something this node never had. */
    uint8_t          expected_mask;
    bool             soil_enabled;
} edge_node_def_t;

unsigned                edge_node_count(void);
const edge_node_def_t  *edge_node_table(void);

/* NULL when the id or name is not in the roster. */
const edge_node_def_t  *edge_node_by_id(uint8_t node_id);
const edge_node_def_t  *edge_node_by_name(const char *name);

bool                    edge_node_is_sensor(const edge_node_def_t *node);
const char             *edge_node_role_name(edge_node_role_t role);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_NODES_H */
