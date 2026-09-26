#!/usr/bin/env python3
"""Fail when configuration has silently diverged.

Three separate things are checked, because each of them fails quietly on its own:

1. Rendered header vs YAML
   firmware/common/config/edge_config_generated.h is derived from
   config/edge_config.yaml. A hand edit on either side makes firmware and offline
   analysis disagree about the same run.

2. B/C symmetry
   Nodes B and C are redundant witnesses of one environment. They must differ in
   node_id and nothing else: same channels, same soil setting, same role. If the
   two nodes drift apart, "the nodes disagree" stops being attributable to the
   environment or to a sensor, and the cross-node experiment loses its meaning.
   (Shared pins and radio settings come from the global YAML sections, so they are
   common by construction once (1) passes.)

3. Documented pins vs the hardware record
   The pin values below mirror docs/hardware/wiring.md, which in turn records what
   three pre-existing projects used. They are deliberately duplicated here so that
   changing a pin cannot happen silently: it requires editing the YAML, this file
   *and* the wiring document, at which point it is a decision rather than a typo.

Exit status: 0 when everything agrees, 1 otherwise.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = REPO_ROOT / "config" / "edge_config.yaml"
HEADER_PATH = REPO_ROOT / "firmware" / "common" / "config" / "edge_config_generated.h"

# Mirrors docs/hardware/wiring.md. Status there is "documented", not "verified":
# these come from the pre-existing projects' records, not from an I2C scan on
# EdgeSense hardware.
DOCUMENTED_PINS: dict[str, int] = {
    "i2c.sda_gpio": 8,
    "i2c.scl_gpio": 9,
    "i2c.sht30_addr": 0x44,
    "i2c.bh1750_addr": 0x23,
    "soil.adc_gpio": 1,
    "lora.m0_gpio": 13,
    "lora.m1_gpio": 14,
    "lora.aux_gpio": 15,
    "lora.txd_gpio": 17,
    "lora.rxd_gpio": 16,
    "lora.module_address": 0x0000,
    "lora.reg0": 0x62,
    "lora.channel": 0x17,
    "lora.baud": 9600,
}

EXPECTED_TAXONOMY_VERSION = "phase-0.2"


def load_yaml(path: Path) -> dict[str, Any]:
    try:
        import yaml
    except ImportError:  # pragma: no cover
        raise SystemExit(
            "PyYAML is required.\n"
            "  python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt"
        )
    with path.open(encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def check_rendered_header(problems: list[str]) -> None:
    sys.path.insert(0, str(REPO_ROOT / "scripts"))
    import generate_config  # noqa: PLC0415 - local import keeps the CLI simple

    if not HEADER_PATH.is_file():
        problems.append(f"generated header missing: {HEADER_PATH.relative_to(REPO_ROOT)}")
        return
    expected = generate_config.render(load_yaml(CONFIG_PATH))
    if HEADER_PATH.read_text(encoding="utf-8") != expected:
        problems.append(
            "generated header is stale: firmware/common/config/edge_config_generated.h "
            "does not match config/edge_config.yaml (run scripts/generate_config.py)"
        )


def check_bc_symmetry(config: dict[str, Any], problems: list[str]) -> None:
    sensors = [n for n in config["nodes"] if n["role"] == "sensor"]
    if len(sensors) < 2:
        problems.append(
            f"expected at least two sensor nodes to compare, found {len(sensors)}"
        )
        return

    reference = sensors[0]
    for other in sensors[1:]:
        for field in ("role", "channels", "soil_enabled"):
            if reference[field] != other[field]:
                problems.append(
                    f"B/C asymmetry: nodes {reference['name']} and {other['name']} "
                    f"differ in '{field}' ({reference[field]!r} vs {other[field]!r})"
                )
        if reference["node_id"] == other["node_id"]:
            problems.append(
                f"node_id collision: {reference['name']} and {other['name']} both use "
                f"{reference['node_id']}"
            )

    ids = [n["node_id"] for n in config["nodes"]]
    if len(set(ids)) != len(ids):
        problems.append(f"node_id values are not unique: {ids}")


def check_documented_pins(config: dict[str, Any], problems: list[str]) -> None:
    for dotted, expected in DOCUMENTED_PINS.items():
        section, key = dotted.split(".", 1)
        try:
            actual = config[section][key]
        except KeyError:
            problems.append(f"configuration is missing {dotted}")
            continue
        if int(actual) != int(expected):
            problems.append(
                f"pin drift: {dotted} is {actual!r} in edge_config.yaml but "
                f"{hex(expected)} in docs/hardware/wiring.md (update both, and only "
                f"after verifying on hardware)"
            )


def check_version_markers(config: dict[str, Any], problems: list[str]) -> None:
    if config.get("fault_taxonomy_version") != EXPECTED_TAXONOMY_VERSION:
        problems.append(
            f"fault_taxonomy_version is {config.get('fault_taxonomy_version')!r}, "
            f"expected {EXPECTED_TAXONOMY_VERSION!r} (see PHASE_0_2 section 2)"
        )
    if config.get("protocol_version") != 1:
        problems.append(
            f"protocol_version is {config.get('protocol_version')!r}; v1 expects 1"
        )
    if not re.match(r"^phase-\d+\.\d+$", str(config.get("fault_taxonomy_version", ""))):
        problems.append("fault_taxonomy_version should look like 'phase-0.2'")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)

    problems: list[str] = []
    config = load_yaml(CONFIG_PATH)

    check_rendered_header(problems)
    check_bc_symmetry(config, problems)
    check_documented_pins(config, problems)
    check_version_markers(config, problems)

    if problems:
        print(f"CONFIG PARITY FAILED — {len(problems)} problem(s):")
        for line in problems:
            print(f"  - {line}")
        return 1

    sensors = [n["name"] for n in config["nodes"] if n["role"] == "sensor"]
    print(
        "PASS config parity: header matches YAML, "
        f"B/C symmetric ({', '.join(sensors)}), pins match the hardware record, "
        f"taxonomy {EXPECTED_TAXONOMY_VERSION}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
