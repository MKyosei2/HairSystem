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
MANIFEST_VERSION = "novelty01-100k-v1.4"


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
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as handle:
        handle.write(encoded)
        temporary = Path(handle.name)
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(description="Freeze the real Novelty01 100k asset without mutating the tracked template manifest.")
    parser.add_argument("--template-manifest", "--manifest", dest="template_manifest", required=True)
    parser.add_argument("--output-manifest", required=True)
    parser.add_argument("--asset", required=True)
    parser.add_argument("--generator-settings", required=True)
    topology_group = parser.add_mutually_exclusive_group(required=True)
    topology_group.add_argument("--topology-file")
    topology_group.add_argument("--topology-sha256")
    parser.add_argument("--operator", required=True)
    parser.add_argument("--repo-root")
    parser.add_argument("--allow-dirty-worktree", action="store_true")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    template_path = Path(args.template_manifest).resolve()
    output_path = Path(args.output_manifest).resolve()
    asset_path = Path(args.asset).resolve()
    settings_path = Path(args.generator_settings).resolve()
    topology_path = Path(args.topology_file).resolve() if args.topology_file else None
    for path, label in ((template_path, "template manifest"), (asset_path, "asset"), (settings_path, "generator settings")):
        if not path.is_file():
            raise SystemExit(f"Missing {label}: {path}")
    if topology_path is not None and not topology_path.is_file():
        raise SystemExit(f"Missing topology file: {topology_path}")
    if output_path.exists() and not args.force:
        raise SystemExit(f"Output manifest already exists: {output_path}. Use --force to replace it intentionally.")

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

    payload = read_json(template_path)
    generation = payload.setdefault("generation", {})
    expected = {
        "displayStrandCount": 100000,
        "primaryGuideStrandCount": 4000,
        "followersPerGuide": 24,
        "particlesPerGuide": 128,
        "solverIterations": 6,
    }
    mismatches = {key: (expected_value, generation.get(key)) for key, expected_value in expected.items() if generation.get(key) != expected_value}
    if mismatches:
        raise SystemExit(f"Template generation contract mismatch: {mismatches}")

    payload.update({
        "manifestVersion": MANIFEST_VERSION,
        "freezeStatus": "FROZEN",
        "sourceAssetPath": str(asset_path),
        "sourceAssetSha256": sha256_file(asset_path),
        "sourceAssetSizeBytes": asset_path.stat().st_size,
        "generatedTopologySha256": topology_sha256,
        "generatedTopologyPath": str(topology_path) if topology_path is not None else "",
        "generatorSettingsPath": str(settings_path),
        "generatorSettingsSha256": sha256_file(settings_path),
        "frozenAtUtc": datetime.now(timezone.utc).isoformat(),
        "frozenBy": args.operator,
        "sourceCommitSha": head.lower(),
        "sourceBranch": branch,
        "freezeTool": "tools/final_stage/freeze_novelty01_asset.py",
        "freezeToolVersion": "1.4",
    })
    gate = payload.setdefault("evidenceGate", {})
    gate.update({
        "rejectWhenFreezeStatusIsNot": "FROZEN",
        "rejectPlaceholderHashes": True,
        "requireExactSha256Format": True,
        "requireSameAssetHashAcrossAllMethodsAndSeeds": True,
        "requireSourceCommitMatch": True,
        "requireCleanGitWorktreeAtFreeze": not args.allow_dirty_worktree,
        "requireLocalFileHashVerification": True,
        "requireViewerObservedAssetPathMatch": True,
        "requirePreAndPostRunAssetHashMatch": True,
    })
    atomic_write_json(output_path, payload)
    print(f"Frozen manifest: {output_path}")
    print(f"Asset SHA-256: {payload['sourceAssetSha256']}")
    print(f"Topology SHA-256: {payload['generatedTopologySha256']}")
    print(f"Generator settings SHA-256: {payload['generatorSettingsSha256']}")
    print(f"Source commit: {payload['sourceCommitSha']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
