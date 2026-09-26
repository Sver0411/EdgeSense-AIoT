#!/usr/bin/env python3
"""Run every check that does not need hardware, and report one verdict.

The point of a single entry point is that "it passes" means one specific thing.
Any check that cannot run is reported as SKIP, never as PASS: a skipped check and a
passing check look equally green in a log if you let them, which is how a
verification quietly stops verifying.

Checks:
  1. vendor integrity     upstream sources unchanged, vendored copies byte-identical
  2. config parity        generated header matches the YAML, B/C symmetric, pins match
  3. host tests           portable C units, compiled with -Wall -Wextra -Werror
  4. python tests         evaluation framework, schema rules and metric refusals
  5. EXP-000              the framework self-check produces its hand-derived numbers
  6. static checks        forbidden paths, secrets, truth leakage, TODO classes

Nothing here touches a device. Hardware validation is a separate, manual procedure
(see docs/hardware/wiring.md) and is deliberately absent from this script.

Usage:
    python3 scripts/check_all.py
    python3 scripts/check_all.py --quiet     # only failures and the summary
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VENV_PY = REPO_ROOT / ".venv" / "bin" / "python"


@dataclass
class Check:
    name: str
    command: list[str]
    cwd: Path
    skip_reason: str | None = None


def build_checks() -> list[Check]:
    py = str(VENV_PY) if VENV_PY.is_file() else sys.executable
    venv_missing = None if VENV_PY.is_file() else "no .venv (run: python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt)"

    return [
        Check(
            name="vendor integrity",
            command=[py, "scripts/vendor_manifest.py", "--verify"],
            cwd=REPO_ROOT,
        ),
        Check(
            name="config parity",
            command=[py, "scripts/check_config_parity.py"],
            cwd=REPO_ROOT,
            skip_reason=venv_missing,
        ),
        Check(
            name="host C tests",
            command=["bash", "scripts/test_host.sh"],
            cwd=REPO_ROOT,
        ),
        Check(
            name="python tests",
            command=[py, "-m", "pytest"],
            cwd=REPO_ROOT,
            skip_reason=venv_missing,
        ),
        Check(
            name="EXP-000 self-check",
            command=[py, "experiments/analyze_experiment.py", "--exp", "experiments/EXP-000"],
            cwd=REPO_ROOT,
            skip_reason=venv_missing,
        ),
        Check(
            name="static checks",
            command=[py, "scripts/static_checks.py"],
            cwd=REPO_ROOT,
            skip_reason=venv_missing,
        ),
    ]


def run(check: Check, quiet: bool) -> str:
    if check.skip_reason:
        print(f"[ SKIP ] {check.name}  —  {check.skip_reason}")
        return "skip"

    if not quiet:
        print(f"[ .... ] {check.name}")
    try:
        done = subprocess.run(
            check.command,
            cwd=str(check.cwd),
            capture_output=True,
            text=True,
            timeout=1800,
        )
    except subprocess.TimeoutExpired:
        print(f"[ FAIL ] {check.name}  —  timed out")
        return "fail"
    except OSError as exc:
        print(f"[ FAIL ] {check.name}  —  could not run: {exc}")
        return "fail"

    output = (done.stdout or "") + (done.stderr or "")
    if done.returncode == 0:
        summary = _last_meaningful_line(output)
        print(f"[ PASS ] {check.name}" + (f"  —  {summary}" if summary else ""))
        if not quiet:
            for line in output.strip().splitlines():
                print(f"         {line}")
        return "pass"

    print(f"[ FAIL ] {check.name}  —  exit {done.returncode}")
    for line in output.strip().splitlines()[-40:]:
        print(f"         {line}")
    return "fail"


def _last_meaningful_line(output: str) -> str:
    for line in reversed(output.strip().splitlines()):
        stripped = line.strip()
        if stripped and not stripped.startswith("["):
            return stripped[:120]
    return ""


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--quiet", action="store_true", help="print only failures and the summary")
    args = parser.parse_args(argv)

    print("EdgeSense check_all — every check that does not need hardware")
    print(f"repo: {REPO_ROOT}")
    print()

    results: dict[str, str] = {}
    for check in build_checks():
        results[check.name] = run(check, args.quiet)
        print()

    passed = sum(1 for value in results.values() if value == "pass")
    failed = sum(1 for value in results.values() if value == "fail")
    skipped = sum(1 for value in results.values() if value == "skip")

    print("-----------------------------------------------")
    print(f"passed {passed}  failed {failed}  skipped {skipped}")
    if skipped:
        print("SKIP is not PASS: the checks above did not run.")
    if failed:
        print("failed: " + ", ".join(name for name, value in results.items() if value == "fail"))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
