# EdgeSense sensor node — component sources.
#
# Both B and C build from this project. The node identity is configuration, not
# code (see sdkconfig.defaults).

set(EDGE_COMMON "${CMAKE_CURRENT_LIST_DIR}/../../common")

set(EDGE_SRCS
    "main.c"

    # EdgeSense's own portable core
    "${EDGE_COMMON}/sensor/edge_channel.c"
    "${EDGE_COMMON}/sensor/edge_sensor_state.c"
    "${EDGE_COMMON}/sensor/edge_sensor_map.c"
    "${EDGE_COMMON}/config/edge_nodes.c"
    "${EDGE_COMMON}/protocol/edge_protocol.c"

    # Device-side glue (the only unit here that touches ESP-IDF hardware APIs)
    "${EDGE_COMMON}/sensor_port/edge_sensor_port.c"

    # Vendored AdaptiveSense sensor layer — never edited in place.
    "${EDGE_COMMON}/vendor/adaptivesense/sensor.c"
    "${EDGE_COMMON}/vendor/adaptivesense/sensor_bus.c"
    "${EDGE_COMMON}/vendor/adaptivesense/sensor_sht30.c"
    "${EDGE_COMMON}/vendor/adaptivesense/sensor_bh1750.c"
    "${EDGE_COMMON}/vendor/adaptivesense/soil_moisture.c"
    "${EDGE_COMMON}/vendor/adaptivesense/sensor_supervisor.c"
    "${EDGE_COMMON}/vendor/adaptivesense/sht30_proto.c"
    "${EDGE_COMMON}/vendor/adaptivesense/bh1750_proto.c"
    "${EDGE_COMMON}/vendor/adaptivesense/soil_moisture_math.c"

    # Vendored EventGuard-LoRa transport: the CRC for EdgeSense Protocol v1.
    "${EDGE_COMMON}/vendor/eventguard/protocol.c"
)

idf_component_register(
    SRCS ${EDGE_SRCS}
    INCLUDE_DIRS
        "."
        "${EDGE_COMMON}"
        "${EDGE_COMMON}/sensor"
        "${EDGE_COMMON}/config"
        "${EDGE_COMMON}/protocol"
        "${EDGE_COMMON}/sensor_port"
        "${EDGE_COMMON}/vendor/adaptivesense"
        "${EDGE_COMMON}/vendor/eventguard"
    REQUIRES esp_timer driver esp_adc
)
