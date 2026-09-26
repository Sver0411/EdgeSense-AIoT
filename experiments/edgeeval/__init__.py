"""EdgeSense offline evaluation.

This package is deliberately **not** importable from the runtime. Firmware cannot
use it, the backend must not depend on it, and it is the only place where ground
truth and system decisions are ever brought together.
"""

from .schema import (
    DECISION_LABELS,
    EXCLUDED_FROM_PRIMARY,
    PRIMARY_RQ_CATEGORIES,
    TRUTH_CATEGORIES,
    Decision,
    SchemaError,
    TruthEpisode,
    load_decisions,
    load_manifest,
    load_truth,
)

__all__ = [
    "DECISION_LABELS",
    "EXCLUDED_FROM_PRIMARY",
    "PRIMARY_RQ_CATEGORIES",
    "TRUTH_CATEGORIES",
    "Decision",
    "SchemaError",
    "TruthEpisode",
    "load_decisions",
    "load_manifest",
    "load_truth",
]
