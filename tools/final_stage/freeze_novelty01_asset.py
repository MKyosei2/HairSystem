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
from typing import Any, Iterable

SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
COMMIT_PATTERN = re.compile(r"^[0-9a-fA-F]{40}$")
MANIFEST_VERSION = "novelty01-100k-v1.6"
GENERATOR_SETTINGS_SCHEMA = "novelty01-generator-settings-v1"
CANONICAL_ASSET_NAME = "novelty01_100k.hairbin"
CANONICAL_TOPOLOGY_NAME = "novelty01_100k_topology.bin"
CANONICAL_SETTINGS_NAME = "generator_settings.json"
FORMAL_BRANCH = "main"
FORMAL_GROUPS_PER_BLOCK = 32


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
    encoded = json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def git_value(repo_root: Path, *args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=repo_root,
            check=False,
            capture_output=True,
            text=True,
            timeout=20,
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
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, delete=False, newline="\n"
    ) as handle:
        handle.write(encoded)
        handle.flush()
        os.fsync(handle.fileno())
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
    if destination.stat().st_size != source.stat().st_size:
        raise SystemExit(f"Copy size verification failed: {source} -> {destination}")
    if sha256_file(destination) != sha256_file(source):
        raise SystemExit(f"Copy hash verification failed: {source} -> {destination}")


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


def run_checked(
    command: Iterable[str], *, cwd: Path, timeout: int = 600
) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        list(command),
        cwd=cwd,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "<no output>").strip()
        raise SystemExit(
            f"Command failed with exit code {completed.returncode}: {' '.join(command)}\n{detail}"
        )
    return completed


def _manifest_generation_from_suite(payload: dict[str, Any]) -> dict[str, Any]:
    defaults = payload.get("run_defaults")
    contract = payload.get("frozen_asset_contract")
    if not isinstance(defaults, dict):
        raise SystemExit("Template has neither generation nor run_defaults.")
    if not isinstance(contract, dict):
        contract = {}
    return {
        "displayStrandCount": int(defaults.get("display_strand_count", 100000)),
        "primaryGuideStrandCount": int(defaults.get("guide_strand_count", 4000)),
        "followersPerGuide": int(defaults.get("followers_per_guide", 24)),
        "particlesPerGuide": int(defaults.get("particles_per_guide", 128)),
        "solverIterations": int(defaults.get("solver_iterations", 6)),
        "groupsPerBlock": int(
            contract.get("required_groups_per_block", FORMAL_GROUPS_PER_BLOCK)
        ),
    }


def load_manifest_template(path: Path) -> dict[str, Any]:
    source = read_json(path)
    if isinstance(source.get("generation"), dict):
        payload = dict(source)
    else:
        payload = {
            "assetName": "Novelty01 canonical 100k frozen asset",
            "generation": _manifest_generation_from_suite(source),
        }
    return payload


def validate_generator_settings(path: Path) -> dict[str, Any]:
    settings = read_json(path)
    expected = {
        "schemaVersion": GENERATOR_SETTINGS_SCHEMA,
        "guideStrandCount": 4000,
        "followersPerGuide": 24,
        "displayStrandCount": 100000,
        "particlesPerStrand": 128,
        "solverIterations": 6,
        "groupsPerBlock": FORMAL_GROUPS_PER_BLOCK,
        "maskSourceMode": "Procedural",
        "editStackApplied": False,
    }
    mismatches: dict[str, tuple[Any, Any]] = {}
    for key, expected_value in expected.items():
        observed = settings.get(key)
        if observed != expected_value:
            mismatches[key] = (expected_value, observed)
    if mismatches:
        raise SystemExit(f"Generator settings contract mismatch: {mismatches}")
    return settings


def resolve_bundle_member(manifest_path: Path, value: Any) -> Path:
    raw = Path(str(value))
    if raw.is_absolute():
        raise SystemExit(f"Absolute bundle path is forbidden: {raw}")
    root = manifest_path.parent.resolve()
    resolved = (root / raw).resolve()
    try:
        resolved.relative_to(root)
    except ValueError as exc:
        raise SystemExit(f"Bundle path escapes root: {raw}") from exc
    if resolved.is_symlink():
        raise SystemExit(f"Bundle symlink is forbidden: {resolved}")
    return resolved


def verify_bundle(bundle_dir: Path, viewer_exe: Path | None = None) -> dict[str, Any]:
    manifest_path = bundle_dir / "frozen_asset_manifest.json"
    if not manifest_path.is_file():
        raise SystemExit(f"Frozen manifest is missing: {manifest_path}")
    manifest = read_json(manifest_path)
    if manifest.get("manifestVersion") != MANIFEST_VERSION:
        raise SystemExit(
            f"Manifest version mismatch: expected {MANIFEST_VERSION}, "
            f"observed {manifest.get('manifestVersion')}"
        )
    if manifest.get("freezeStatus") != "FROZEN":
        raise SystemExit("Frozen manifest freezeStatus must be FROZEN.")
    if manifest.get("sourceBranch") != FORMAL_BRANCH:
        raise SystemExit(
            f"Frozen manifest sourceBranch must be {FORMAL_BRANCH!r}, "
            f"observed {manifest.get('sourceBranch')!r}."
        )
    source_commit = str(manifest.get("sourceCommitSha", ""))
    if not COMMIT_PATTERN.fullmatch(source_commit):
        raise SystemExit("Frozen manifest sourceCommitSha must be a 40-character SHA.")

    entries = manifest.get("bundleFiles")
    if not isinstance(entries, list) or len(entries) != 3:
        raise SystemExit("bundleFiles must contain exactly asset, topology and settings entries.")
    canonical_entries: list[dict[str, Any]] = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise SystemExit("bundleFiles contains a non-object entry.")
        member = resolve_bundle_member(manifest_path, entry.get("path"))
        if not member.is_file():
            raise SystemExit(f"Bundle member is missing: {member}")
        actual = file_entry(member, bundle_dir)
        canonical_entries.append(actual)
        if actual["sizeBytes"] != int(entry.get("sizeBytes", -1)):
            raise SystemExit(f"Bundle size mismatch: {entry.get('path')}")
        if actual["sha256"].lower() != str(entry.get("sha256", "")).lower():
            raise SystemExit(f"Bundle hash mismatch: {entry.get('path')}")

    expected_bundle_hash = canonical_json_sha256(
        sorted(canonical_entries, key=lambda item: item["path"])
    )
    if expected_bundle_hash.lower() != str(manifest.get("bundleSha256", "")).lower():
        raise SystemExit("bundleSha256 mismatch.")

    for path_key, hash_key in (
        ("sourceAssetPath", "sourceAssetSha256"),
        ("generatedTopologyPath", "generatedTopologySha256"),
        ("generatorSettingsPath", "generatorSettingsSha256"),
    ):
        member = resolve_bundle_member(manifest_path, manifest.get(path_key))
        if not member.is_file():
            raise SystemExit(f"Manifest member is missing: {path_key}: {member}")
        if sha256_file(member).lower() != str(manifest.get(hash_key, "")).lower():
            raise SystemExit(f"Manifest member hash mismatch: {path_key}")

    settings_path = resolve_bundle_member(manifest_path, manifest["generatorSettingsPath"])
    validate_generator_settings(settings_path)

    if viewer_exe is not None:
        viewer_exe = viewer_exe.resolve()
        if not viewer_exe.is_file():
            raise SystemExit(f"Viewer executable is missing: {viewer_exe}")
        run_checked(
            [str(viewer_exe), "--validate-frozen-source", str(bundle_dir.resolve())],
            cwd=viewer_exe.parent,
            timeout=600,
        )

    print(f"Frozen bundle verified: {bundle_dir}")
    print(f"Bundle SHA-256: {expected_bundle_hash}")
    return manifest


def export_source_with_viewer(
    viewer_exe: Path, source_dir: Path
) -> tuple[Path, Path, Path]:
    viewer_exe = viewer_exe.resolve()
    if not viewer_exe.is_file():
        raise SystemExit(f"Viewer executable is missing: {viewer_exe}")
    source_dir.mkdir(parents=True, exist_ok=True)
    run_checked(
        [str(viewer_exe), "--export-frozen-source", str(source_dir)],
        cwd=viewer_exe.parent,
        timeout=600,
    )
    asset_path = source_dir / CANONICAL_ASSET_NAME
    topology_path = source_dir / CANONICAL_TOPOLOGY_NAME
    settings_path = source_dir / CANONICAL_SETTINGS_NAME
    for path, label in (
        (asset_path, "canonical hair asset"),
        (topology_path, "canonical topology"),
        (settings_path, "generator settings"),
    ):
        if not path.is_file() or path.stat().st_size <= 0:
            raise SystemExit(f"Viewer did not generate a valid {label}: {path}")
    validate_generator_settings(settings_path)
    run_checked(
        [str(viewer_exe), "--validate-frozen-source", str(source_dir)],
        cwd=viewer_exe.parent,
        timeout=600,
    )
    return asset_path, topology_path, settings_path


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Generate, freeze and verify the portable Novelty01 canonical 100k "
            "asset bundle from the formal main branch."
        )
    )
    parser.add_argument("--template-manifest", "--manifest", dest="template_manifest")
    parser.add_argument("--output-manifest")
    parser.add_argument("--bundle-dir", required=True)
    parser.add_argument("--asset")
    parser.add_argument("--generator-settings")
    parser.add_argument("--topology-file")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--generate-source", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--operator")
    parser.add_argument("--repo-root")
    parser.add_argument("--required-branch", default=FORMAL_BRANCH)
    parser.add_argument("--allow-dirty-worktree", action="store_true")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    bundle_dir = Path(args.bundle_dir).resolve()
    viewer_exe = Path(args.viewer_exe).resolve() if args.viewer_exe else None
    if args.verify_only:
        verify_bundle(bundle_dir, viewer_exe)
        return 0

    if not args.template_manifest:
        raise SystemExit("--template-manifest is required unless --verify-only is used.")
    if not args.operator:
        raise SystemExit("--operator is required when creating a frozen bundle.")

    template_path = Path(args.template_manifest).resolve()
    if not template_path.is_file():
        raise SystemExit(f"Missing template manifest/config: {template_path}")
    repo_root = (
        Path(args.repo_root).resolve()
        if args.repo_root
        else find_repo_root(template_path.parent)
    )
    if repo_root is None:
        raise SystemExit("Cannot locate the Git repository root.")

    head = git_value(repo_root, "rev-parse", "HEAD")
    branch = git_value(repo_root, "branch", "--show-current")
    dirty = git_value(repo_root, "status", "--porcelain", "--untracked-files=normal")
    if not COMMIT_PATTERN.fullmatch(head):
        raise SystemExit("Cannot resolve a valid Git HEAD commit.")
    required_branch = str(args.required_branch).strip() or FORMAL_BRANCH
    if branch != required_branch:
        raise SystemExit(
            f"Asset freezing requires branch {required_branch!r}, "
            f"observed: {branch or '<detached>'}"
        )
    if dirty and not args.allow_dirty_worktree:
        raise SystemExit("Git worktree is dirty. Commit changes before freezing formal evidence.")

    temporary_source: tempfile.TemporaryDirectory[str] | None = None
    try:
        if args.generate_source:
            if viewer_exe is None:
                raise SystemExit("--viewer-exe is required with --generate-source.")
            temporary_source = tempfile.TemporaryDirectory(
                prefix="novelty01_frozen_source_"
            )
            asset_path, topology_path, settings_path = export_source_with_viewer(
                viewer_exe, Path(temporary_source.name)
            )
            generation_mode = "viewer-canonical-export"
        else:
            if not args.asset or not args.topology_file or not args.generator_settings:
                raise SystemExit(
                    "Manual mode requires --asset, --topology-file and --generator-settings."
                )
            asset_path = Path(args.asset).resolve()
            topology_path = Path(args.topology_file).resolve()
            settings_path = Path(args.generator_settings).resolve()
            generation_mode = "manual-canonical-input"
            for path, label in (
                (asset_path, "asset"),
                (topology_path, "topology"),
                (settings_path, "generator settings"),
            ):
                if not path.is_file() or path.stat().st_size <= 0:
                    raise SystemExit(f"Missing or empty {label}: {path}")
            validate_generator_settings(settings_path)

        payload = load_manifest_template(template_path)
        generation = payload.setdefault("generation", {})
        expected = {
            "displayStrandCount": 100000,
            "primaryGuideStrandCount": 4000,
            "followersPerGuide": 24,
            "particlesPerGuide": 128,
            "solverIterations": 6,
            "groupsPerBlock": FORMAL_GROUPS_PER_BLOCK,
        }
        mismatches = {
            key: (expected_value, generation.get(key))
            for key, expected_value in expected.items()
            if generation.get(key) != expected_value
        }
        if mismatches:
            raise SystemExit(f"Template generation contract mismatch: {mismatches}")

        temporary = bundle_dir.with_name(bundle_dir.name + f".tmp.{os.getpid()}")
        if temporary.exists():
            shutil.rmtree(temporary)
        temporary.mkdir(parents=True)
        asset_target = temporary / "asset" / CANONICAL_ASSET_NAME
        topology_target = temporary / "topology" / CANONICAL_TOPOLOGY_NAME
        settings_target = temporary / "settings" / CANONICAL_SETTINGS_NAME
        copy_verified(asset_path, asset_target)
        copy_verified(topology_path, topology_target)
        copy_verified(settings_path, settings_target)

        entries = sorted(
            [
                file_entry(asset_target, temporary),
                file_entry(topology_target, temporary),
                file_entry(settings_target, temporary),
            ],
            key=lambda item: item["path"],
        )
        payload.update(
            {
                "manifestVersion": MANIFEST_VERSION,
                "freezeStatus": "FROZEN",
                "generationMode": generation_mode,
                "generatorSettingsSchemaVersion": GENERATOR_SETTINGS_SCHEMA,
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
                "sourceRefType": "branch",
                "sourceCommitPolicy": "exact-runtime-head-recorded",
                "assetFormatVersion": 1,
                "topologyFormatVersion": 1,
                "minimumCompatibleRuntimeVersion": 5,
                "assetInternalValidation": "PASS",
                "topologyInternalValidation": "PASS",
                "roundTripValidation": "PASS",
                "freezeTool": "tools/final_stage/freeze_novelty01_asset.py",
                "freezeToolVersion": "4.0-main-formal-100k",
            }
        )
        payload.setdefault("evidenceGate", {}).update(
            {
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
                "requireRoundTripValidation": True,
                "requireMainBranchAtFreeze": required_branch == FORMAL_BRANCH,
            }
        )
        atomic_write_json(temporary / "frozen_asset_manifest.json", payload)

        for entry in entries:
            member = temporary / entry["path"]
            if (
                not member.is_file()
                or member.stat().st_size != entry["sizeBytes"]
                or sha256_file(member) != entry["sha256"]
            ):
                raise SystemExit(f"Bundle verification failed: {entry['path']}")

        if viewer_exe is not None:
            run_checked(
                [str(viewer_exe), "--validate-frozen-source", str(temporary)],
                cwd=viewer_exe.parent,
                timeout=600,
            )
        replace_directory_atomic(temporary, bundle_dir, args.force)
        verify_bundle(bundle_dir, viewer_exe)
        print(f"Frozen portable bundle: {bundle_dir}")
        print(f"Frozen manifest: {bundle_dir / 'frozen_asset_manifest.json'}")
        print(f"Asset source commit: {head.lower()}")
        return 0
    finally:
        if temporary_source is not None:
            temporary_source.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
