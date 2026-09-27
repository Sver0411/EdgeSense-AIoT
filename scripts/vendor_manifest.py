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
        path = Path(repo["local_path"]).expanduser()
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
            "vendored_into": [],
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

        for item in repo.get("vendored_into", []):
            src_rel = item["src"]
            dest_rel = item["dest"]
            src_path = path / src_rel
            dest_path = REPO_ROOT / dest_rel
            record: dict[str, Any] = {
                "src": src_rel,
                "dest": dest_rel,
                "src_present": src_path.is_file(),
                "dest_present": dest_path.is_file(),
            }
            if src_path.is_file():
                record["src_sha256"] = sha256_of(src_path)
            if dest_path.is_file():
                record["dest_sha256"] = sha256_of(dest_path)
            if record.get("src_sha256") and record.get("dest_sha256"):
                record["identical"] = record["src_sha256"] == record["dest_sha256"]
            entry["vendored_into"].append(record)

        manifest["repos"].append(entry)

    return manifest


def verify(manifest: dict[str, Any], require_upstream: bool = False) -> int:
    """Check what can be checked here, and say plainly what could not.

    Two different questions are asked, and they are answerable in different places:

      upstream drift      did the pinned upstream checkout change? Only answerable
                          where that checkout exists.
      vendored copy       is the copy inside EdgeSense still byte-identical to the
                          pinned upstream? Answerable anywhere, because the pinned
                          hashes are recorded in the manifest.

    Conflating them made this script fail on a CI runner, where none of the upstream
    checkouts exist - a red build caused by the environment rather than by a real
    problem. Now an absent upstream is reported as skipped, and only
    --require-upstream turns it into a failure. The vendored copies are always
    verified, on any machine.
    """
    problems: list[str] = []
    notes: list[str] = []
    upstream_checked = 0
    upstream_absent: list[str] = []

    for repo in manifest["repos"]:
        name = repo["name"]
        path = Path(repo["local_path"]).expanduser()
        upstream_present = path.is_dir()

        if upstream_present:
            upstream_checked += 1
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
        else:
            upstream_absent.append(name)
            message = (
                f"{name}: upstream checkout absent at {repo['local_path']} - "
                "upstream drift not checked here; vendored copies are still verified "
                "against the hashes recorded in the manifest"
            )
            if require_upstream:
                problems.append(f"{name}: upstream checkout missing (--require-upstream)")
            else:
                notes.append(message)

        # Vendored copies: verified everywhere, against the recorded hash.
        for record in repo.get("vendored_into", []):
            dest_path = REPO_ROOT / record["dest"]
            if not dest_path.is_file():
                problems.append(f"{name}: vendored copy missing: {record['dest']}")
                continue

            reference = record.get("src_sha256")
            if reference is None:
                problems.append(
                    f"{name}: no recorded source hash for {record['src']}; "
                    f"cannot verify {record['dest']}"
                )
                continue

            dest_sha = sha256_of(dest_path)
            if dest_sha != reference:
                problems.append(
                    f"{name}: VENDORED COPY EDITED: {record['dest']} no longer matches "
                    f"the pinned upstream {record['src']} "
                    f"(dest {dest_sha[:12]} vs recorded {reference[:12]})"
                )

            if upstream_present:
                src_path = path / record["src"]
                if not src_path.is_file():
                    problems.append(f"{name}: upstream src vanished: {record['src']}")
                else:
                    src_sha = sha256_of(src_path)
                    if src_sha != reference:
                        problems.append(
                            f"{name}: UPSTREAM DRIFT in {record['src']} "
                            f"({reference[:12]} -> {src_sha[:12]})"
                        )

    for note in notes:
        print(f"  skip: {note}")

    if problems:
        print(f"VENDOR FREEZE CHECK FAILED — {len(problems)} problem(s):")
        for line in problems:
            print(f"  - {line}")
        return 1

    vend = [v for r in manifest["repos"] for v in r.get("vendored_into", [])]
    scope = f"upstream checkouts verified: {upstream_checked}"
    if upstream_absent:
        scope += f", absent: {len(upstream_absent)} ({', '.join(upstream_absent)})"
    print(
        f"VENDOR FREEZE CHECK PASSED — {len(vend)} vendored copies intact; {scope}"
    )
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true", help="re-check the recorded manifest")
    parser.add_argument(
        "--require-upstream",
        action="store_true",
        help="also fail when an upstream checkout is absent (a local-machine check; "
             "CI runners do not have the upstream repos)",
    )
    args = parser.parse_args(argv)

    if args.verify:
        if not MANIFEST_PATH.exists():
            print(f"no manifest to verify at {MANIFEST_PATH}")
            return 1
        with MANIFEST_PATH.open(encoding="utf-8") as handle:
            return verify(json.load(handle), require_upstream=args.require_upstream)

    manifest = collect()
    MANIFEST_PATH.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    frozen = sum(len(r["take"]) for r in manifest["repos"])
    preserved = sum(len(r["preserve_readonly"]) for r in manifest["repos"])
    missing = sum(len(r["missing"]) for r in manifest["repos"])
    vend = [v for r in manifest["repos"] for v in r.get("vendored_into", [])]
    vend_ok = sum(1 for v in vend if v.get("identical") is True)
    vend_bad = sum(1 for v in vend if v.get("identical") is False)
    vend_absent = sum(1 for v in vend if not v.get("dest_present"))
    print(f"wrote {MANIFEST_PATH.relative_to(REPO_ROOT)}")
    print(f"  repos={len(manifest['repos'])} frozen_files={frozen} preserved={preserved} missing={missing}")
    print(
        f"  vendored_into_edge_sense={len(vend)} identical={vend_ok} "
        f"modified={vend_bad} not_yet_copied={vend_absent}"
    )
    for repo in manifest["repos"]:
        flag = "" if repo.get("commit_matches_declared") is not False else "  <-- COMMIT MISMATCH"
        if repo.get("no_git_history"):
            flag = "  (no git history: content-hash only)"
        print(f"  - {repo['name']:<20} take={len(repo.get('take', []))} missing={len(repo.get('missing', []))}{flag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
