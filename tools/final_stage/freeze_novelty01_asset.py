from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
COMMIT_PATTERN = re.compile(r"^[0-9a-fA-F]{40}$")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_head(repo_root: Path) -> str:
    try:
        completed = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo_root,
            check=False,
            capture_output=True,
            text=True,
            timeout=20,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return ""
    return completed.stdout.strip() if completed.returncode == 0 else ""


def git_clean(repo_root: Path) -> bool:
    try:
        completed = subprocess.run(
            ["git", "status", "--porcelain", "--untracked-files=normal"],
            cwd=repo_root,
            check=False,
            capture_output=True,
            text=True,
            timeout=20,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return False
    return completed.returncode == 0 and not completed.stdout.strip()


def find_repo_root(start: Path) -> Path | None:
    for candidate in [start, *start.parents]:
        if (candidate / ".git").exists():
            return candidate
    return None


def atomic_write_json(path: Path, payload: dict[str, Any]) -> None:
    encoded = json.dumps(payload, ensure_ascii=False, indent=2) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as handle:
        handle.write(encoded)
        temporary = Path(handle.name)
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(description="Freeze the novelty01 100k benchmark asset manifest using real local bytes.")
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--asset", required=True)
    parser.add_argument("--generator-settings", required=True)
    topology_group = parser.add_mutually_exclusive_group(required=True)
    topology_group.add_argument("--topology-file")
    topology_group.add_argument("--topology-sha256")
    parser.add_argument("--operator", required=True)
    parser.add_argument("--repo-root")
    parser.add_argument("--allow-dirty-worktree", action="store_true", help="Development only; not valid for final paper evidence.")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    manifest_path = Path(args.manifest).resolve()
    asset_path = Path(args.asset).resolve()
    generator_settings_path = Path(args.generator_settings).resolve()
    topology_file = Path(args.topology_file).resolve() if args.topology_file else None
    for path, label in ((manifest_path, "manifest"), (asset_path, "asset"), (generator_settings_path, "generator settings")):
        if not path.exists() or not path.is_file():
            raise SystemExit(f"Missing {label} file: {path}")
    if topology_file is not None and (not topology_file.exists() or not topology_file.is_file()):
        raise SystemExit(f"Missing topology file: {topology_file}")

    payload = read_json(manifest_path)
    if str(payload.get("freezeStatus", "")) == "FROZEN" and not args.force:
        raise SystemExit("Manifest is already FROZEN. Use --force only after intentionally invalidating previous evidence.")

    repo_root = Path(args.repo_root).resolve() if args.repo_root else find_repo_root(manifest_path.parent)
    if repo_root is None:
        raise SystemExit("Cannot locate the Git repository root.")
    head = git_head(repo_root)
    if not COMMIT_PATTERN.fullmatch(head):
        raise SystemExit("Cannot resolve a valid Git HEAD commit.")
    if not args.allow_dirty_worktree and not git_clean(repo_root):
        raise SystemExit("Git worktree is dirty. Commit changes before freezing the evidence asset.")

    topology_sha256 = sha256_file(topology_file) if topology_file is not None else str(args.topology_sha256).lower()
    if not SHA256_PATTERN.fullmatch(topology_sha256):
        raise SystemExit("Topology SHA-256 must be exactly 64 hexadecimal characters.")

    payload["manifestVersion"] = "novelty01-100k-v1.2"
    payload["freezeStatus"] = "FROZEN"
    payload["sourceAssetPath"] = str(asset_path)
    payload["sourceAssetSha256"] = sha256_file(asset_path)
    payload["sourceAssetSizeBytes"] = asset_path.stat().st_size
    payload["generatedTopologySha256"] = topology_sha256
    payload["generatorSettingsPath"] = str(generator_settings_path)
    payload["generatorSettingsSha256"] = sha256_file(generator_settings_path)
    payload["frozenAtUtc"] = datetime.now(timezone.utc).isoformat()
    payload["frozenBy"] = args.operator
    payload["sourceCommitSha"] = head.lower()
    payload["freezeTool"] = "tools/final_stage/freeze_novelty01_asset.py"
    payload["freezeToolVersion"] = "1.2"
    payload.setdefault("evidenceGate", {})["rejectWhenFreezeStatusIsNot"] = "FROZEN"
    payload["evidenceGate"]["rejectPlaceholderHashes"] = True
    payload["evidenceGate"]["requireSameAssetHashAcrossAllMethodsAndSeeds"] = True
    payload["evidenceGate"]["requireSourceCommitMatch"] = True
    payload["evidenceGate"]["requireCleanGitWorktreeAtFreeze"] = not args.allow_dirty_worktree

    backup = manifest_path.with_suffix(manifest_path.suffix + ".before_freeze")
    if not backup.exists():
        backup.write_bytes(manifest_path.read_bytes())
    atomic_write_json(manifest_path, payload)
    print(f"Frozen manifest: {manifest_path}")
    print(f"Asset SHA-256: {payload['sourceAssetSha256']}")
    print(f"Topology SHA-256: {payload['generatedTopologySha256']}")
    print(f"Generator settings SHA-256: {payload['generatorSettingsSha256']}")
    print(f"Source commit: {payload['sourceCommitSha']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
