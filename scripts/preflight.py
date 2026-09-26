#!/usr/bin/env python3
"""EdgeSense environment preflight.

Answers one question: **what is missing on this machine right now?**

Design constraints:
  * reports gaps, does NOT fail hard — a missing optional tool is information, not an error
  * uses the standard library only: it must run before any dependency is installed
  * never installs anything and never modifies the machine

Usage:
    python3 scripts/preflight.py             # report, always exit 0
    python3 scripts/preflight.py --strict    # exit 1 if any REQUIRED item is missing

Rationale for the non-failing default: the Phase 0 audit found that several upstream
repos cannot be reproduced on this machine (missing venvs, a broken .venv, absent
PyYAML). A preflight that aborts on the first gap would hide the rest of the picture.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

Status = Literal["ok", "missing", "warn", "info"]

REPO_ROOT = Path(__file__).resolve().parent.parent
VENDOR_SOURCES = REPO_ROOT / "docs" / "vendored" / "vendor_sources.json"
MANIFEST = REPO_ROOT / "docs" / "vendored" / "vendor_manifest.json"

# Python distributions EdgeSense will need. None are needed to RUN this script.
PYTHON_PACKAGES: list[tuple[str, str, str]] = [
    ("pytest", "pytest", "required — host-side unit tests and C/Python parity checks"),
    ("yaml", "PyYAML", "required by the vendored AdaptiveSense simulator"),
    ("serial", "pyserial", "required for serial capture of node/gateway logs"),
    ("fastapi", "fastapi", "required from Phase 8 (backend)"),
    ("sqlalchemy", "SQLAlchemy", "required from Phase 8 (backend)"),
    ("psycopg", "psycopg[binary]", "required from Phase 8 (PostgreSQL driver)"),
]

EXPECTED_DIRS = [
    "docs/vendored",
    "docs/engineering",
    "docs/hardware",
    "docs/research",
    "scripts",
    "experiments",
    "datasets/real",
    "datasets/injected",
]

EXPECTED_FILES = [
    "docs/vendored/vendor_sources.json",
    "docs/vendored/vendor_manifest.json",
    "docs/engineering/design_decisions.md",
    "docs/engineering/architecture.md",
    "docs/hardware/wiring.md",
    "docs/research/experiment_design.md",
    "scripts/vendor_manifest.py",
]


@dataclass
class Section:
    title: str
    rows: list[tuple[Status, str, str]] = field(default_factory=list)

    def add(self, status: Status, item: str, detail: str = "") -> None:
        self.rows.append((status, item, detail))


def marker(status: Status) -> str:
    return {"ok": "  ok  ", "missing": " MISS ", "warn": " WARN ", "info": " info "}[status]


def check_interpreter(section: Section) -> None:
    section.add("info", "python", f"{sys.executable} ({sys.version.split()[0]})")
    section.add("info", "platform", sys.platform)
    venv = os.environ.get("VIRTUAL_ENV")
    if venv:
        section.add("ok", "virtualenv", venv)
    else:
        section.add(
            "warn",
            "virtualenv",
            "no VIRTUAL_ENV set — consider a project venv before installing dependencies",
        )


def check_python_packages(section: Section) -> None:
    for module, dist, why in PYTHON_PACKAGES:
        try:
            found = importlib.util.find_spec(module) is not None
        except (ImportError, ValueError):
            found = False
        if found:
            section.add("ok", dist, why)
        else:
            section.add("missing", dist, why)


def check_esp_idf(section: Section) -> None:
    idf_path = os.environ.get("IDF_PATH")
    candidate = Path(idf_path) if idf_path else Path.home() / "esp" / "esp-idf"
    if candidate.is_dir():
        export = candidate / "export.sh"
        section.add("ok", "ESP-IDF tree", str(candidate))
        section.add(
            "ok" if export.is_file() else "warn",
            "export.sh",
            str(export) if export.is_file() else f"not found under {candidate}",
        )
    else:
        section.add("missing", "ESP-IDF tree", f"looked for {candidate}")

    idf_py = shutil.which("idf.py")
    if idf_py:
        section.add("ok", "idf.py on PATH", idf_py)
    else:
        section.add(
            "warn",
            "idf.py on PATH",
            "not active — source export.sh before building firmware (expected outside a build shell)",
        )

    for tool in ("cc", "cmake", "ninja"):
        path = shutil.which(tool)
        section.add("ok" if path else "missing", tool, path or "not found on PATH")


def check_serial(section: Section) -> None:
    found = sorted(Path("/dev").glob("cu.*"))
    if found:
        section.add("ok", "serial devices", f"{len(found)} found: " + ", ".join(p.name for p in found[:6]))
    else:
        section.add(
            "warn",
            "serial devices",
            "none found under /dev (expected when no board is plugged in)",
        )


def check_upstreams(section: Section) -> None:
    if not VENDOR_SOURCES.is_file():
        section.add("missing", "vendor_sources.json", str(VENDOR_SOURCES))
        return
    with VENDOR_SOURCES.open(encoding="utf-8") as handle:
        sources = json.load(handle)
    for repo in sources["repos"]:
        path = Path(repo["local_path"])
        if not path.is_dir():
            section.add("missing", repo["name"], f"upstream path absent: {path}")
            continue
        commit = repo.get("commit") or ""
        if commit:
            detail = f"{path}  @ {commit[:12]}"
        else:
            detail = f"{path}  @ no git history (content-hash freeze)"
        section.add("ok", repo["name"], detail)


def run_vendor_verify() -> tuple[bool, str]:
    script = REPO_ROOT / "scripts" / "vendor_manifest.py"
    if not script.is_file():
        return False, "scripts/vendor_manifest.py not found"
    try:
        done = subprocess.run(
            [sys.executable, str(script), "--verify"],
            capture_output=True,
            text=True,
            timeout=180,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return False, f"could not run vendor verify: {exc}"
    output = (done.stdout or done.stderr or "").strip().splitlines()
    return done.returncode == 0, output[0] if output else "no output"


def check_project_layout(section: Section) -> None:
    for rel in EXPECTED_DIRS:
        target = REPO_ROOT / rel
        if target.is_dir():
            section.add("ok", f"{rel}/", "")
        else:
            section.add("missing", f"{rel}/", "create this directory")
    for rel in EXPECTED_FILES:
        target = REPO_ROOT / rel
        if target.is_file():
            section.add("ok", rel, "")
        else:
            section.add("missing", rel, "missing document/tool")


def check_stray_artifacts(section: Section) -> None:
    gitignore = REPO_ROOT / ".gitignore"
    section.add("ok" if gitignore.is_file() else "warn", ".gitignore", str(gitignore) if gitignore.is_file() else "not present")
    tracked_ish = [".DS_Store"]
    for name in tracked_ish:
        hits = list(REPO_ROOT.rglob(name))
        if hits:
            section.add("warn", f"stray {name}", f"{len(hits)} occurrence(s)")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true", help="exit non-zero when REQUIRED items are missing")
    args = parser.parse_args(argv)

    sections = [
        ("Interpreter", check_interpreter),
        ("Python packages", check_python_packages),
        ("Toolchain", check_esp_idf),
        ("Serial", check_serial),
        ("Upstream sources", check_upstreams),
        ("Project layout", check_project_layout),
        ("Hygiene", check_stray_artifacts),
    ]

    print("EdgeSense preflight — reporting gaps, not failing on them")
    print(f"repo root: {REPO_ROOT}")
    print()

    results: list[Section] = []
    for title, fn in sections:
        section = Section(title)
        fn(section)
        results.append(section)

    counts: dict[Status, int] = {"ok": 0, "missing": 0, "warn": 0, "info": 0}
    for section in results:
        print(f"== {section.title} ==")
        for status, item, detail in section.rows:
            counts[status] += 1
            line = f"[{marker(status)}] {item}"
            if detail:
                line += f"  —  {detail}"
            print(line)
        print()

    ok, summary = run_vendor_verify()
    print("== Vendor freeze ==")
    print(f"[{marker('ok' if ok else 'missing')}] vendor_manifest --verify  —  {summary}")
    print()

    print("== Summary ==")
    print(f"  ok={counts['ok']}  missing={counts['missing']}  warn={counts['warn']}  info={counts['info']}")
    if counts["missing"]:
        print("  Note: 'missing' items above are informational. Several are only needed from later phases.")

    if args.strict and counts["missing"]:
        print("  --strict: failing because required items are missing.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
