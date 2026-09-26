"""Schema validation for EdgeSense experiment artifacts.

Two schemas, kept strictly apart:

* **truth** — `truth/episodes.csv`. Ground truth for an experiment. It never
  reaches the running system: firmware does not receive it, the backend has no
  column for it, and `analyze_experiment.py` is not importable from the runtime.

* **decision** — `results/decisions.csv`. What the system decided. It must contain
  no truth field, and a decision label vocabulary narrower than the truth
  vocabulary, because the system is only ever asked to answer three things.

The separation is enforced here rather than trusted, so that an experiment cannot
accidentally measure itself against its own output.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

# Ground-truth categories (PHASE_0_2 section 2). Kept in one place so a typo in a
# CSV cannot quietly create a fourth category.
TRUTH_CATEGORIES: frozenset[str] = frozenset(
    {
        "sensor_fault",
        "shared_event",
        "localized_event",
        "network_fault",
        "device_unavailable",
    }
)

EVENT_SOURCES: frozenset[str] = frozenset(
    {"sensing", "environment", "radio", "power", "firmware"}
)

GROUND_TRUTH_SOURCES: frozenset[str] = frozenset(
    {
        "injection_plan",
        "physical_intervention_log",
        "firmware_diagnostic_counter",
        "host_observation",
    }
)

PHYSICAL_OR_INJECTED: frozenset[str] = frozenset({"physical", "injected"})

# What the running system may say. Deliberately narrower than the truth
# vocabulary: there is no `localized_event` decision, because the system makes no
# claim to distinguish one, and no `network_fault` decision, because that belongs
# to the transport evaluation.
DECISION_LABELS: frozenset[str] = frozenset({"normal", "sensor_fault", "shared_event"})

DECISION_SOURCES: frozenset[str] = frozenset({"gateway_fused", "node_only"})

# Categories the primary comparison needs in order to be defined at all.
PRIMARY_RQ_CATEGORIES: tuple[str, ...] = ("sensor_fault", "shared_event", "normal")

# Categories excluded from the primary metrics; reported separately instead.
EXCLUDED_FROM_PRIMARY: frozenset[str] = frozenset(
    {"localized_event", "network_fault", "device_unavailable"}
)

TRUTH_REQUIRED_COLUMNS: tuple[str, ...] = (
    "episode_id",
    "truth_category",
    "event_source",
    "family",
    "channels",
    "nodes",
    "start_ts",
    "end_ts",
    "physical_or_injected",
    "ground_truth_source",
)

# Optional columns. `scenario` groups episodes for per-scenario reporting, which
# the frozen design requires; `intensity`, `injection_method`,
# `intervention_description` and `notes` are free text.
TRUTH_OPTIONAL_COLUMNS: tuple[str, ...] = (
    "scenario",
    "intensity",
    "injection_method",
    "intervention_description",
    "notes",
)

DECISION_REQUIRED_COLUMNS: tuple[str, ...] = (
    "window_id",
    "node_id",
    "window_start",
    "window_end",
    "decision_label",
    "decision_source",
    "confidence",
)

# If any of these appear in a decision file, the file is not a decision file.
FORBIDDEN_IN_DECISIONS: frozenset[str] = frozenset(
    {
        "truth_category",
        "event_category",
        "ground_truth_source",
        "physical_or_injected",
        "truth",
        "fault_family",
        "injection_method",
    }
)


class SchemaError(Exception):
    """Raised when an artifact does not conform. Never caught to continue."""


@dataclass(frozen=True)
class TruthEpisode:
    episode_id: str
    truth_category: str
    event_source: str
    family: str
    channels: str
    nodes: str
    start: datetime
    end: datetime
    physical_or_injected: str
    ground_truth_source: str
    scenario: str
    row: int

    @property
    def duration_s(self) -> float:
        return (self.end - self.start).total_seconds()


@dataclass(frozen=True)
class Decision:
    window_id: str
    node_id: str
    start: datetime
    end: datetime
    decision_label: str
    decision_source: str
    confidence: float
    row: int


def _parse_ts(value: str, *, where: str, field: str) -> datetime:
    text = (value or "").strip()
    if not text:
        raise SchemaError(f"{where}: '{field}' is empty")
    candidate = text.replace("Z", "+00:00")
    try:
        return datetime.fromisoformat(candidate)
    except ValueError as exc:
        raise SchemaError(f"{where}: '{field}' is not ISO 8601: {text!r}") from exc


def _read_rows(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    if not path.is_file():
        raise SchemaError(f"missing required file: {path}")
    # utf-8-sig, not utf-8: these files are expected to be authored by hand, often
    # in a spreadsheet, and a byte-order mark is normal there. Treating a BOM as a
    # corrupted column name would reject a perfectly good file.
    with path.open(encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        fieldnames = [(name or "").lstrip("\ufeff").strip() for name in (reader.fieldnames or [])]
        rows: list[dict[str, str]] = []
        for row in reader:
            if row and any((value or "").strip() for value in row.values()):
                rows.append(
                    {
                        (key or "").lstrip("\ufeff").strip(): (value or "").strip()
                        for key, value in row.items()
                        if key
                    }
                )
    if not fieldnames:
        raise SchemaError(f"{path}: no header row")
    return fieldnames, rows


def _require_non_empty(value: str, *, where: str, field: str) -> None:
    if not value:
        raise SchemaError(f"{where}: required field '{field}' is empty")


def load_truth(path: Path) -> list[TruthEpisode]:
    fieldnames, rows = _read_rows(path)

    missing = [column for column in TRUTH_REQUIRED_COLUMNS if column not in fieldnames]
    if missing:
        raise SchemaError(f"{path}: missing required column(s): {', '.join(missing)}")

    unknown = [
        column
        for column in fieldnames
        if column not in TRUTH_REQUIRED_COLUMNS and column not in TRUTH_OPTIONAL_COLUMNS
    ]
    if unknown:
        raise SchemaError(
            f"{path}: unexpected column(s): {', '.join(unknown)} "
            f"(add to the schema deliberately, not by accident)"
        )

    episodes: list[TruthEpisode] = []
    seen: set[str] = set()
    for index, row in enumerate(rows, start=2):
        where = f"{path}:{index}"
        for field in TRUTH_REQUIRED_COLUMNS:
            _require_non_empty(row.get(field, ""), where=where, field=field)

        category = row["truth_category"]
        if category not in TRUTH_CATEGORIES:
            raise SchemaError(
                f"{where}: unknown truth_category {category!r}; "
                f"allowed: {sorted(TRUTH_CATEGORIES)}"
            )
        source = row["event_source"]
        if source not in EVENT_SOURCES:
            raise SchemaError(f"{where}: unknown event_source {source!r}")
        truth_source = row["ground_truth_source"]
        if truth_source not in GROUND_TRUTH_SOURCES:
            raise SchemaError(f"{where}: unknown ground_truth_source {truth_source!r}")
        provenance = row["physical_or_injected"]
        if provenance not in PHYSICAL_OR_INJECTED:
            raise SchemaError(f"{where}: physical_or_injected must be physical|injected")

        # A physical intervention is recorded by a person; an injection is declared
        # in advance. Each must say which it was.
        if provenance == "injected" and not row.get("injection_method", ""):
            raise SchemaError(f"{where}: injected episode needs 'injection_method'")
        if provenance == "physical" and not row.get("intervention_description", ""):
            raise SchemaError(f"{where}: physical episode needs 'intervention_description'")

        start = _parse_ts(row["start_ts"], where=where, field="start_ts")
        end = _parse_ts(row["end_ts"], where=where, field="end_ts")
        if end <= start:
            raise SchemaError(f"{where}: end_ts must be after start_ts")

        episode_id = row["episode_id"]
        if episode_id in seen:
            raise SchemaError(f"{where}: duplicate episode_id {episode_id!r}")
        seen.add(episode_id)

        episodes.append(
            TruthEpisode(
                episode_id=episode_id,
                truth_category=category,
                event_source=source,
                family=row["family"],
                channels=row["channels"],
                nodes=row["nodes"],
                start=start,
                end=end,
                physical_or_injected=provenance,
                ground_truth_source=truth_source,
                scenario=row.get("scenario", "") or row["family"],
                row=index,
            )
        )

    return episodes


def load_decisions(path: Path) -> list[Decision]:
    fieldnames, rows = _read_rows(path)

    leaked = sorted(FORBIDDEN_IN_DECISIONS.intersection(fieldnames))
    if leaked:
        raise SchemaError(
            f"{path}: decision file carries ground-truth field(s): {', '.join(leaked)}. "
            "The running system must not know the experiment's truth."
        )

    missing = [column for column in DECISION_REQUIRED_COLUMNS if column not in fieldnames]
    if missing:
        raise SchemaError(f"{path}: missing required column(s): {', '.join(missing)}")

    decisions: list[Decision] = []
    for index, row in enumerate(rows, start=2):
        where = f"{path}:{index}"
        for field in DECISION_REQUIRED_COLUMNS:
            _require_non_empty(row.get(field, ""), where=where, field=field)

        label = row["decision_label"]
        if label not in DECISION_LABELS:
            raise SchemaError(
                f"{where}: unknown decision_label {label!r}; "
                f"allowed: {sorted(DECISION_LABELS)}"
            )
        decision_source = row["decision_source"]
        if decision_source not in DECISION_SOURCES:
            raise SchemaError(f"{where}: unknown decision_source {decision_source!r}")

        try:
            confidence = float(row["confidence"])
        except ValueError as exc:
            raise SchemaError(f"{where}: confidence is not a number") from exc
        if not 0.0 <= confidence <= 1.0:
            raise SchemaError(f"{where}: confidence must be within [0, 1]")

        start = _parse_ts(row["window_start"], where=where, field="window_start")
        end = _parse_ts(row["window_end"], where=where, field="window_end")
        if end <= start:
            raise SchemaError(f"{where}: window_end must be after window_start")

        decisions.append(
            Decision(
                window_id=row["window_id"],
                node_id=row["node_id"],
                start=start,
                end=end,
                decision_label=label,
                decision_source=decision_source,
                confidence=confidence,
                row=index,
            )
        )

    return decisions


def load_manifest(path: Path) -> dict[str, Any]:
    import json

    if not path.is_file():
        raise SchemaError(f"missing manifest: {path}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SchemaError(f"{path}: not valid JSON ({exc})") from exc
    if not isinstance(data, dict):
        raise SchemaError(f"{path}: manifest must be a JSON object")

    for field in ("exp_id", "input_mode", "fault_taxonomy_version", "status"):
        _require_non_empty(str(data.get(field, "")), where=str(path), field=field)

    allowed_input_modes = {"on_device", "offline_replay", "synthetic_fixture"}
    if data["input_mode"] not in allowed_input_modes:
        raise SchemaError(
            f"{path}: input_mode {data['input_mode']!r} not in {sorted(allowed_input_modes)}"
        )
    return data
