#!/usr/bin/env python3
"""Aggregate the separated pre-novelty evidence phases into one immutable package.

This helper intentionally does not run the Viewer.  It validates the outputs produced
by Run_Final_Runtime_Suite.bat, records their hashes and repository identity, writes
one completion manifest atomically, and creates one portable ZIP.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import tempfile
import time
import zipfile
from pathlib import Path
from typing import Any, Iterable

SCHEMA_VERSION = "hairgpu-prenovelty-complete-v1"
PHASES = ("smoke", "startup20", "determinism", "soak30")
BAD_MARKER_PATTERNS = ("*_ERROR.json", "*_INCOMPLETE.json", "*_RUNNING.json", "*.run.lock")


def utc_now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_sha256(payload: Any) -> str:
    raw = json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def write_text_atomic(path: Path, text: str, *, encoding: str = "utf-8") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "w", encoding=encoding, newline="") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_json_atomic(path: Path, payload: dict[str, Any]) -> None:
    write_text_atomic(
        path,
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
    )


def git_value(repo_root: Path, *args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=repo_root,
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return ""
    return completed.stdout.strip() if completed.returncode == 0 else ""


def resolve_config_asset_manifest(config_path: Path) -> Path | None:
    config = read_json(config_path)
    source = config.get("source")
    if not isinstance(source, dict):
        return None
    raw = source.get("asset_manifest")
    if raw in (None, ""):
        return None
    path = Path(str(raw))
    if not path.is_absolute():
        path = config_path.parent / path
    return path.resolve()


def file_record(path: Path, root: Path) -> dict[str, Any]:
    return {
        "path": path.resolve().relative_to(root.resolve()).as_posix(),
        "sizeBytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def collect_phase_files(root: Path, phase: str) -> tuple[list[Path], list[dict[str, Any]]]:
    phase_dir = root / phase
    issues: list[dict[str, Any]] = []
    if not phase_dir.is_dir():
        return [], [{"code": "phase_directory_missing", "phase": phase, "path": str(phase_dir)}]

    if phase == "startup20":
        required = [phase_dir / "startup20_summary.json"]
    else:
        required = [
            phase_dir / "suite_preflight.json",
            phase_dir / "suite_metrics.csv",
            phase_dir / "suite_metrics.json",
            phase_dir / "final_runtime_suite.sqlite",
            phase_dir / "suite_post_gates.json",
            phase_dir / "suite_summary.json",
            phase_dir / "final_runtime_suite_artifacts.zip",
        ]

    for path in required:
        if not path.is_file():
            issues.append({"code": "phase_summary_missing", "phase": phase, "path": str(path)})

    return required, issues


def validate_phase(root: Path, phase: str) -> tuple[dict[str, Any], list[dict[str, Any]], list[Path]]:
    required, issues = collect_phase_files(root, phase)
    payload: dict[str, Any] = {"phase": phase, "passed": False, "files": []}
    if issues:
        return payload, issues, []

    if phase == "startup20":
        summary = read_json(required[0])
        requested = int(summary.get("requestedRepetitions", 0) or 0)
        completed = int(summary.get("completedRepetitions", 0) or 0)
        runs = summary.get("runs", []) if isinstance(summary.get("runs"), list) else []
        passed_runs = sum(
            1 for run in runs
            if isinstance(run, dict) and bool(run.get("passed"))
        )
        passed = (
            bool(summary.get("passed"))
            and requested == 20
            and completed == requested
            and len(runs) == requested
            and passed_runs == requested
        )
        if not passed:
            issues.append(
                {
                    "code": "startup20_failed",
                    "phase": phase,
                    "requested": requested,
                    "completed": completed,
                    "runRecordCount": len(runs),
                    "passedRunCount": passed_runs,
                    "passed": bool(summary.get("passed")),
                }
            )
        payload.update(
            {
                "passed": passed,
                "requestedRepetitions": requested,
                "completedRepetitions": completed,
                "runRecordCount": len(runs),
                "passedRunCount": passed_runs,
                "viewerSha256": summary.get("viewerSha256", ""),
            }
        )
    else:
        preflight = read_json(required[0])
        post = read_json(required[4])
        summary = read_json(required[5])
        selected = int(summary.get("selectedRunCount", 0) or 0)
        completed = int(summary.get("completedRowCount", 0) or 0)
        valid = int(summary.get("validRunCount", 0) or 0)
        worst_exit = int(summary.get("worstExitCode", 999) or 0)
        complete_markers = sorted((root / phase).glob("*_COMPLETE.json"))
        preflight_problems = (
            preflight.get("problems", [])
            if isinstance(preflight.get("problems"), list)
            else ["invalid_preflight_problem_list"]
        )
        post_issues = (
            post.get("issues", [])
            if isinstance(post.get("issues"), list)
            else ["invalid_post_gate_issue_list"]
        )
        passed = (
            not preflight_problems
            and bool(summary.get("postSuiteGatesPassed"))
            and bool(post.get("passed"))
            and not post_issues
            and selected > 0
            and completed == selected
            and valid == selected
            and len(complete_markers) == selected
            and worst_exit == 0
        )
        if not passed:
            issues.append(
                {
                    "code": "suite_phase_failed",
                    "phase": phase,
                    "selectedRunCount": selected,
                    "completedRowCount": completed,
                    "validRunCount": valid,
                    "worstExitCode": worst_exit,
                    "preflightProblems": preflight_problems,
                    "postSuiteGatesPassed": bool(summary.get("postSuiteGatesPassed")),
                    "postGatePassed": bool(post.get("passed")),
                    "postGateIssues": post_issues,
                    "completeMarkerCount": len(complete_markers),
                }
            )
        payload.update(
            {
                "passed": passed,
                "selectedRunCount": selected,
                "completedRowCount": completed,
                "validRunCount": valid,
                "qualityPassCount": int(summary.get("qualityPassCount", 0) or 0),
                "completeMarkerCount": len(complete_markers),
                "worstExitCode": worst_exit,
                "runtimeBranch": summary.get("runtimeBranch", ""),
                "runtimeCommitSha": summary.get("runtimeCommitSha", ""),
            }
        )

    payload["files"] = [file_record(path, root) for path in required]
    return payload, issues, required


def find_bad_markers(root: Path) -> list[Path]:
    found: list[Path] = []
    for pattern in BAD_MARKER_PATTERNS:
        found.extend(path for path in root.rglob(pattern) if path.is_file())
    return sorted(set(found))


def iter_archive_files(
    root: Path,
    archive: Path,
    manifest_path: Path,
    sidecar_path: Path,
) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if path in {archive, manifest_path, sidecar_path}:
            continue
        if path.name.endswith(".tmp") or ".tmp." in path.name:
            continue
        yield path


def build_archive(
    root: Path,
    archive: Path,
    config_path: Path,
    manifest_path: Path,
    sidecar_path: Path,
) -> None:
    temporary = archive.with_suffix(archive.suffix + ".tmp")
    temporary.unlink(missing_ok=True)
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as handle:
        handle.write(config_path, arcname="configuration/" + config_path.name)
        handle.write(manifest_path, arcname="complete_manifest.json")
        for path in iter_archive_files(root, archive, manifest_path, sidecar_path):
            handle.write(path, arcname="results/" + path.relative_to(root).as_posix())
    os.replace(temporary, archive)


def aggregate(root: Path, config_path: Path, repo_root: Path) -> tuple[dict[str, Any], int]:
    root = root.resolve()
    config_path = config_path.resolve()
    repo_root = repo_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    archive_path = root / "prenovelty_complete_evidence.zip"
    sidecar_path = root / "prenovelty_complete_evidence.zip.sha256"
    archive_path.unlink(missing_ok=True)
    sidecar_path.unlink(missing_ok=True)

    issues: list[dict[str, Any]] = []
    phases: list[dict[str, Any]] = []
    evidence_files: list[Path] = []
    for phase in PHASES:
        payload, phase_issues, required = validate_phase(root, phase)
        phases.append(payload)
        issues.extend(phase_issues)
        evidence_files.extend(required)

    bad_markers = find_bad_markers(root)
    if bad_markers:
        issues.append(
            {
                "code": "incomplete_or_error_markers_present",
                "paths": [path.relative_to(root).as_posix() for path in bad_markers],
            }
        )

    branch = git_value(repo_root, "branch", "--show-current")
    commit_sha = git_value(repo_root, "rev-parse", "HEAD")
    dirty = git_value(repo_root, "status", "--porcelain", "--untracked-files=normal")
    if not branch:
        issues.append({"code": "runtime_branch_unavailable"})
    if not commit_sha:
        issues.append({"code": "runtime_commit_unavailable"})
    if dirty:
        issues.append({"code": "dirty_worktree", "paths": dirty.splitlines()[:20]})

    asset_manifest = resolve_config_asset_manifest(config_path)
    asset_record: dict[str, Any] = {"available": False, "path": "", "sha256": ""}
    if asset_manifest and asset_manifest.is_file():
        asset_record = {
            "available": True,
            "path": str(asset_manifest),
            "sha256": sha256_file(asset_manifest),
        }
    else:
        issues.append(
            {
                "code": "frozen_asset_manifest_missing",
                "path": str(asset_manifest) if asset_manifest else "",
            }
        )

    successful = not issues and all(bool(phase.get("passed")) for phase in phases)
    manifest: dict[str, Any] = {
        "schemaVersion": SCHEMA_VERSION,
        "generatedUtc": utc_now(),
        "status": "COMPLETE" if successful else "INCOMPLETE",
        "passed": successful,
        "repository": {
            "root": str(repo_root),
            "branch": branch,
            "commitSha": commit_sha,
            "cleanWorktree": not bool(dirty),
        },
        "configuration": {
            "path": str(config_path),
            "sha256": sha256_file(config_path),
        },
        "frozenAssetManifest": asset_record,
        "phases": phases,
        "issues": issues,
        "archiveFileName": "prenovelty_complete_evidence.zip",
        "archiveSha256Sidecar": "prenovelty_complete_evidence.zip.sha256",
    }
    unsigned = dict(manifest)
    manifest["manifestSha256"] = canonical_json_sha256(unsigned)

    manifest_path = root / "complete_manifest.json"
    write_json_atomic(manifest_path, manifest)

    if successful:
        archive = archive_path
        sidecar = sidecar_path
        build_archive(root, archive, config_path, manifest_path, sidecar)
        archive_hash = sha256_file(archive)
        write_text_atomic(
            sidecar,
            f"{archive_hash}  {archive.name}\n",
            encoding="ascii",
        )
        return manifest, 0

    return manifest, 2


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="hairgpu_prenovelty_complete_") as temporary:
        base = Path(temporary)
        root = base / "out"
        repo = base / "repo"
        repo.mkdir()
        config = base / "suite.json"
        frozen = base / "frozen_asset_manifest.json"
        frozen.write_text("{}\n", encoding="utf-8")
        config.write_text(
            json.dumps({"source": {"asset_manifest": str(frozen)}}, indent=2) + "\n",
            encoding="utf-8",
        )
        for phase in ("smoke", "determinism", "soak30"):
            directory = root / phase
            directory.mkdir(parents=True)
            write_json_atomic(directory / "suite_preflight.json", {"problems": []})
            (directory / "suite_metrics.csv").write_text("run_name,valid\ncase,1\n", encoding="utf-8")
            write_json_atomic(directory / "suite_metrics.json", {"rows": [{"run_name": "case", "valid": 1}]})
            (directory / "final_runtime_suite.sqlite").write_bytes(b"SQLite format 3\x00selftest")
            write_json_atomic(directory / "suite_post_gates.json", {"passed": True, "issues": []})
            write_json_atomic(
                directory / "suite_summary.json",
                {
                    "selectedRunCount": 1,
                    "completedRowCount": 1,
                    "validRunCount": 1,
                    "qualityPassCount": 1,
                    "worstExitCode": 0,
                    "postSuiteGatesPassed": True,
                },
            )
            (directory / "final_runtime_suite_artifacts.zip").write_bytes(b"PK\x05\x06" + b"\x00" * 18)
            write_json_atomic(directory / "case_COMPLETE.json", {"state": "COMPLETE"})
        startup = root / "startup20"
        startup.mkdir(parents=True)
        write_json_atomic(
            startup / "startup20_summary.json",
            {
                "requestedRepetitions": 20,
                "completedRepetitions": 20,
                "passed": True,
                "viewerSha256": "a" * 64,
                "runs": [
                    {"repetition": index, "passed": True, "exitCode": 0}
                    for index in range(1, 21)
                ],
            },
        )

        # Use a small temporary Git repository so identity and clean-state checks run.
        subprocess.run(["git", "init"], cwd=repo, check=True, capture_output=True)
        subprocess.run(["git", "config", "user.email", "selftest@example.invalid"], cwd=repo, check=True)
        subprocess.run(["git", "config", "user.name", "Self Test"], cwd=repo, check=True)
        (repo / "README.txt").write_text("test\n", encoding="utf-8")
        subprocess.run(["git", "add", "README.txt"], cwd=repo, check=True)
        subprocess.run(["git", "commit", "-m", "self test"], cwd=repo, check=True, capture_output=True)

        manifest, exit_code = aggregate(root, config, repo)
        if exit_code != 0 or not manifest.get("passed"):
            print(json.dumps(manifest, indent=2), flush=True)
            return 1
        archive = root / "prenovelty_complete_evidence.zip"
        sidecar = root / "prenovelty_complete_evidence.zip.sha256"
        if not archive.is_file() or not sidecar.is_file():
            return 2
        expected_hash = sidecar.read_text(encoding="ascii").split()[0]
        if expected_hash != sha256_file(archive):
            return 4
        with zipfile.ZipFile(archive, "r") as handle:
            archived_manifest = json.loads(
                handle.read("complete_manifest.json").decode("utf-8")
            )
        if archived_manifest != read_json(root / "complete_manifest.json"):
            return 5

        (root / "soak30" / "BROKEN_ERROR.json").write_text("{}\n", encoding="utf-8")
        failed, failed_code = aggregate(root, config, repo)
        if failed_code == 0 or failed.get("passed"):
            return 3

    print("hairgpu_prenovelty_complete self-test: PASS")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Aggregate separated pre-novelty evidence phases.")
    parser.add_argument("--root", type=Path)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--repo-root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return self_test()
    if not args.root or not args.config or not args.repo_root:
        raise SystemExit("--root, --config and --repo-root are required")
    try:
        manifest, exit_code = aggregate(args.root, args.config, args.repo_root)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Pre-novelty aggregation failed: {exc}", flush=True)
        return 3
    print(json.dumps(manifest, ensure_ascii=False, indent=2), flush=True)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
