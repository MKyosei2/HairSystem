from __future__ import annotations

"""Resolve the active Git branch into a reproducible HairSystem suite context.

The formal runner still validates ``source.required_branch``.  This helper keeps
that strict check while removing the need to hard-code a branch name in every
suite JSON.  It detects the currently checked-out branch, pins the current
commit SHA, copies scenario timelines to an ignored run-context directory, and
writes a resolved configuration with absolute paths.

The original suite configuration and scenario files are never modified.
"""

import argparse
import copy
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Mapping


SCHEMA_VERSION = "hairgpu-suite-branch-context-v1"


class BranchContextError(RuntimeError):
    pass


def _run_git(repo_root: Path, *args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=repo_root,
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
    except (FileNotFoundError, subprocess.SubprocessError) as exc:
        raise BranchContextError(f"git command failed: {' '.join(args)}: {exc}") from exc
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "").strip()
        raise BranchContextError(
            f"git command failed ({completed.returncode}): {' '.join(args)}: {detail}"
        )
    return completed.stdout.strip()


def _try_git(repo_root: Path, *args: str) -> str:
    try:
        return _run_git(repo_root, *args)
    except BranchContextError:
        return ""


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _write_json(path: Path, payload: Any) -> None:
    _atomic_write_text(
        path,
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
    )


def _read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise BranchContextError(f"Expected a JSON object: {path}")
    return value


def _safe_component(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value.strip())
    cleaned = cleaned.strip("._-")
    return cleaned[:96] or "unknown"


def detect_branch(repo_root: Path, *, allow_detached: bool = False) -> dict[str, Any]:
    commit_sha = _run_git(repo_root, "rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-fA-F]{40}", commit_sha):
        raise BranchContextError(f"Invalid commit SHA returned by git: {commit_sha!r}")

    branch = _try_git(repo_root, "branch", "--show-current")
    detection_source = "git_branch_show_current"
    if not branch:
        branch = _try_git(repo_root, "symbolic-ref", "--quiet", "--short", "HEAD")
        detection_source = "git_symbolic_ref"

    # Actions checkouts can be detached.  Prefer the actual head branch supplied
    # by GitHub rather than treating refs/pull/*/merge as a real source branch.
    if not branch:
        github_head = os.environ.get("GITHUB_HEAD_REF", "").strip()
        github_ref = os.environ.get("GITHUB_REF_NAME", "").strip()
        if github_head:
            branch = github_head
            detection_source = "GITHUB_HEAD_REF"
        elif github_ref and not github_ref.endswith("/merge"):
            branch = github_ref
            detection_source = "GITHUB_REF_NAME"

    detached = not bool(branch)
    if detached:
        if not allow_detached:
            raise BranchContextError(
                "The current Git branch could not be resolved. Check out a named branch "
                "or pass --allow-detached explicitly for diagnostic-only execution."
            )
        branch = f"detached-{commit_sha[:12]}"
        detection_source = "detached_commit_fallback"

    upstream = _try_git(
        repo_root,
        "rev-parse",
        "--abbrev-ref",
        "--symbolic-full-name",
        "@{upstream}",
    )
    remote_url = _try_git(repo_root, "remote", "get-url", "origin")
    return {
        "schemaVersion": SCHEMA_VERSION,
        "branch": branch,
        "commitSha": commit_sha.lower(),
        "commitShortSha": commit_sha[:12].lower(),
        "detachedHead": detached,
        "detectionSource": detection_source,
        "upstream": upstream,
        "originUrl": remote_url,
    }


def _resolve_path(base: Path, value: Any) -> str:
    path = Path(str(value))
    if not path.is_absolute():
        path = base / path
    return str(path.resolve())


def _merge_assertions(
    timeline_payload: Mapping[str, Any], scenario_entry: Mapping[str, Any]
) -> dict[str, Any]:
    timeline_assertions = timeline_payload.get("assertions")
    merged = dict(timeline_assertions) if isinstance(timeline_assertions, dict) else {}
    configured = scenario_entry.get("assertions")
    if isinstance(configured, dict):
        merged.update(configured)
    return merged


def materialize_config(
    config_path: Path,
    repo_root: Path,
    output_override: Path | None,
    context: Mapping[str, Any],
    *,
    allow_output_context_mismatch: bool = False,
) -> tuple[Path, Path, dict[str, Any]]:
    config_path = config_path.resolve()
    config_base = config_path.parent
    source_bytes = config_path.read_bytes()
    original = _read_json(config_path)
    resolved = copy.deepcopy(original)

    for key in ("hardware_profiles",):
        if resolved.get(key):
            resolved[key] = _resolve_path(config_base, resolved[key])

    source = resolved.setdefault("source", {})
    if not isinstance(source, dict):
        raise BranchContextError("source must be a JSON object")
    for key in ("viewer_exe", "schema", "asset_manifest", "shader_root"):
        if source.get(key):
            source[key] = _resolve_path(config_base, source[key])

    configured_output = output_override
    if configured_output is None:
        raw_output = resolved.get("outputs_dir")
        if not raw_output:
            raise BranchContextError("outputs_dir is required when --output-dir is omitted")
        configured_output = Path(_resolve_path(config_base, raw_output))
    output_dir = configured_output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    branch_name = str(context["branch"])
    commit_sha = str(context["commitSha"])
    context_root = (
        output_dir
        / ".suite_context"
        / _safe_component(branch_name)
        / commit_sha[:12]
    )
    timeline_root = context_root / "timelines"
    timeline_root.mkdir(parents=True, exist_ok=True)

    scenarios = resolved.get("scenarios", [])
    if not isinstance(scenarios, list):
        raise BranchContextError("scenarios must be a JSON array")
    for index, scenario in enumerate(scenarios):
        if not isinstance(scenario, dict):
            raise BranchContextError(f"scenario entry {index} is not an object")
        raw_timeline = scenario.get("timeline")
        if not raw_timeline:
            raise BranchContextError(f"scenario entry {index} has no timeline")
        source_timeline = Path(_resolve_path(config_base, raw_timeline))
        if not source_timeline.is_file():
            raise BranchContextError(f"scenario timeline was not found: {source_timeline}")
        timeline_payload = _read_json(source_timeline)
        merged_assertions = _merge_assertions(timeline_payload, scenario)
        if merged_assertions:
            timeline_payload["assertions"] = merged_assertions
            scenario["assertions"] = copy.deepcopy(merged_assertions)
        timeline_payload["materializedFrom"] = str(source_timeline)
        timeline_payload["materializedFromSha256"] = _sha256_file(source_timeline)
        timeline_payload["materializedForBranch"] = branch_name
        timeline_payload["materializedForCommitSha"] = commit_sha
        target_name = f"{index:02d}_{_safe_component(str(scenario.get('code', 'scenario')))}.json"
        target_timeline = timeline_root / target_name
        _write_json(target_timeline, timeline_payload)
        scenario["timeline"] = str(target_timeline.resolve())

    source["required_branch"] = branch_name
    source["detected_branch"] = branch_name
    source["detected_commit_sha"] = commit_sha
    source["branch_detection_source"] = context.get("detectionSource", "")
    source["original_config_path"] = str(config_path)
    source["original_config_sha256"] = _sha256_bytes(source_bytes)
    resolved["outputs_dir"] = str(output_dir)
    resolved["telemetry_dir"] = str((output_dir / "telemetry").resolve())
    resolved["branch_policy"] = {
        "mode": "detect_current",
        "allowDetachedHead": bool(context.get("detachedHead", False)),
        "preventCrossBranchOutputReuse": True,
    }
    resolved["resolved_runtime_context"] = dict(context)

    context_path = output_dir / "suite_branch_context.json"
    existing: dict[str, Any] | None = None
    if context_path.is_file():
        existing = _read_json(context_path)
        existing_identity = (
            str(existing.get("branch", "")),
            str(existing.get("commitSha", "")),
        )
        current_identity = (branch_name, commit_sha)
        if existing_identity != current_identity and not allow_output_context_mismatch:
            raise BranchContextError(
                "The output directory already belongs to another branch or commit: "
                f"existing={existing_identity}, current={current_identity}. "
                "Choose a new output directory instead of mixing evidence."
            )

    context_payload = dict(context)
    context_payload.update(
        {
            "originalConfigPath": str(config_path),
            "originalConfigSha256": _sha256_bytes(source_bytes),
            "resolvedConfigPath": str((context_root / "resolved_suite_config.json").resolve()),
            "outputDirectory": str(output_dir),
            "sourceTimelinesModified": False,
        }
    )
    _write_json(context_path, context_payload)

    resolved_config_path = context_root / "resolved_suite_config.json"
    _write_json(resolved_config_path, resolved)
    return resolved_config_path, output_dir, context_payload


def _write_env_file(
    path: Path,
    resolved_config: Path,
    output_dir: Path,
    context: Mapping[str, Any],
) -> None:
    lines = [
        f"HAIRGPU_RESOLVED_CONFIG={resolved_config}",
        f"HAIRGPU_RESOLVED_OUTPUT={output_dir}",
        f"HAIRGPU_DETECTED_BRANCH={context['branch']}",
        f"HAIRGPU_DETECTED_COMMIT={context['commitSha']}",
    ]
    _atomic_write_text(path.resolve(), "\n".join(lines) + "\n")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Detect the current branch and materialize a branch-pinned suite config."
    )
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--env-file", required=True, type=Path)
    parser.add_argument("--allow-detached", action="store_true")
    parser.add_argument("--allow-output-context-mismatch", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        repo_root = args.repo_root.resolve()
        config_path = args.config.resolve()
        if not (repo_root / ".git").exists():
            raise BranchContextError(f"Not a Git worktree root: {repo_root}")
        if not config_path.is_file():
            raise BranchContextError(f"Suite configuration was not found: {config_path}")
        context = detect_branch(repo_root, allow_detached=args.allow_detached)
        resolved_config, output_dir, context_payload = materialize_config(
            config_path,
            repo_root,
            args.output_dir.resolve() if args.output_dir else None,
            context,
            allow_output_context_mismatch=args.allow_output_context_mismatch,
        )
        _write_env_file(args.env_file, resolved_config, output_dir, context_payload)
        print(
            json.dumps(
                {
                    "branch": context_payload["branch"],
                    "commitSha": context_payload["commitSha"],
                    "resolvedConfig": str(resolved_config),
                    "outputDirectory": str(output_dir),
                },
                ensure_ascii=False,
            )
        )
        return 0
    except (BranchContextError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Branch context error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
