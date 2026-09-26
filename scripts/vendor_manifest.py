#!/usr/bin/env python3
"""EdgeSense vendor freeze manifest — generator and verifier.

Reads  docs/vendored/vendor_sources.json   (authoritative, hand-maintained)
Writes docs/vendored/vendor_manifest.json  (generated, never hand-edited)

Each frozen file records three independent facts:
  blob_sha1   git content address of the upstream blob (verifiable with `git hash-object`)
  sha256      content digest (works even for upstreams with no git history)
  size_bytes  file size

Usage:
    python scripts/vendor_manifest.py            # generate the manifest
    python scripts/vendor_manifest.py --verify   # re-check and exit non-zero on drift

No third-party dependencies: the standard library only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCES_PATH = REPO_ROOT / "docs" / "vendored" / "vendor_sources.json"
MANIFEST_PATH = REPO_ROOT / "docs" / "vendored" / "vendor_manifest.json"

SCHEMA_VERSION = 1


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_blob_sha1(repo: Path, relpath: str) -> str | None:
    """Return the git blob SHA-1 of a tracked path, or None if unavailable."""
    try:
        done = subprocess.run(
            ["git", "hash-object", "--", relpath],
            cwd=str(repo),
            capture_output=True,
            text=True,
            timeout=20,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode != 0:
        return None
    out = done.stdout.strip()
    return out or None


def git_head(repo: Path) -> tuple[str | None, str | None, bool | None]:
    """Return (commit, commit_date_iso, dirty) for a repo, tolerating no-history repos."""

    def run(*args: str) -> str | None:
        try:
            done = subprocess.run(
                ["git", *args], cwd=str(repo), capture_output=True, text=True, timeout=20
            )
        except (OSError, subprocess.SubprocessError):
            return None
        if done.returncode != 0:
            return None
        return done.stdout.strip() or None

    commit = run("rev-parse", "HEAD")
    if commit is not None and not _looks_like_sha(commit):
        commit = None  # unborn branch: rev-parse prints the literal "HEAD"
    date = run("log", "-1", "--format=%cI") if commit else None
    status = run("status", "--porcelain")
    dirty = bool(status) if status is not None else None
    return commit, date, dirty


def _looks_like_sha(value: str) -> bool:
    return len(value) == 40 and all(c in "0123456789abcdef" for c in value)


def load_sources() -> dict[str, Any]:
    if not SOURCES_PATH.exists():
        raise SystemExit(f"missing source definition: {SOURCES_PATH}")
    with SOURCES_PATH.open(encoding="utf-8") as handle:
        return json.load(handle)


def collect() -> dict[str, Any]:
    sources = load_sources()
    manifest: dict[str, Any] = {
        "schema": SCHEMA_VERSION,
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "frozen_on": sources.get("frozen_on"),
        "generator": "scripts/vendor_manifest.py",
        "repos": [],
    }

    for repo in sources["repos"]:
        name = repo["name"]
        path = Path(repo["local_path"])
        entry: dict[str, Any] = {
            "name": name,
            "local_path": repo["local_path"],
            "exists": path.exists(),
            "remote": repo.get("remote"),
            "declared_commit": repo.get("commit"),
            "no_git_history": bool(repo.get("no_git_history")),
            "role_in_edgesense": repo.get("role_in_edgesense"),
            "take": [],
            "preserve_readonly": [],
            "missing": [],
        }

        if not path.exists():
            manifest["repos"].append(entry)
            continue

        observed_commit, observed_date, dirty = git_head(path)
        entry["observed_commit"] = observed_commit
        entry["observed_commit_date"] = observed_date
        entry["observed_dirty"] = dirty
        entry["commit_matches_declared"] = (
            None
            if repo.get("commit") is None
            else observed_commit == repo["commit"]
        )

        for rel in repo.get("take", []):
            target = path / rel
            if not target.is_file():
                entry["missing"].append(rel)
                continue
            entry["take"].append(
                {
                    "path": rel,
                    "blob_sha1": git_blob_sha1(path, rel),
                    "sha256": sha256_of(target),
                    "size_bytes": target.stat().st_size,
                }
            )

        for rel in repo.get("preserve_readonly", []):
            target = path / rel
            if not target.is_file():
                entry["missing"].append(rel)
                continue
            entry["preserve_readonly"].append(
                {
                    "path": rel,
                    "sha256": sha256_of(target),
                    "size_bytes": target.stat().st_size,
                }
            )

        manifest["repos"].append(entry)

    return manifest


def verify(manifest: dict[str, Any]) -> int:
    problems: list[str] = []

    for repo in manifest["repos"]:
        name = repo["name"]
        path = Path(repo["local_path"])
        if not repo["exists"]:
            problems.append(f"{name}: local_path no longer exists")
            continue

        observed_commit, _, _ = git_head(path)
        if repo.get("declared_commit") and observed_commit != repo["declared_commit"]:
            problems.append(
                f"{name}: commit drift — declared {repo['declared_commit'][:12]}, "
                f"observed {str(observed_commit)[:12]}"
            )

        for item in repo.get("take", []) + repo.get("preserve_readonly", []):
            target = path / item["path"]
            if not target.is_file():
                problems.append(f"{name}: missing {item['path']}")
                continue
            actual = sha256_of(target)
            if actual != item["sha256"]:
                problems.append(
                    f"{name}: content drift in {item['path']} "
                    f"(sha256 {item['sha256'][:12]} -> {actual[:12]})"
                )

        for rel in repo.get("missing", []):
            problems.append(f"{name}: declared but absent at freeze time: {rel}")

    if problems:
        print(f"VENDOR FREEZE CHECK FAILED — {len(problems)} problem(s):")
        for line in problems:
            print(f"  - {line}")
        return 1

    total = sum(len(r.get("take", [])) + len(r.get("preserve_readonly", [])) for r in manifest["repos"])
    print(f"VENDOR FREEZE CHECK PASSED — {len(manifest['repos'])} repos, {total} frozen files unchanged")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true", help="re-check the recorded manifest")
    args = parser.parse_args(argv)

    if args.verify:
        if not MANIFEST_PATH.exists():
            print(f"no manifest to verify at {MANIFEST_PATH}")
            return 1
        with MANIFEST_PATH.open(encoding="utf-8") as handle:
            return verify(json.load(handle))

    manifest = collect()
    MANIFEST_PATH.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    frozen = sum(len(r["take"]) for r in manifest["repos"])
    preserved = sum(len(r["preserve_readonly"]) for r in manifest["repos"])
    missing = sum(len(r["missing"]) for r in manifest["repos"])
    print(f"wrote {MANIFEST_PATH.relative_to(REPO_ROOT)}")
    print(f"  repos={len(manifest['repos'])} frozen_files={frozen} preserved={preserved} missing={missing}")
    for repo in manifest["repos"]:
        flag = "" if repo.get("commit_matches_declared") is not False else "  <-- COMMIT MISMATCH"
        if repo.get("no_git_history"):
            flag = "  (no git history: content-hash only)"
        print(f"  - {repo['name']:<20} take={len(repo.get('take', []))} missing={len(repo.get('missing', []))}{flag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
