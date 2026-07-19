from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
COMMIT_PATTERN = re.compile(r"^[0-9a-fA-F]{40}$")
MANIFEST_VERSION = "novelty01-100k-v1.5"


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise SystemExit(f"Expected a JSON object: {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_sha256(payload: Any) -> str:
    encoded = json.dumps(payload, ensure_ascii=False, sort_keys=True,
                         separators=(",", ":"), allow_nan=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def git_value(repo_root: Path, *args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", *args], cwd=repo_root, check=False,
            capture_output=True, text=True, timeout=20,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return ""
    return completed.stdout.strip() if completed.returncode == 0 else ""


def find_repo_root(start: Path) -> Path | None:
    for candidate in [start, *start.parents]:
        if (candidate / ".git").exists():
            return candidate
    return None


def atomic_write_json(path: Path, payload: dict[str, Any]) -> None:
    encoded = json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent,
                                     delete=False, newline="\n") as handle:
        handle.write(encoded); handle.flush(); os.fsync(handle.fileno())
        temporary = Path(handle.name)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)
    if read_json(path) != payload:
        raise SystemExit(f"Frozen manifest verification failed after atomic replace: {path}")


def copy_verified(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    if destination.stat().st_size != source.stat().st_size or sha256_file(destination) != sha256_file(source):
        raise SystemExit(f"Copy verification failed: {source} -> {destination}")


def file_entry(path: Path, bundle_root: Path) -> dict[str, Any]:
    return {
        "path": path.relative_to(bundle_root).as_posix(),
        "sizeBytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def replace_directory_atomic(temporary: Path, destination: Path, force: bool) -> None:
    backup = destination.with_name(destination.name + ".previous")
    if destination.exists():
        if not force:
            raise SystemExit(f"Bundle already exists: {destination}. Use --force intentionally.")
        if backup.exists():
            shutil.rmtree(backup)
        os.replace(destination, backup)
    try:
        os.replace(temporary, destination)
    except Exception:
        if backup.exists() and not destination.exists():
            os.replace(backup, destination)
        raise
    else:
        if backup.exists():
            shutil.rmtree(backup)


def main() -> int:
    parser = argparse.ArgumentParser(description="Freeze the portable Novelty01 canonical 100k asset bundle.")
    parser.add_argument("--template-manifest", "--manifest", dest="template_manifest", required=True)
    parser.add_argument("--output-manifest")
    parser.add_argument("--bundle-dir")
    parser.add_argument("--asset", required=True)
    parser.add_argument("--generator-settings", required=True)
    topology = parser.add_mutually_exclusive_group(required=True)
    topology.add_argument("--topology-file")
    topology.add_argument("--topology-sha256")
    parser.add_argument("--operator", required=True)
    parser.add_argument("--repo-root")
    parser.add_argument("--allow-dirty-worktree", action="store_true")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    template_path = Path(args.template_manifest).resolve()
    asset_path = Path(args.asset).resolve()
    settings_path = Path(args.generator_settings).resolve()
    topology_path = Path(args.topology_file).resolve() if args.topology_file else None
    for path, label in ((template_path, "template manifest"), (asset_path, "asset"),
                        (settings_path, "generator settings")):
        if not path.is_file():
            raise SystemExit(f"Missing {label}: {path}")
    if topology_path is not None and not topology_path.is_file():
        raise SystemExit(f"Missing topology file: {topology_path}")

    if args.bundle_dir:
        bundle_dir = Path(args.bundle_dir).resolve()
        output_path = bundle_dir / "frozen_asset_manifest.json"
    elif args.output_manifest:
        output_path = Path(args.output_manifest).resolve()
        bundle_dir = output_path.parent
    else:
        raise SystemExit("Either --bundle-dir or --output-manifest is required.")

    repo_root = Path(args.repo_root).resolve() if args.repo_root else find_repo_root(template_path.parent)
    if repo_root is None:
        raise SystemExit("Cannot locate the Git repository root.")
    head = git_value(repo_root, "rev-parse", "HEAD")
    branch = git_value(repo_root, "branch", "--show-current")
    dirty = git_value(repo_root, "status", "--porcelain", "--untracked-files=normal")
    if not COMMIT_PATTERN.fullmatch(head):
        raise SystemExit("Cannot resolve a valid Git HEAD commit.")
    if branch != "Novelty":
        raise SystemExit(f"Asset freezing requires branch Novelty, observed: {branch or '<detached>'}")
    if dirty and not args.allow_dirty_worktree:
        raise SystemExit("Git worktree is dirty. Commit changes before freezing formal evidence.")

    topology_sha256 = sha256_file(topology_path) if topology_path else str(args.topology_sha256).lower()
    if not SHA256_PATTERN.fullmatch(topology_sha256):
        raise SystemExit("Topology SHA-256 must be exactly 64 hexadecimal characters.")
    if topology_path is None:
        raise SystemExit("Portable formal bundles require --topology-file; --topology-sha256 is retained only for CLI compatibility.")

    payload = read_json(template_path)
    generation = payload.setdefault("generation", {})
    expected = {
        "displayStrandCount": 100000, "primaryGuideStrandCount": 4000,
        "followersPerGuide": 24, "particlesPerGuide": 128, "solverIterations": 6,
    }
    mismatches = {key: (value, generation.get(key)) for key, value in expected.items()
                  if generation.get(key) != value}
    if mismatches:
        raise SystemExit(f"Template generation contract mismatch: {mismatches}")

    temporary = bundle_dir.with_name(bundle_dir.name + f".tmp.{os.getpid()}")
    if temporary.exists():
        shutil.rmtree(temporary)
    temporary.mkdir(parents=True)
    asset_target = temporary / "asset" / asset_path.name
    topology_target = temporary / "topology" / topology_path.name
    settings_target = temporary / "settings" / settings_path.name
    copy_verified(asset_path, asset_target)
    copy_verified(topology_path, topology_target)
    copy_verified(settings_path, settings_target)

    entries = sorted([
        file_entry(asset_target, temporary),
        file_entry(topology_target, temporary),
        file_entry(settings_target, temporary),
    ], key=lambda item: item["path"])
    payload.update({
        "manifestVersion": MANIFEST_VERSION,
        "freezeStatus": "FROZEN",
        "sourceAssetPath": asset_target.relative_to(temporary).as_posix(),
        "sourceAssetSha256": sha256_file(asset_target),
        "sourceAssetSizeBytes": asset_target.stat().st_size,
        "generatedTopologyPath": topology_target.relative_to(temporary).as_posix(),
        "generatedTopologySha256": sha256_file(topology_target),
        "generatorSettingsPath": settings_target.relative_to(temporary).as_posix(),
        "generatorSettingsSha256": sha256_file(settings_target),
        "bundleFiles": entries,
        "bundleSha256": canonical_json_sha256(entries),
        "frozenAtUtc": datetime.now(timezone.utc).isoformat(),
        "frozenBy": args.operator,
        "assetSourceCommitSha": head.lower(),
        "sourceCommitSha": head.lower(),
        "sourceBranch": branch,
        "assetFormatVersion": 5,
        "minimumCompatibleRuntimeVersion": 4,
        "freezeTool": "tools/final_stage/freeze_novelty01_asset.py",
        "freezeToolVersion": "2.0-portable-bundle",
    })
    payload.setdefault("evidenceGate", {}).update({
        "rejectWhenFreezeStatusIsNot": "FROZEN",
        "rejectPlaceholderHashes": True,
        "requireExactSha256Format": True,
        "requireSameAssetHashAcrossAllMethodsAndSeeds": True,
        "requireSourceCommitMatch": False,
        "recordRuntimeCommitSeparately": True,
        "requireCleanGitWorktreeAtFreeze": not args.allow_dirty_worktree,
        "requireLocalFileHashVerification": True,
        "requireViewerObservedAssetPathMatch": True,
        "requirePreAndPostRunAssetHashMatch": True,
        "rejectAbsoluteBundlePaths": True,
        "rejectBundlePathTraversal": True,
    })
    atomic_write_json(temporary / "frozen_asset_manifest.json", payload)

    # Verify the final temporary bundle before making it visible.
    for entry in entries:
        member = temporary / entry["path"]
        if not member.is_file() or member.stat().st_size != entry["sizeBytes"] or sha256_file(member) != entry["sha256"]:
            raise SystemExit(f"Bundle verification failed: {entry['path']}")
    replace_directory_atomic(temporary, bundle_dir, args.force)

    final_manifest = bundle_dir / "frozen_asset_manifest.json"
    print(f"Frozen portable bundle: {bundle_dir}")
    print(f"Frozen manifest: {final_manifest}")
    print(f"Bundle SHA-256: {payload['bundleSha256']}")
    print(f"Asset source commit: {payload['assetSourceCommitSha']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
