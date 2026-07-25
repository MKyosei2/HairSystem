#!/usr/bin/env python3
"""Create once, verify always contract for the Novelty01 frozen asset bundle."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any

SCHEMA_VERSION = "hairgpu-frozen-bundle-lock-v1"
LOCK_NAME = "frozen_bundle_lock.json"
EXCLUDED_NAMES = {
    LOCK_NAME,
    "frozen_asset_bundle_snapshot.zip",
    "frozen_asset_bundle_snapshot.zip.sha256",
}


def utc_now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as stream:
            json.dump(payload, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


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


def bundle_files(bundle_dir: Path) -> list[Path]:
    return [
        path
        for path in sorted(bundle_dir.rglob("*"))
        if path.is_file()
        and path.name not in EXCLUDED_NAMES
        and ".tmp." not in path.name
        and not path.name.endswith(".tmp")
    ]


def file_records(bundle_dir: Path) -> list[dict[str, Any]]:
    return [
        {
            "path": path.relative_to(bundle_dir).as_posix(),
            "sizeBytes": path.stat().st_size,
            "sha256": sha256_file(path),
        }
        for path in bundle_files(bundle_dir)
    ]


def records_sha256(records: list[dict[str, Any]]) -> str:
    canonical = json.dumps(
        records,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def build_lock(bundle_dir: Path, config: Path, repo_root: Path) -> dict[str, Any]:
    manifest = bundle_dir / "frozen_asset_manifest.json"
    if not manifest.is_file():
        raise RuntimeError(f"Frozen asset manifest is missing: {manifest}")
    records = file_records(bundle_dir)
    if not records:
        raise RuntimeError(f"Frozen asset bundle is empty: {bundle_dir}")
    return {
        "schemaVersion": SCHEMA_VERSION,
        "lockedUtc": utc_now(),
        "sourceBranch": git_value(repo_root, "branch", "--show-current"),
        "sourceCommitSha": git_value(repo_root, "rev-parse", "HEAD"),
        "configurationPath": str(config.resolve()),
        "configurationSha256": sha256_file(config),
        "manifestPath": manifest.relative_to(bundle_dir).as_posix(),
        "manifestSha256": sha256_file(manifest),
        "fileCount": len(records),
        "bundleTreeSha256": records_sha256(records),
        "files": records,
        "regenerationPolicy": "explicit-refreeze-only",
    }


def verify_lock(bundle_dir: Path, lock: dict[str, Any]) -> list[str]:
    problems: list[str] = []
    if lock.get("schemaVersion") != SCHEMA_VERSION:
        problems.append("lock schema version mismatch")
    expected_records = lock.get("files")
    if not isinstance(expected_records, list):
        return problems + ["lock files array is missing"]
    observed_records = file_records(bundle_dir)
    if observed_records != expected_records:
        problems.append("bundle file list, size, or SHA-256 changed")
    observed_tree = records_sha256(observed_records)
    if observed_tree != str(lock.get("bundleTreeSha256", "")):
        problems.append("bundle tree SHA-256 mismatch")
    manifest = bundle_dir / str(lock.get("manifestPath") or "frozen_asset_manifest.json")
    if not manifest.is_file():
        problems.append("frozen asset manifest is missing")
    elif sha256_file(manifest) != str(lock.get("manifestSha256", "")):
        problems.append("frozen asset manifest SHA-256 mismatch")
    return problems


def run_tool(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    completed = subprocess.run(command, check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"Frozen asset tool failed with exit code {completed.returncode}: {' '.join(command)}"
        )


def ensure_bundle(
    bundle_dir: Path,
    config: Path,
    viewer: Path,
    freeze_tool: Path,
    operator: str,
    repo_root: Path,
    *,
    refreeze: bool,
) -> dict[str, Any]:
    bundle_dir.mkdir(parents=True, exist_ok=True)
    manifest = bundle_dir / "frozen_asset_manifest.json"
    lock_path = bundle_dir / LOCK_NAME

    if refreeze:
        if not operator:
            raise RuntimeError("--operator is required for --refreeze")
        run_tool([
            os.fspath(Path(os.sys.executable)), os.fspath(freeze_tool),
            "--template-manifest", os.fspath(config),
            "--bundle-dir", os.fspath(bundle_dir),
            "--viewer-exe", os.fspath(viewer),
            "--generate-source", "--operator", operator,
            "--repo-root", os.fspath(repo_root), "--force",
        ])
    elif not manifest.is_file():
        if lock_path.exists():
            raise RuntimeError("Frozen bundle lock exists but the manifest is missing")
        if not operator:
            raise RuntimeError("--operator is required for the first frozen bundle creation")
        run_tool([
            os.fspath(Path(os.sys.executable)), os.fspath(freeze_tool),
            "--template-manifest", os.fspath(config),
            "--bundle-dir", os.fspath(bundle_dir),
            "--viewer-exe", os.fspath(viewer),
            "--generate-source", "--operator", operator,
            "--repo-root", os.fspath(repo_root), "--force",
        ])

    if not manifest.is_file():
        raise RuntimeError(f"Frozen asset manifest was not generated: {manifest}")

    run_tool([
        os.fspath(Path(os.sys.executable)), os.fspath(freeze_tool),
        "--bundle-dir", os.fspath(bundle_dir),
        "--viewer-exe", os.fspath(viewer),
        "--verify-only",
    ])

    if lock_path.is_file() and not refreeze:
        lock = json.loads(lock_path.read_text(encoding="utf-8-sig"))
        if not isinstance(lock, dict):
            raise RuntimeError("Frozen bundle lock must contain a JSON object")
        problems = verify_lock(bundle_dir, lock)
        if problems:
            raise RuntimeError("Frozen bundle changed after locking: " + "; ".join(problems))
        print(f"Frozen bundle verified without regeneration: {lock_path}", flush=True)
        return lock

    lock = build_lock(bundle_dir, config, repo_root)
    atomic_write_json(lock_path, lock)
    problems = verify_lock(bundle_dir, lock)
    if problems:
        raise RuntimeError("New frozen bundle lock failed verification: " + "; ".join(problems))
    action = "refrozen" if refreeze else "created and locked"
    print(f"Frozen bundle {action}: {lock_path}", flush=True)
    return lock


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="hairgpu_frozen_lock_") as temporary:
        root = Path(temporary)
        bundle = root / "bundle"
        bundle.mkdir()
        config = root / "suite.json"
        config.write_text("{}\n", encoding="utf-8")
        (bundle / "frozen_asset_manifest.json").write_text('{"version":1}\n', encoding="utf-8")
        (bundle / "asset.bin").write_bytes(b"asset")
        (bundle / "topology.bin").write_bytes(b"topology")
        lock = build_lock(bundle, config, root)
        atomic_write_json(bundle / LOCK_NAME, lock)
        if verify_lock(bundle, lock):
            return 1
        (bundle / "asset.bin").write_bytes(b"changed")
        if not verify_lock(bundle, lock):
            return 2
    print("hairgpu_frozen_bundle_lock self-test: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle-dir", type=Path)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--viewer", type=Path)
    parser.add_argument("--freeze-tool", type=Path)
    parser.add_argument("--operator", default="")
    parser.add_argument("--repo-root", type=Path)
    parser.add_argument("--ensure", action="store_true")
    parser.add_argument("--refreeze", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    required = (args.bundle_dir, args.config, args.viewer, args.freeze_tool, args.repo_root)
    if any(value is None for value in required):
        parser.error("--bundle-dir, --config, --viewer, --freeze-tool, and --repo-root are required")
    if args.ensure == args.refreeze:
        parser.error("choose exactly one of --ensure or --refreeze")
    try:
        ensure_bundle(
            args.bundle_dir.resolve(),
            args.config.resolve(),
            args.viewer.resolve(),
            args.freeze_tool.resolve(),
            args.operator,
            args.repo_root.resolve(),
            refreeze=args.refreeze,
        )
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"Frozen bundle operation failed: {exc}", flush=True)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
