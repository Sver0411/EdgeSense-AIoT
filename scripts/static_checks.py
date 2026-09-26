#!/usr/bin/env python3
"""Static checks for the things a compiler and a test suite cannot see.

Each check here exists because of a specific way this project could quietly go
wrong:

  absolute paths      a machine-specific path committed as if it were portable
  truth leakage       ground-truth vocabulary appearing in firmware, which would
                      mean the running system could know the answer
  secrets             a credential in the tree
  TODO classes        an unclassified TODO, which is how "we will fix it" becomes
                      "nobody knows if this still matters"
  stale names         references to a project this one replaced
  build artifacts     a build/ directory that got committed

Fails on the first four. Reports the rest without failing, because they are
hygiene rather than correctness.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

SOURCE_SUFFIXES = {".c", ".h", ".py", ".sh", ".yaml", ".yml", ".json", ".ini", ".cmake", ".txt"}

# Documentation legitimately quotes machine paths; data files record where an
# upstream checkout lives. Nothing else may.
ABSOLUTE_PATH_ALLOWLIST = {
    "docs/vendored/vendor_sources.json",
}
ABSOLUTE_PATH_SCOPES = ("firmware", "scripts", "experiments", "config", "tests")

# The checkers themselves necessarily contain the patterns they search for — a
# TODO sweep that flags its own regular expression is a check that can never pass,
# which is worse than no check at all. They are excluded from the pattern sweeps
# and remain fully covered by the firmware truth-leakage check and by git.
SELF_EXEMPT = {
    "scripts/static_checks.py",
    "scripts/check_all.py",
}

# Only code carries actionable TODOs. A JSON string that mentions the word is
# documentation, not a task.
TODO_SUFFIXES = {".c", ".h", ".py", ".sh"}

# Vocabulary that belongs to the experiment layer only. Its presence in firmware
# would mean the running system could see the ground truth.
TRUTH_VOCABULARY = (
    "truth_category",
    "ground_truth_source",
    "physical_or_injected",
    "event_category",
    "injection_plan",
)

SECRET_PATTERNS = (
    re.compile(r"(?i)\bapi[_-]?key\s*[:=]\s*['\"][^'\"]{8,}"),
    re.compile(r"(?i)\bpassword\s*[:=]\s*['\"][^'\"]{4,}"),
    re.compile(r"(?i)\bsecret\s*[:=]\s*['\"][^'\"]{8,}"),
    re.compile(r"(?i)\bbearer\s+[A-Za-z0-9\-_.]{20,}"),
    re.compile(r"sk-[A-Za-z0-9]{20,}"),
)

TODO_PATTERN = re.compile(r"\b(TODO|FIXME|XXX)\b")
TODO_CLASSED = re.compile(r"\b(TODO|FIXME|XXX)\((blocker|future|optional)\)")

STALE_NAMES = ("EdgeSense-Fusion", "EdgeSense Fusion")


@dataclass
class Report:
    failures: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def fail(self, message: str) -> None:
        self.failures.append(message)

    def warn(self, message: str) -> None:
        self.warnings.append(message)


def iter_source_files() -> list[Path]:
    files: list[Path] = []
    for scope in ABSOLUTE_PATH_SCOPES:
        base = REPO_ROOT / scope
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file():
                continue
            if "build" in path.parts or "vendor" in path.parts or ".venv" in path.parts:
                continue
            if path.suffix in SOURCE_SUFFIXES:
                files.append(path)
    return files


def check_absolute_paths(report: Report) -> None:
    for path in iter_source_files():
        rel = path.relative_to(REPO_ROOT).as_posix()
        if rel in ABSOLUTE_PATH_ALLOWLIST or rel in SELF_EXEMPT:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), 1):
            if "/Users/" not in line and "/home/" not in line:
                continue
            if "allow-absolute-path" in line:
                continue
            # A docstring that names where an upstream checkout lives is a
            # reference, not a dependency.
            if re.search(r"(?i)(upstream|checkout|pre-existing|records where)", line):
                continue
            report.fail(
                f"absolute path in {rel}:{number} — {line.strip()[:100]!r} "
                f"(add '# allow-absolute-path' with a reason if it is intentional)"
            )


def check_truth_leakage(report: Report) -> None:
    firmware = REPO_ROOT / "firmware"
    if not firmware.is_dir():
        return
    for path in sorted(firmware.rglob("*.[ch]")):
        if "vendor" in path.parts:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for token in TRUTH_VOCABULARY:
            if token in text:
                report.fail(
                    f"truth vocabulary {token!r} appears in {path.relative_to(REPO_ROOT)}: "
                    "the running system must not be able to know the experiment's ground truth"
                )


def check_secrets(report: Report) -> None:
    for path in iter_source_files():
        rel = path.relative_to(REPO_ROOT).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        for pattern in SECRET_PATTERNS:
            match = pattern.search(text)
            if match:
                report.fail(f"possible credential in {rel}: {match.group(0)[:40]!r}")

    if (REPO_ROOT / ".env").is_file():
        report.fail(".env exists in the tree; it must be git-ignored and absent")


def check_todo_classes(report: Report) -> None:
    for path in iter_source_files():
        rel = path.relative_to(REPO_ROOT).as_posix()
        if rel in SELF_EXEMPT or path.suffix not in TODO_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), 1):
            if TODO_PATTERN.search(line) and not TODO_CLASSED.search(line):
                report.warn(
                    f"unclassified TODO/FIXME in {rel}:{number} — "
                    f"write it as TODO(blocker|future|optional): ... — {line.strip()[:80]!r}"
                )


def check_stale_names(report: Report) -> None:
    for path in iter_source_files():
        rel = path.relative_to(REPO_ROOT).as_posix()
        if rel.startswith("docs/PHASE_0") or rel in SELF_EXEMPT:
            continue  # historical records and the checkers legitimately name it
        text = path.read_text(encoding="utf-8", errors="replace")
        for name in STALE_NAMES:
            if name in text:
                report.warn(f"stale project name {name!r} in {rel}")


def check_build_artifacts(report: Report) -> None:
    try:
        done = subprocess.run(
            ["git", "ls-files"], cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=60
        )
    except (OSError, subprocess.SubprocessError):
        report.warn("git not available: cannot check tracked build artifacts")
        return
    if done.returncode != 0:
        report.warn("not a git repository: cannot check tracked build artifacts")
        return

    tracked = [line.strip() for line in done.stdout.splitlines() if line.strip()]
    for rel in tracked:
        if rel.startswith("build/") or "/build/" in rel or rel.endswith("sdkconfig"):
            report.fail(f"build artifact is tracked by git: {rel}")
        if rel.endswith(".bin") or rel.endswith(".elf"):
            report.fail(f"binary artifact is tracked by git: {rel}")


def check_experiment_layout(report: Report) -> None:
    experiments = REPO_ROOT / "experiments"
    if not experiments.is_dir():
        return
    for path in sorted(experiments.iterdir()):
        if not path.is_dir() or not path.name.startswith("EXP-"):
            continue
        if path.name.endswith("-template"):
            continue
        if not (path / "manifest.json").is_file():
            report.warn(f"{path.name}: no manifest.json")
        if not (path / "truth" / "episodes.csv").is_file():
            report.warn(f"{path.name}: no truth/episodes.csv")
        has_input = (path / "results" / "decisions.csv").is_file() or (path / "raw").is_dir()
        if not has_input:
            report.warn(f"{path.name}: neither results/decisions.csv nor raw/ present")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.parse_args(argv)

    report = Report()
    check_absolute_paths(report)
    check_truth_leakage(report)
    check_secrets(report)
    check_todo_classes(report)
    check_stale_names(report)
    check_build_artifacts(report)
    check_experiment_layout(report)

    for message in report.warnings:
        print(f"  warn: {message}")
    for message in report.failures:
        print(f"  FAIL: {message}")

    if report.failures:
        print(f"static checks FAILED — {len(report.failures)} problem(s), {len(report.warnings)} warning(s)")
        return 1

    print(f"static checks passed — 0 problems, {len(report.warnings)} warning(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
