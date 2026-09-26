"""Host tests for the offline evaluation framework.

Three things are checked here, and the third matters most:

1. the analysis computes correct numbers on a fixture whose answers were derived by
   hand (experiments/EXP-000);
2. the output is deterministic, so a diff between two runs means something;
3. the analysis **refuses** to report a metric whose denominator has no ground
   truth, and refuses a decision file that carries ground truth. Those two refusals
   are the guard against the failure mode that motivated this framework: an
   event-to-fault rate computed from a dataset containing no events.
"""

from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

import pytest

from analyze_experiment import main as analyze_main
from edgeeval.metrics import (
    MissingTruthCategory,
    compute_primary_metrics,
    match_windows,
    proportion,
    wilson_interval,
)
from edgeeval.schema import SchemaError, load_decisions, load_truth

REPO_ROOT = Path(__file__).resolve().parents[2]
EXP000 = REPO_ROOT / "experiments" / "EXP-000"

TRUTH_HEADER = (
    "episode_id,truth_category,event_source,family,channels,nodes,start_ts,end_ts,"
    "scenario,physical_or_injected,ground_truth_source,injection_method,"
    "intervention_description,notes\n"
)
DECISION_HEADER = (
    "window_id,node_id,window_start,window_end,decision_label,decision_source,"
    "confidence,reason_codes,evidence_ref\n"
)

MINIMAL_MANIFEST = {
    "exp_id": "EXP-TEST",
    "input_mode": "synthetic_fixture",
    "fault_taxonomy_version": "phase-0.2",
    "status": "complete",
}


def make_exp(root: Path, truth_rows: list[str], decision_rows: list[str]) -> Path:
    (root / "truth").mkdir(parents=True, exist_ok=True)
    (root / "results").mkdir(parents=True, exist_ok=True)
    (root / "manifest.json").write_text(json.dumps(MINIMAL_MANIFEST), encoding="utf-8")
    (root / "truth" / "episodes.csv").write_text(TRUTH_HEADER + "".join(truth_rows), encoding="utf-8")
    (root / "results" / "decisions.csv").write_text(
        DECISION_HEADER + "".join(decision_rows), encoding="utf-8"
    )
    return root


def fault_row(episode_id: str, start: str, end: str, family: str = "SPIKE") -> str:
    return (
        f"{episode_id},sensor_fault,sensing,{family},temperature,B,{start},{end},"
        f"fault,injected,injection_plan,amplitude_10C,,fixture\n"
    )


def event_row(episode_id: str, start: str, end: str, family: str = "area_light_on") -> str:
    return (
        f"{episode_id},shared_event,environment,{family},light,B|C,{start},{end},"
        f"event,physical,physical_intervention_log,,room light on,fixture\n"
    )


def decision_row(window_id: str, start: str, end: str, label: str) -> str:
    return f"{window_id},B,{start},{end},{label},gateway_fused,0.9,reason,ref\n"


# ---------------------------------------------------------------------------
# 1. correctness against hand-derived answers
# ---------------------------------------------------------------------------


def test_exp000_produces_the_hand_derived_numbers():
    assert analyze_main(["--exp", str(EXP000)]) == 0
    payload = json.loads((EXP000 / "results" / "analysis" / "metrics.json").read_text(encoding="utf-8"))

    assert payload["windows"]["total"] == 20
    assert payload["windows"]["in_primary_analysis"] == 20
    assert payload["windows"]["excluded"] == 0
    assert payload["windows"]["by_truth_category"] == {
        "normal": 10,
        "sensor_fault": 5,
        "shared_event": 5,
    }

    primary = payload["primary_rq"]
    assert primary["fault_to_event"]["value"] == pytest.approx(0.4)
    assert primary["fault_to_event"]["numerator"] == 2
    assert primary["fault_to_event"]["denominator"] == 5
    assert primary["event_to_fault"]["value"] == pytest.approx(0.2)
    assert primary["event_to_fault"]["numerator"] == 1
    assert primary["shared_event_recall"]["value"] == pytest.approx(0.8)
    assert primary["false_alarm_rate"]["value"] == pytest.approx(0.2)


def test_exp000_confusion_matrix_matches_a_manual_count():
    assert analyze_main(["--exp", str(EXP000)]) == 0
    payload = json.loads((EXP000 / "results" / "analysis" / "metrics.json").read_text(encoding="utf-8"))
    assert payload["confusion_matrix"] == {
        "sensor_fault": {"normal": 0, "sensor_fault": 3, "shared_event": 2},
        "shared_event": {"normal": 0, "sensor_fault": 1, "shared_event": 4},
        "normal": {"normal": 8, "sensor_fault": 0, "shared_event": 2},
    }


def test_exp000_per_class_metrics_are_consistent_with_the_matrix():
    assert analyze_main(["--exp", str(EXP000)]) == 0
    payload = json.loads((EXP000 / "results" / "analysis" / "metrics.json").read_text(encoding="utf-8"))
    per_class = payload["per_class"]

    # sensor_fault: predicted 3+1=4, of which 3 correct
    assert per_class["sensor_fault"]["predicted"] == 4
    assert per_class["sensor_fault"]["true_positive"] == 3
    assert per_class["sensor_fault"]["precision"]["value"] == pytest.approx(0.75)
    assert per_class["sensor_fault"]["recall"]["value"] == pytest.approx(0.6)
    assert per_class["sensor_fault"]["f1"] == pytest.approx(2 * 0.75 * 0.6 / 1.35)

    # shared_event: predicted 2+4+2=8, of which 4 correct
    assert per_class["shared_event"]["predicted"] == 8
    assert per_class["shared_event"]["precision"]["value"] == pytest.approx(0.5)

    # normal: predicted 8, all correct
    assert per_class["normal"]["precision"]["value"] == pytest.approx(1.0)
    assert per_class["normal"]["recall"]["value"] == pytest.approx(0.8)


def test_exp000_reports_per_scenario_and_per_family():
    assert analyze_main(["--exp", str(EXP000)]) == 0
    payload = json.loads((EXP000 / "results" / "analysis" / "metrics.json").read_text(encoding="utf-8"))

    scenarios = {entry["scenario"] for entry in payload["per_scenario"]}
    assert {"fixture_fault", "fixture_shared_event"} <= scenarios

    families = {(entry["truth_category"], entry["family"]) for entry in payload["per_family"]}
    assert ("sensor_fault", "SPIKE") in families
    assert ("shared_event", "area_light_on") in families


def test_exp000_report_is_written_and_says_no_threshold_applies():
    assert analyze_main(["--exp", str(EXP000)]) == 0
    report = (EXP000 / "results" / "analysis" / "report.md").read_text(encoding="utf-8")
    assert "No threshold is applied and none is implied" in report
    assert "fault -> event rate" in report
    assert "Confusion matrix" in report


def test_analysis_output_is_deterministic():
    assert analyze_main(["--exp", str(EXP000)]) == 0
    first = (EXP000 / "results" / "analysis" / "metrics.json").read_bytes()
    assert analyze_main(["--exp", str(EXP000)]) == 0
    second = (EXP000 / "results" / "analysis" / "metrics.json").read_bytes()
    assert first == second, "the same inputs must produce byte-identical output"


# ---------------------------------------------------------------------------
# 2. the refusals
# ---------------------------------------------------------------------------


def test_zero_denominator_for_a_required_category_is_a_hard_error(tmp_path: Path):
    """The exact failure this framework exists to prevent.

    Five fault windows and no event windows at all: an event-to-fault rate would
    have a denominator of zero. It must stop the run, not print nan.
    """
    exp = make_exp(
        tmp_path / "exp",
        [fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z")],
        [decision_row("W1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z", "sensor_fault")],
    )
    assert analyze_main(["--exp", str(exp)]) == 1


def test_missing_category_error_names_the_category_and_the_counts(tmp_path: Path):
    exp = make_exp(
        tmp_path / "exp",
        [fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z")],
        [decision_row("W1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z", "sensor_fault")],
    )
    episodes = load_truth(exp / "truth" / "episodes.csv")
    decisions = load_decisions(exp / "results" / "decisions.csv")
    pairs = match_windows(decisions, episodes)
    with pytest.raises(MissingTruthCategory) as excinfo:
        compute_primary_metrics(pairs, ("sensor_fault", "shared_event", "normal"))
    message = str(excinfo.value)
    assert "shared_event" in message
    assert "denominator" in message


def test_decision_file_carrying_ground_truth_is_rejected(tmp_path: Path):
    exp = make_exp(
        tmp_path / "exp",
        [fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z")],
        [],
    )
    # A decision file that knows the truth is not a decision file.
    (exp / "results" / "decisions.csv").write_text(
        DECISION_HEADER.rstrip("\n") + ",truth_category\n"
        "W1,B,2026-01-01T00:00:00Z,2026-01-01T00:01:00Z,normal,node_only,0.5,,,sensor_fault\n",
        encoding="utf-8",
    )
    with pytest.raises(SchemaError) as excinfo:
        load_decisions(exp / "results" / "decisions.csv")
    assert "truth_category" in str(excinfo.value)
    assert analyze_main(["--exp", str(exp)]) == 1


def test_unknown_decision_label_is_rejected(tmp_path: Path):
    """The system answers three things. A fourth answer is a schema violation."""
    exp = make_exp(
        tmp_path / "exp",
        [fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z")],
        [decision_row("W1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z", "localized_event")],
    )
    with pytest.raises(SchemaError):
        load_decisions(exp / "results" / "decisions.csv")


def test_truth_schema_rules(tmp_path: Path):
    base = tmp_path / "exp"

    # end before start
    exp = make_exp(base / "a", [fault_row("E1", "2026-01-01T00:05:00Z", "2026-01-01T00:01:00Z")], [])
    with pytest.raises(SchemaError):
        load_truth(exp / "truth" / "episodes.csv")

    # unknown category
    exp = make_exp(
        base / "b",
        ["E1,mystery,sensing,SPIKE,temperature,B,2026-01-01T00:00:00Z,"
         "2026-01-01T00:01:00Z,fault,injected,injection_plan,amp,,\n"],
        [],
    )
    with pytest.raises(SchemaError):
        load_truth(exp / "truth" / "episodes.csv")

    # duplicate episode_id
    exp = make_exp(
        base / "c",
        [
            fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z"),
            fault_row("E1", "2026-01-01T00:10:00Z", "2026-01-01T00:11:00Z"),
        ],
        [],
    )
    with pytest.raises(SchemaError):
        load_truth(exp / "truth" / "episodes.csv")

    # an injected episode must say how it was injected
    exp = make_exp(
        base / "d",
        ["E1,sensor_fault,sensing,SPIKE,temperature,B,2026-01-01T00:00:00Z,"
         "2026-01-01T00:01:00Z,fault,injected,injection_plan,,,\n"],
        [],
    )
    with pytest.raises(SchemaError):
        load_truth(exp / "truth" / "episodes.csv")

    # a physical episode must describe the intervention
    exp = make_exp(
        base / "e",
        ["E1,shared_event,environment,area_light_on,light,B|C,2026-01-01T00:00:00Z,"
         "2026-01-01T00:01:00Z,event,physical,physical_intervention_log,,,\n"],
        [],
    )
    with pytest.raises(SchemaError):
        load_truth(exp / "truth" / "episodes.csv")

    # an unexpected column must be added deliberately, not by accident
    exp = make_exp(base / "f", [fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z")], [])
    path = exp / "truth" / "episodes.csv"
    path.write_text(TRUTH_HEADER.rstrip("\n") + ",surprise\n" + fault_row(
        "E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z"
    ).rstrip("\n") + ",x\n", encoding="utf-8")
    with pytest.raises(SchemaError):
        load_truth(path)


def test_manifest_requires_the_fields_that_make_a_run_traceable(tmp_path: Path):
    exp = tmp_path / "exp"
    exp.mkdir()
    (exp / "manifest.json").write_text(json.dumps({"exp_id": "EXP-X"}), encoding="utf-8")
    from edgeeval.schema import load_manifest

    with pytest.raises(SchemaError):
        load_manifest(exp / "manifest.json")


# ---------------------------------------------------------------------------
# 3. matching and interval behaviour
# ---------------------------------------------------------------------------


def test_window_overlapping_nothing_is_normal(tmp_path: Path):
    exp = make_exp(
        tmp_path / "exp",
        [fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z")],
        [decision_row("W9", "2026-01-01T05:00:00Z", "2026-01-01T05:01:00Z", "normal")],
    )
    pairs = match_windows(
        load_decisions(exp / "results" / "decisions.csv"), load_truth(exp / "truth" / "episodes.csv")
    )
    assert len(pairs) == 1
    assert pairs[0].truth_category == "normal"
    assert pairs[0].matched_episode_id is None


def test_largest_overlap_wins_and_ties_break_deterministically(tmp_path: Path):
    exp = make_exp(
        tmp_path / "exp",
        [
            fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:00:10Z"),
            event_row("E2", "2026-01-01T00:00:10Z", "2026-01-01T00:01:00Z"),
        ],
        [decision_row("W1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z", "normal")],
    )
    pairs = match_windows(
        load_decisions(exp / "results" / "decisions.csv"), load_truth(exp / "truth" / "episodes.csv")
    )
    # E2 covers 50 s of the window, E1 only 10 s.
    assert pairs[0].matched_episode_id == "E2"


def test_excluded_categories_stay_out_of_the_primary_metrics(tmp_path: Path):
    """localized_event is real truth, but the primary comparison does not use it."""
    exp = make_exp(
        tmp_path / "exp",
        [
            fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z"),
            event_row("E2", "2026-01-01T00:10:00Z", "2026-01-01T00:11:00Z"),
            "E3,localized_event,environment,breath_on_sensor,humidity,B,"
            "2026-01-01T00:20:00Z,2026-01-01T00:21:00Z,localized,physical,"
            "physical_intervention_log,,breath at 20 cm,fixture\n",
        ],
        [
            decision_row("W1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z", "sensor_fault"),
            decision_row("W2", "2026-01-01T00:10:00Z", "2026-01-01T00:11:00Z", "shared_event"),
            decision_row("W3", "2026-01-01T00:20:00Z", "2026-01-01T00:21:00Z", "normal"),
            decision_row("W4", "2026-01-01T01:00:00Z", "2026-01-01T01:01:00Z", "normal"),
        ],
    )
    pairs = match_windows(
        load_decisions(exp / "results" / "decisions.csv"), load_truth(exp / "truth" / "episodes.csv")
    )
    metrics = compute_primary_metrics(pairs, ("sensor_fault", "shared_event", "normal"))
    assert metrics.windows_total == 4
    assert metrics.windows_in_primary == 3
    assert metrics.windows_excluded == 1
    assert metrics.excluded_by_category == {"localized_event": 1}


def test_breakdown_without_a_denominator_is_reported_as_not_defined_not_zero(tmp_path: Path):
    """A scenario with no fault windows has no fault->event rate. It is undefined."""
    exp = make_exp(
        tmp_path / "exp",
        [event_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z")],
        [
            decision_row("W1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z", "shared_event"),
            decision_row("W2", "2026-01-01T01:00:00Z", "2026-01-01T01:01:00Z", "normal"),
        ],
    )
    pairs = match_windows(
        load_decisions(exp / "results" / "decisions.csv"), load_truth(exp / "truth" / "episodes.csv")
    )
    # The top level fails, because these categories were declared required.
    with pytest.raises(MissingTruthCategory):
        compute_primary_metrics(pairs, ("sensor_fault", "shared_event", "normal"))

    # Relaxing the requirement makes the run defined, and the fault metric is then
    # explicitly undefined rather than zero.
    metrics = compute_primary_metrics(pairs, ("shared_event", "normal"))
    assert metrics.fault_to_event.defined is False
    assert metrics.fault_to_event.denominator == 0
    assert metrics.fault_to_event.as_dict()["value"] is None
    assert metrics.shared_event_recall.value == pytest.approx(1.0)
    assert metrics.false_alarm_rate.value == pytest.approx(0.0)


def test_wilson_interval_stays_inside_zero_and_one():
    low, high = wilson_interval(0, 10)
    assert low == pytest.approx(0.0)
    assert 0.2 < high < 0.35

    low, high = wilson_interval(10, 10)
    assert high == pytest.approx(1.0)
    assert low < 0.8

    low, high = wilson_interval(0, 0)
    assert low is None and high is None


def test_proportion_without_a_denominator_is_undefined():
    entry = proportion(0, 0)
    assert entry.defined is False
    assert entry.value is None
    payload = entry.as_dict()
    assert payload["defined"] is False
    assert payload["value"] is None


def test_metrics_json_contains_no_wall_clock_timestamp():
    """A timestamp in the output would break run-to-run comparison."""
    assert analyze_main(["--exp", str(EXP000)]) == 0
    payload = json.loads((EXP000 / "results" / "analysis" / "metrics.json").read_text(encoding="utf-8"))
    text = json.dumps(payload)
    for forbidden in ("generated_at", "timestamp", "2026-09", "2026-10"):
        assert forbidden not in text


def test_truth_file_with_a_byte_order_mark_still_loads(tmp_path: Path):
    """Hand-authored CSVs often carry a BOM. It is not a schema error."""
    exp = make_exp(tmp_path / "exp", [fault_row("E1", "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z")], [])
    path = exp / "truth" / "episodes.csv"
    payload = path.read_bytes()
    path.write_bytes(b"\xef\xbb\xbf" + payload)
    episodes = load_truth(path)
    assert len(episodes) == 1
    assert episodes[0].episode_id == "E1"


def test_analysis_does_not_import_anything_from_the_runtime_side():
    """The dependency direction is one-way.

    The evaluation package reads firmware-produced artifacts as data. It must not
    import firmware or backend code, and nothing on the runtime side may import it —
    that is what keeps ground truth out of the running system.
    """
    package_dir = REPO_ROOT / "experiments" / "edgeeval"
    for path in sorted(package_dir.glob("*.py")):
        source = path.read_text(encoding="utf-8")
        assert "from backend" not in source, path.name
        assert "import backend" not in source, path.name
        assert "from firmware" not in source, path.name
        assert "import firmware" not in source, path.name

    # And nothing under firmware/ mentions the evaluation package.
    for path in sorted((REPO_ROOT / "firmware").rglob("*.[ch]")):
        source = path.read_text(encoding="utf-8", errors="replace")
        assert "edgeeval" not in source, path
