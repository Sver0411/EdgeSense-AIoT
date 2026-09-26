"""Metric computation for EdgeSense experiments.

The rule this module exists to enforce: **every reported number has a denominator
with real ground truth behind it.**

A metric whose denominator is empty is not zero and is not `nan` — it is
undefined. The earlier failure this guards against was an experiment reporting an
event-to-fault rate computed from a dataset that contained no events at all; the
number looked plausible and meant nothing. Here:

* a *required* category with no windows raises :class:`MissingTruthCategory`, which
  stops the analysis with a non-zero exit status;
* an *optional* breakdown (one scenario, one fault family) with no windows is
  reported as ``defined=False`` so it is visibly "not measured" rather than zero.

Proportions carry a Wilson score interval, because a rate with a handful of
episodes behind it is not a rate that should be quoted bare.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Iterable, Sequence

from .schema import EXCLUDED_FROM_PRIMARY, Decision, TruthEpisode

PRIMARY_LABELS: tuple[str, ...] = ("normal", "sensor_fault", "shared_event")

NORMAL_LABEL = "normal"

Z_95 = 1.959963984540054


class MissingTruthCategory(Exception):
    """A required truth category has no windows. The analysis cannot be defined."""


@dataclass(frozen=True)
class Proportion:
    """A rate with its denominator and a Wilson interval attached."""

    numerator: int
    denominator: int
    value: float | None

    @property
    def defined(self) -> bool:
        return self.value is not None

    def as_dict(self) -> dict[str, object]:
        low, high = wilson_interval(self.numerator, self.denominator)
        return {
            "numerator": self.numerator,
            "denominator": self.denominator,
            "value": _round(self.value),
            "ci95_low": _round(low),
            "ci95_high": _round(high),
            "defined": self.defined,
        }


def _round(value: float | None) -> float | None:
    return None if value is None else round(value, 6)


def proportion(numerator: int, denominator: int) -> Proportion:
    if denominator <= 0:
        return Proportion(numerator=numerator, denominator=denominator, value=None)
    return Proportion(numerator=numerator, denominator=denominator, value=numerator / denominator)


def wilson_interval(k: int, n: int, z: float = Z_95) -> tuple[float | None, float | None]:
    """Wilson score interval for a binomial proportion.

    Preferred over the normal approximation because several of these denominators
    are small, which is exactly where the normal approximation misbehaves.
    """
    if n <= 0:
        return (None, None)
    phat = k / n
    denominator = 1.0 + (z * z) / n
    centre = (phat + (z * z) / (2.0 * n)) / denominator
    margin = (z / denominator) * math.sqrt(phat * (1.0 - phat) / n + (z * z) / (4.0 * n * n))
    return (max(0.0, centre - margin), min(1.0, centre + margin))


def _overlap_seconds(a_start, a_end, b_start, b_end) -> float:
    start = max(a_start, b_start)
    end = min(a_end, b_end)
    return max(0.0, (end - start).total_seconds())


@dataclass(frozen=True)
class MatchedWindow:
    window: Decision
    truth_category: str
    matched_episode_id: str | None
    scenario: str
    family: str
    overlap_s: float


def match_windows(
    decisions: Iterable[Decision], episodes: Sequence[TruthEpisode]
) -> list[MatchedWindow]:
    """Assign each decision window the ground truth it overlaps.

    A window that overlaps nothing is `normal` — that is the definition of a false
    alarm here, not a missing label. Where several episodes overlap, the largest
    overlap wins; ties break on `episode_id` so the result does not depend on file
    order.
    """
    matched: list[MatchedWindow] = []
    for decision in decisions:
        best: TruthEpisode | None = None
        best_overlap = 0.0
        for episode in episodes:
            overlap = _overlap_seconds(decision.start, decision.end, episode.start, episode.end)
            if overlap <= 0.0:
                continue
            if overlap > best_overlap or (
                overlap == best_overlap
                and best is not None
                and episode.episode_id < best.episode_id
            ):
                best = episode
                best_overlap = overlap

        if best is None:
            matched.append(
                MatchedWindow(
                    window=decision,
                    truth_category=NORMAL_LABEL,
                    matched_episode_id=None,
                    scenario=NORMAL_LABEL,
                    family=NORMAL_LABEL,
                    overlap_s=0.0,
                )
            )
        else:
            matched.append(
                MatchedWindow(
                    window=decision,
                    truth_category=best.truth_category,
                    matched_episode_id=best.episode_id,
                    scenario=best.scenario,
                    family=best.family,
                    overlap_s=best_overlap,
                )
            )
    return matched


def confusion_matrix(pairs: Sequence[MatchedWindow]) -> dict[str, dict[str, int]]:
    """truth -> decision -> count, with every primary label present even at zero.

    Present-but-zero matters: an absent row would make a missing class look like a
    formatting detail instead of the empty denominator it is.
    """
    matrix: dict[str, dict[str, int]] = {
        truth: {decision: 0 for decision in PRIMARY_LABELS} for truth in PRIMARY_LABELS
    }
    for pair in pairs:
        truth = pair.truth_category
        decision = pair.window.decision_label
        matrix.setdefault(truth, {label: 0 for label in PRIMARY_LABELS})
        matrix[truth][decision] = matrix[truth].get(decision, 0) + 1
    return matrix


@dataclass(frozen=True)
class ClassMetrics:
    label: str
    support: int
    predicted: int
    true_positive: int
    precision: Proportion
    recall: Proportion
    f1: float | None


def class_metrics(pairs: Sequence[MatchedWindow], label: str) -> ClassMetrics:
    support = sum(1 for pair in pairs if pair.truth_category == label)
    predicted = sum(1 for pair in pairs if pair.window.decision_label == label)
    true_positive = sum(
        1 for pair in pairs if pair.truth_category == label and pair.window.decision_label == label
    )
    precision = proportion(true_positive, predicted)
    recall = proportion(true_positive, support)
    f1: float | None = None
    if precision.value is not None and recall.value is not None:
        if precision.value + recall.value > 0.0:
            f1 = 2.0 * precision.value * recall.value / (precision.value + recall.value)
        else:
            f1 = 0.0
    return ClassMetrics(
        label=label,
        support=support,
        predicted=predicted,
        true_positive=true_positive,
        precision=precision,
        recall=recall,
        f1=f1,
    )


@dataclass
class ScenarioMetrics:
    scenario: str
    windows: int
    fault_to_event: Proportion
    event_to_fault: Proportion
    shared_event_recall: Proportion
    false_alarm_rate: Proportion


@dataclass
class FamilyBreakdown:
    """fault family (or event family) -> what the system decided."""

    family: str
    truth_category: str
    windows: int
    decisions: dict[str, int] = field(default_factory=dict)


@dataclass
class PrimaryMetrics:
    windows_total: int
    windows_in_primary: int
    windows_excluded: int
    excluded_by_category: dict[str, int]
    confusion: dict[str, dict[str, int]]
    fault_to_event: Proportion
    event_to_fault: Proportion
    shared_event_recall: Proportion
    false_alarm_rate: Proportion
    per_class: dict[str, ClassMetrics]
    per_scenario: list[ScenarioMetrics]
    per_family: list[FamilyBreakdown]
    counts: dict[str, int]


def _safe_rate(pairs: Sequence[MatchedWindow], numerator_label: str, denominator_truth: str) -> Proportion:
    denominator = sum(1 for pair in pairs if pair.truth_category == denominator_truth)
    numerator = sum(
        1
        for pair in pairs
        if pair.truth_category == denominator_truth and pair.window.decision_label == numerator_label
    )
    return proportion(numerator, denominator)


def _scenario_metrics(pairs: Sequence[MatchedWindow], scenario: str) -> ScenarioMetrics:
    subset = [pair for pair in pairs if pair.scenario == scenario]
    false_alarms = sum(
        1
        for pair in subset
        if pair.truth_category == NORMAL_LABEL and pair.window.decision_label != NORMAL_LABEL
    )
    normal_windows = sum(1 for pair in subset if pair.truth_category == NORMAL_LABEL)
    return ScenarioMetrics(
        scenario=scenario,
        windows=len(subset),
        fault_to_event=_safe_rate(subset, "shared_event", "sensor_fault"),
        event_to_fault=_safe_rate(subset, "sensor_fault", "shared_event"),
        shared_event_recall=_safe_rate(subset, "shared_event", "shared_event"),
        false_alarm_rate=proportion(false_alarms, normal_windows),
    )


def _family_breakdown(pairs: Sequence[MatchedWindow]) -> list[FamilyBreakdown]:
    by_key: dict[tuple[str, str], FamilyBreakdown] = {}
    for pair in pairs:
        if pair.truth_category == NORMAL_LABEL:
            continue
        key = (pair.truth_category, pair.family)
        entry = by_key.get(key)
        if entry is None:
            entry = FamilyBreakdown(family=pair.family, truth_category=pair.truth_category, windows=0)
            by_key[key] = entry
        entry.windows += 1
        entry.decisions[pair.window.decision_label] = (
            entry.decisions.get(pair.window.decision_label, 0) + 1
        )
    return [
        by_key[key]
        for key in sorted(by_key, key=lambda item: (item[0], item[1]))
    ]


def compute_primary_metrics(
    pairs: Sequence[MatchedWindow], required_categories: Sequence[str]
) -> PrimaryMetrics:
    """Compute the primary comparison, refusing to invent an undefined rate.

    `required_categories` are the truth classes whose presence the caller asserts.
    If one of them has no windows, the whole analysis stops: a metric without its
    denominator is not a weak result, it is not a result.
    """
    primary = [pair for pair in pairs if pair.truth_category not in EXCLUDED_FROM_PRIMARY]
    excluded = [pair for pair in pairs if pair.truth_category in EXCLUDED_FROM_PRIMARY]

    counts = {
        label: sum(1 for pair in primary if pair.truth_category == label) for label in PRIMARY_LABELS
    }

    missing = [
        category
        for category in required_categories
        if category not in PRIMARY_LABELS or counts.get(category, 0) == 0
    ]
    if missing:
        present = ", ".join(f"{label}={counts.get(label, 0)}" for label in PRIMARY_LABELS)
        raise MissingTruthCategory(
            "no windows with ground truth for: "
            + ", ".join(missing)
            + f" (present: {present}). "
            + "A metric whose denominator has no ground truth behind it is undefined, "
            + "not zero; fix the experiment or drop the metric."
        )

    excluded_by_category: dict[str, int] = {}
    for pair in excluded:
        excluded_by_category[pair.truth_category] = excluded_by_category.get(pair.truth_category, 0) + 1

    scenarios = sorted({pair.scenario for pair in primary})

    false_alarms = sum(
        1
        for pair in primary
        if pair.truth_category == NORMAL_LABEL and pair.window.decision_label != NORMAL_LABEL
    )

    return PrimaryMetrics(
        windows_total=len(pairs),
        windows_in_primary=len(primary),
        windows_excluded=len(excluded),
        excluded_by_category=dict(sorted(excluded_by_category.items())),
        confusion=confusion_matrix(primary),
        fault_to_event=_safe_rate(primary, "shared_event", "sensor_fault"),
        event_to_fault=_safe_rate(primary, "sensor_fault", "shared_event"),
        shared_event_recall=_safe_rate(primary, "shared_event", "shared_event"),
        false_alarm_rate=proportion(false_alarms, counts.get(NORMAL_LABEL, 0)),
        per_class={label: class_metrics(primary, label) for label in PRIMARY_LABELS},
        per_scenario=[_scenario_metrics(primary, scenario) for scenario in scenarios],
        per_family=_family_breakdown(primary),
        counts=counts,
    )
