from __future__ import annotations

"""Apply the four-field Viewer formal-asset binding contract to the Main runner.

The patch is deterministic, idempotent, and fail-closed:
- it modifies only tools/final_stage/hairgpu_final_run.py;
- it accepts the exact current-Main anchors;
- it refuses ambiguous source layouts;
- it compiles the resulting Python before writing;
- it writes UTF-8 without BOM and creates an optional backup.
"""

import argparse
import json
import shutil
from pathlib import Path

REQUIRED_ENV_KEYS = (
    "HAIRGPU_FORMAL_ASSET_PATH",
    "HAIRGPU_FORMAL_ASSET_SHA256",
    "HAIRGPU_FORMAL_TOPOLOGY_SHA256",
    "HAIRGPU_GENERATOR_SETTINGS_SHA256",
)

RESOLVED_MEMBER_ANCHOR = """\
    shader_hash = sha256_tree([shader_root], base_path=repo_root) if shader_root and shader_root.exists() else ""
    values = {
"""

RESOLVED_MEMBER_REPLACEMENT = """\
    shader_hash = sha256_tree([shader_root], base_path=repo_root) if shader_root and shader_root.exists() else ""

    # Viewer formal runs are not allowed to fall back to the interactive asset
    # cache. Resolve the validated frozen-bundle members once and propagate the
    # exact identity contract to the child process.
    resolved_members = asset_manifest.get("_resolvedMembers", {})
    if not isinstance(resolved_members, Mapping):
        raise FormalRunError(
            "Frozen asset manifest does not contain resolved bundle members.",
            EXIT_CONFIGURATION_FAILURE,
            [issue(
                "E_FORMAL_ASSET_RESOLVED_MEMBERS",
                "Validated _resolvedMembers mapping is required.",
                expected="mapping",
                observed=type(resolved_members).__name__,
            )],
        )
    formal_asset_path = str(
        resolved_members.get("sourceAssetPath", "")
    ).strip()
    if not formal_asset_path:
        raise FormalRunError(
            "Frozen asset source path is unavailable.",
            EXIT_CONFIGURATION_FAILURE,
            [issue(
                "E_FORMAL_ASSET_PATH",
                "Resolved sourceAssetPath is required.",
                expected="non-empty absolute path",
                observed=formal_asset_path,
            )],
        )
    formal_asset = Path(formal_asset_path)
    if not formal_asset.is_absolute() or not formal_asset.is_file():
        raise FormalRunError(
            "Resolved frozen asset file is missing.",
            EXIT_CONFIGURATION_FAILURE,
            [issue(
                "E_FORMAL_ASSET_FILE",
                "Resolved frozen asset must be an existing absolute file.",
                expected="existing absolute file",
                observed=formal_asset_path,
            )],
        )

    formal_asset_sha256 = str(
        asset_manifest.get("sourceAssetSha256", "")
    ).strip().lower()
    formal_topology_sha256 = str(
        asset_manifest.get("generatedTopologySha256", "")
    ).strip().lower()
    generator_settings_sha256 = str(
        asset_manifest.get("generatorSettingsSha256", "")
    ).strip().lower()
    for contract_name, contract_value in (
        ("sourceAssetSha256", formal_asset_sha256),
        ("generatedTopologySha256", formal_topology_sha256),
        ("generatorSettingsSha256", generator_settings_sha256),
    ):
        if not SHA256_RE.fullmatch(contract_value):
            raise FormalRunError(
                f"Frozen asset identity is invalid: {contract_name}",
                EXIT_CONFIGURATION_FAILURE,
                [issue(
                    "E_FORMAL_ASSET_HASH",
                    f"{contract_name} must be a SHA-256 value.",
                    expected="64 hexadecimal characters",
                    observed=contract_value,
                )],
            )

    values = {
"""

ENV_ANCHOR = """\
        "HAIRGPU_ASSET_SHA256": asset_manifest.get("sourceAssetSha256", ""),
        "HAIRGPU_BUILD_CONFIGURATION": case.get("build_configuration", "Release"),
"""

ENV_REPLACEMENT = """\
        "HAIRGPU_ASSET_SHA256": asset_manifest.get("sourceAssetSha256", ""),
        "HAIRGPU_FORMAL_ASSET_PATH": formal_asset_path,
        "HAIRGPU_FORMAL_ASSET_SHA256": formal_asset_sha256,
        "HAIRGPU_FORMAL_TOPOLOGY_SHA256": formal_topology_sha256,
        "HAIRGPU_GENERATOR_SETTINGS_SHA256": generator_settings_sha256,
        "HAIRGPU_BUILD_CONFIGURATION": case.get("build_configuration", "Release"),
"""


class PatchError(RuntimeError):
    pass


def patch_text(source: str) -> tuple[str, bool]:
    if all(key in source for key in REQUIRED_ENV_KEYS):
        compile(source, "hairgpu_final_run.py", "exec")
        return source, False

    first_count = source.count(RESOLVED_MEMBER_ANCHOR)
    second_count = source.count(ENV_ANCHOR)
    if first_count != 1 or second_count != 1:
        raise PatchError(
            "Unexpected hairgpu_final_run.py layout: "
            f"resolved_anchor={first_count}, env_anchor={second_count}"
        )

    patched = source.replace(
        RESOLVED_MEMBER_ANCHOR,
        RESOLVED_MEMBER_REPLACEMENT,
        1,
    ).replace(
        ENV_ANCHOR,
        ENV_REPLACEMENT,
        1,
    )
    if not all(key in patched for key in REQUIRED_ENV_KEYS):
        raise PatchError("Formal asset environment keys were not inserted.")
    compile(patched, "hairgpu_final_run.py", "exec")
    return patched, True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--backup-dir", type=Path)
    args = parser.parse_args()

    repo = args.repo.resolve()
    target = repo / "tools" / "final_stage" / "hairgpu_final_run.py"
    if not target.is_file():
        raise PatchError(f"Target runner is missing: {target}")

    source = target.read_text(encoding="utf-8-sig")
    patched, changed = patch_text(source)

    backup_path = None
    if changed:
        if args.backup_dir is not None:
            backup_path = (
                args.backup_dir.resolve()
                / "tools"
                / "final_stage"
                / "hairgpu_final_run.py"
            )
            backup_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, backup_path)
        target.write_text(
            patched,
            encoding="utf-8",
            newline="\n",
        )

    result = {
        "status": "PASS",
        "changed": changed,
        "target": str(target),
        "backup": str(backup_path) if backup_path else None,
        "requiredEnvironmentKeys": list(REQUIRED_ENV_KEYS),
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
