/*
 * edge_config_generated.h — GENERATED FILE. DO NOT EDIT.
 *
 * Rendered from config/edge_config.yaml by scripts/generate_config.py.
 * Edit the YAML, then re-run the generator. scripts/check_config_parity.py
 * fails when this file no longer matches the YAML, so a hand edit here is
 * caught rather than trusted.
 */

#ifndef EDGE_CONFIG_GENERATED_H
#define EDGE_CONFIG_GENERATED_H


#include "edge_channel.h"   /* channel enum and EDGE_CH_MASK */


/* versions */

#define EDGE_CONFIG_SCHEMA 1
#define EDGE_CONFIG_VERSION 1
#define EDGE_PROTOCOL_VERSION 1
#define EDGE_FAULT_TAXONOMY_VERSION "phase-0.2"

/* sampling (seconds -> milliseconds) */

#define EDGE_PRIMARY_RQ_INTERVAL_MS 5000
#define EDGE_BEACON_INTERVAL_MS 60000
#define EDGE_HEARTBEAT_INTERVAL_MS 30000
#define EDGE_NODE_TIMEOUT_MS 120000
#define EDGE_ADAPTIVE_LADDER_COUNT 5
static const int EDGE_ADAPTIVE_LADDER_MS[EDGE_ADAPTIVE_LADDER_COUNT] = {
    60000, 40000, 20000, 10000, 5000
};

/* i2c and sensor addresses (documented, not verified) */

#define EDGE_I2C_SDA_GPIO 8
#define EDGE_I2C_SCL_GPIO 9
#define EDGE_SHT30_I2C_ADDR 0x0044
#define EDGE_BH1750_I2C_ADDR 0x0023

/* soil moisture: optional, never a shared-event channel */

#define EDGE_SOIL_ENABLED 0
#define EDGE_SOIL_ADC_GPIO 1
#define EDGE_SOIL_REQUIRE_CALIBRATION 1

/* LoRa E220 (documented, not verified) */

#define EDGE_LORA_UART_NUM 1
#define EDGE_LORA_BAUD 9600
#define EDGE_LORA_M0_GPIO 13
#define EDGE_LORA_M1_GPIO 14
#define EDGE_LORA_AUX_GPIO 15
#define EDGE_LORA_TXD_GPIO 17
#define EDGE_LORA_RXD_GPIO 16
#define EDGE_LORA_MODULE_ADDRESS 0x0000
#define EDGE_LORA_REG0 0x0062
#define EDGE_LORA_CHANNEL 0x0017

/* node roles */

#define EDGE_NODE_A_ID 0
#define EDGE_NODE_A_NAME "A"
#define EDGE_NODE_A_ROLE_GATEWAY 1
#define EDGE_NODE_A_EXPECTED_MASK ((uint8_t)(0u))
#define EDGE_NODE_A_SOIL_ENABLED 0
#define EDGE_NODE_B_ID 1
#define EDGE_NODE_B_NAME "B"
#define EDGE_NODE_B_ROLE_SENSOR 1
#define EDGE_NODE_B_EXPECTED_MASK ((uint8_t)(EDGE_CH_MASK(EDGE_CH_TEMPERATURE) | EDGE_CH_MASK(EDGE_CH_HUMIDITY) | EDGE_CH_MASK(EDGE_CH_LIGHT)))
#define EDGE_NODE_B_SOIL_ENABLED 0
#define EDGE_NODE_C_ID 2
#define EDGE_NODE_C_NAME "C"
#define EDGE_NODE_C_ROLE_SENSOR 1
#define EDGE_NODE_C_EXPECTED_MASK ((uint8_t)(EDGE_CH_MASK(EDGE_CH_TEMPERATURE) | EDGE_CH_MASK(EDGE_CH_HUMIDITY) | EDGE_CH_MASK(EDGE_CH_LIGHT)))
#define EDGE_NODE_C_SOIL_ENABLED 0
#define EDGE_NODE_COUNT 3

#endif /* EDGE_CONFIG_GENERATED_H */
