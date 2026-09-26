#!/usr/bin/env python3
"""Render config/edge_config.yaml into the firmware's generated configuration header.

Why a generated header instead of hand-written #defines
-------------------------------------------------------
Two consumers have to agree on the same numbers: the firmware and the offline
evaluation tooling. If a threshold is edited in only one of them, firmware and
analysis quietly disagree about the same run, and neither file looks wrong. So the
YAML is the single source and the header is derived from it.

The output is byte-stable for the same input: keys are emitted in a fixed order
and nothing time- or machine-dependent is written. That is what lets
scripts/check_config_parity.py compare the committed header against a fresh render
and treat any difference as a failure.

Usage:
    python3 scripts/generate_config.py            # write the header
    python3 scripts/generate_config.py --check    # write nothing; report drift
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = REPO_ROOT / "config" / "edge_config.yaml"
OUTPUT_PATH = REPO_ROOT / "firmware" / "common" / "config" / "edge_config_generated.h"

GENERATED_BANNER = (
    "/*\n"
    " * edge_config_generated.h — GENERATED FILE. DO NOT EDIT.\n"
    " *\n"
    " * Rendered from config/edge_config.yaml by scripts/generate_config.py.\n"
    " * Edit the YAML, then re-run the generator. scripts/check_config_parity.py\n"
    " * fails when this file no longer matches the YAML, so a hand edit here is\n"
    " * caught rather than trusted.\n"
    " */\n"
)

# Channel name -> a *bitmask* expression. Using the bare enum constants here would
# be a silent trap: EDGE_CH_TEMPERATURE|EDGE_CH_HUMIDITY|EDGE_CH_LIGHT evaluates to
# 0|1|2 == 3, which looks like a plausible mask but silently drops every channel
# whose index is already covered by a lower bit.
CHANNEL_BITS = {
    "temperature": "EDGE_CH_MASK(EDGE_CH_TEMPERATURE)",
    "humidity": "EDGE_CH_MASK(EDGE_CH_HUMIDITY)",
    "light": "EDGE_CH_MASK(EDGE_CH_LIGHT)",
    "soil": "EDGE_CH_MASK(EDGE_CH_SOIL)",
}


def load_yaml(path: Path) -> dict[str, Any]:
    try:
        import yaml
    except ImportError:  # pragma: no cover - environment issue, not a code path
        raise SystemExit(
            "PyYAML is required to render the configuration.\n"
            "  python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt"
        )
    with path.open(encoding="utf-8") as handle:
        data = yaml.safe_load(handle)
    if not isinstance(data, dict):
        raise SystemExit(f"{path} did not parse into a mapping")
    return data


def macro(name: str, value: int | str) -> str:
    if isinstance(value, str):
        return f'#define {name} "{value}"'
    if isinstance(value, int) and value >= 0 and name.endswith(("_ADDR", "_REG0", "_CHANNEL", "_ADDRESS")):
        return f"#define {name} 0x{value:04X}"
    return f"#define {name} {value}"


def render(data: dict[str, Any]) -> str:
    sampling = data["sampling"]
    i2c = data["i2c"]
    soil = data["soil"]
    lora = data["lora"]
    nodes = data["nodes"]

    lines: list[str] = [
        GENERATED_BANNER,
        "#ifndef EDGE_CONFIG_GENERATED_H\n#define EDGE_CONFIG_GENERATED_H\n",
        '\n#include "edge_channel.h"   /* channel enum and EDGE_CH_MASK */\n',
        "\n/* versions */\n",
    ]
    lines.append(macro("EDGE_CONFIG_SCHEMA", int(data["schema"])))
    lines.append(macro("EDGE_CONFIG_VERSION", int(data["config_version"])))
    lines.append(macro("EDGE_PROTOCOL_VERSION", int(data["protocol_version"])))
    lines.append(macro("EDGE_FAULT_TAXONOMY_VERSION", str(data["fault_taxonomy_version"])))

    lines.append("\n/* sampling (seconds -> milliseconds) */\n")
    lines.append(macro("EDGE_PRIMARY_RQ_INTERVAL_MS", int(sampling["primary_rq_interval_s"]) * 1000))
    lines.append(macro("EDGE_BEACON_INTERVAL_MS", int(sampling["beacon_interval_s"]) * 1000))
    lines.append(macro("EDGE_HEARTBEAT_INTERVAL_MS", int(sampling["heartbeat_interval_s"]) * 1000))
    lines.append(macro("EDGE_NODE_TIMEOUT_MS", int(sampling["node_timeout_s"]) * 1000))
    ladder = [int(v) for v in sampling["adaptive_ladder_s"]]
    lines.append(f"#define EDGE_ADAPTIVE_LADDER_COUNT {len(ladder)}")
    lines.append("static const int EDGE_ADAPTIVE_LADDER_MS[EDGE_ADAPTIVE_LADDER_COUNT] = {")
    lines.append("    " + ", ".join(f"{v * 1000}" for v in ladder))
    lines.append("};")

    lines.append("\n/* i2c and sensor addresses (documented, not verified) */\n")
    lines.append(macro("EDGE_I2C_SDA_GPIO", int(i2c["sda_gpio"])))
    lines.append(macro("EDGE_I2C_SCL_GPIO", int(i2c["scl_gpio"])))
    lines.append(macro("EDGE_SHT30_I2C_ADDR", int(i2c["sht30_addr"])))
    lines.append(macro("EDGE_BH1750_I2C_ADDR", int(i2c["bh1750_addr"])))

    lines.append("\n/* soil moisture: optional, never a shared-event channel */\n")
    lines.append(macro("EDGE_SOIL_ENABLED", 1 if soil["enabled"] else 0))
    lines.append(macro("EDGE_SOIL_ADC_GPIO", int(soil["adc_gpio"])))
    lines.append(macro("EDGE_SOIL_REQUIRE_CALIBRATION", 1 if soil["require_calibration"] else 0))

    lines.append("\n/* LoRa E220 (documented, not verified) */\n")
    lines.append(macro("EDGE_LORA_UART_NUM", int(lora["uart_num"])))
    lines.append(macro("EDGE_LORA_BAUD", int(lora["baud"])))
    lines.append(macro("EDGE_LORA_M0_GPIO", int(lora["m0_gpio"])))
    lines.append(macro("EDGE_LORA_M1_GPIO", int(lora["m1_gpio"])))
    lines.append(macro("EDGE_LORA_AUX_GPIO", int(lora["aux_gpio"])))
    lines.append(macro("EDGE_LORA_TXD_GPIO", int(lora["txd_gpio"])))
    lines.append(macro("EDGE_LORA_RXD_GPIO", int(lora["rxd_gpio"])))
    lines.append(macro("EDGE_LORA_MODULE_ADDRESS", int(lora["module_address"])))
    lines.append(macro("EDGE_LORA_REG0", int(lora["reg0"])))
    lines.append(macro("EDGE_LORA_CHANNEL", int(lora["channel"])))

    lines.append("\n/* node roles */\n")
    for node in nodes:
        upper = str(node["name"]).upper()
        lines.append(f"#define EDGE_NODE_{upper}_ID {int(node['node_id'])}")
        lines.append(f'#define EDGE_NODE_{upper}_NAME "{node["name"]}"')
        lines.append(f'#define EDGE_NODE_{upper}_ROLE_{str(node["role"]).upper()} 1')
        mask = " | ".join(CHANNEL_BITS[c] for c in node["channels"]) or "0u"
        lines.append(f"#define EDGE_NODE_{upper}_EXPECTED_MASK ((uint8_t)({mask}))")
        lines.append(
            f"#define EDGE_NODE_{upper}_SOIL_ENABLED {1 if node['soil_enabled'] else 0}"
        )
    lines.append(f"#define EDGE_NODE_COUNT {len(nodes)}")

    lines.append("\n#endif /* EDGE_CONFIG_GENERATED_H */\n")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report drift without writing")
    args = parser.parse_args(argv)

    rendered = render(load_yaml(CONFIG_PATH))

    if args.check:
        if not OUTPUT_PATH.is_file():
            print(f"FAIL config parity: generated header missing at {OUTPUT_PATH}")
            return 1
        current = OUTPUT_PATH.read_text(encoding="utf-8")
        if current != rendered:
            print(f"FAIL config parity: {OUTPUT_PATH.relative_to(REPO_ROOT)} is stale")
            return 1
        print("PASS config header matches config/edge_config.yaml")
        return 0

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(rendered, encoding="utf-8")
    print(f"wrote {OUTPUT_PATH.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
