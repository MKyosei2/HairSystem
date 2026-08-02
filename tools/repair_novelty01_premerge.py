from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SOURCE_BASELINE = "37ff9749fe530e8ab08d849892c4c6326a2578bf"
ALLOWED_BRANCHES = {"Novelty01", "main"}

HEADER_DECLARATIONS = """

        // Novelty 01 SG-APT runtime is declared in the real class interface.
        // Keep these declarations private; the implementation is split across
        // parts 16-18 and routed only for P_IntegratedSegmentGroup.
        struct Novelty01RuntimeState;
        struct Novelty01RenderCacheState;
        void ExecuteNovelty01ExperimentTick(float dt);
        void StepNovelty01SingleFrame();
        void StepNovelty01SingleFrameCore();
        void ExecuteNovelty01Complete(float dt);
        bool EnsureNovelty01Runtime(float dt);
        void ResetNovelty01RuntimeIfNeeded();
        void RunNovelty01PartialSimulation(float dt);
        bool RunNovelty01GroundTruth(float dt);
        void CompareNovelty01GroundTruth();
        void WriteNovelty01FormalTelemetry();
        void ValidateNovelty01FormalInvariants();
        void ExecuteNovelty01PreClassificationValidation();
        void ExecuteNovelty01PostCompactionLifecycle();
        void ExecuteNovelty01DirtyRangePropagation();
        void ExecuteNovelty01BuildIndirectArguments();
        void ExecuteNovelty01B4MigrationBaseline();
        void ApplyNovelty01DesignScenarioForTick(float dt);
        bool EnsureNovelty01RenderCache();
        void ConfigureNovelty01FormalTelemetryOutput();
        void UpdateNovelty01RenderCache();
        void ReleaseNovelty01RenderCache();
        void RenderLegacy();
        void ShutdownNovelty01Routed();
        void ShutdownNovelty01RoutedCore();
"""

REPLACEMENTS = (
    ".github/workflows/windows-build-test.yml",
    ".github/workflows/novelty01-formal-gate.yml",
    "apps/dx11_viewer/src/GpuHairViewer.cpp",
    "tools/final_stage/hairgpu_novelty01_preflight.py",
    "tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
    "tests/final_stage/test_novelty01_complete_contract.py",
    "tests/final_stage/test_novelty01_runtime_contract.py",
    "docs/experiments/NOVELTY01_MAIN_INTEGRATION_CHECKLIST.md",
)

REMOVALS = (
    "overlay",
    "replacement",
    "tools/apply_novelty01_69_3_complete.py",
    "tools/apply_novelty01_69_4_complete.py",
    "tools/apply_novelty01_complete.py",
    "tools/apply_novelty01_sgapt.py",
    "tools/final_stage/apply_formal_asset_binding_patch.py",
    "patches/hairgpu_final_run_formal_asset_binding.patch",
)


def run(command: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def git(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return run(["git", "-C", str(repo), *args])


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def backup_path(source: Path, backup_root: Path, relative: Path) -> None:
    if not source.exists():
        return
    destination = backup_root / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, destination, dirs_exist_ok=True)
    else:
        shutil.copy2(source, destination)


def write_text_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".n01-repair.tmp")
    temporary.write_text(text, encoding="utf-8", newline="\n")
    os.replace(temporary, path)


def patch_header(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    if "struct Novelty01RuntimeState;" in text:
        for marker in (
            "struct Novelty01RenderCacheState;",
            "void ExecuteNovelty01ExperimentTick(float dt);",
            "void RenderLegacy();",
            "void ShutdownNovelty01Routed();",
        ):
            require(marker in text, f"GpuHairViewer.h contains a partial Novelty01 declaration set; missing: {marker}")
        return False

    marker = "        struct StrandEditSnapshot;"
    count = text.count(marker)
    require(count == 1, f"Expected exactly one header insertion marker, found {count}: {marker}")
    text = text.replace(marker, marker + HEADER_DECLARATIONS, 1)
    write_text_atomic(path, text)
    return True


def validate_result(repo: Path) -> None:
    cpp = (repo / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
    header = (repo / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
    require("#define private" not in cpp, "GpuHairViewer.cpp still contains #define private.")
    require("#undef private" not in cpp, "GpuHairViewer.cpp still contains #undef private.")
    require('#include "GpuHairViewer.h"' in cpp, "GpuHairViewer.cpp does not include GpuHairViewer.h.")
    for marker in (
        "struct Novelty01RuntimeState;",
        "struct Novelty01RenderCacheState;",
        "void ExecuteNovelty01ExperimentTick(float dt);",
        "void StepNovelty01SingleFrame();",
        "void RenderLegacy();",
        "void ShutdownNovelty01Routed();",
    ):
        require(marker in header, f"GpuHairViewer.h is missing: {marker}")

    for relative in REMOVALS:
        require(not (repo / relative).exists(), f"Obsolete path remains: {relative}")

    for relative in REPLACEMENTS:
        require((repo / relative).is_file(), f"Required repaired file is missing: {relative}")

    for relative in (
        ".github/workflows/windows-build-test.yml",
        ".github/workflows/novelty01-formal-gate.yml",
    ):
        text = (repo / relative).read_text(encoding="utf-8")
        for forbidden in (
            "path.write_text(text",
            "apply_viewer_dedup.py",
            "replacement/",
            "overlay/",
        ):
            require(forbidden not in text, f"Workflow {relative} contains forbidden marker: {forbidden}")

    result = git(repo, "diff", "--check")
    require(result.returncode == 0, "git diff --check failed:\n" + result.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--allow-branch-mismatch", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--skip-static-checks", action="store_true")
    args = parser.parse_args()

    package_root = Path(__file__).resolve().parents[1]
    files_root = package_root / "files"
    repo = args.repo.resolve()
    require(repo.is_dir(), f"Repository directory does not exist: {repo}")
    require((repo / ".git").exists(), f"Not a Git worktree: {repo}")

    branch_result = git(repo, "branch", "--show-current")
    require(branch_result.returncode == 0, branch_result.stdout)
    branch = branch_result.stdout.strip()
    if not args.allow_branch_mismatch:
        require(branch in ALLOWED_BRANCHES, f"Novelty01 or main is required. Current={branch!r}")

    head_result = git(repo, "rev-parse", "HEAD")
    require(head_result.returncode == 0, head_result.stdout)
    head = head_result.stdout.strip()
    ancestor = git(repo, "merge-base", "--is-ancestor", SOURCE_BASELINE, "HEAD")
    require(
        ancestor.returncode == 0,
        f"HEAD must descend from Novelty01 69.9 baseline {SOURCE_BASELINE}. Current HEAD={head}",
    )

    initial_status = git(repo, "status", "--porcelain", "--untracked-files=all")
    require(initial_status.returncode == 0, initial_status.stdout)
    if initial_status.stdout.strip() and not args.allow_dirty:
        raise RuntimeError(
            "The worktree already contains changes. Commit, stash, or restore them "
            "before applying this repair. Use --allow-dirty only after making a separate backup.\n"
            + initial_status.stdout
        )

    required_existing = (
        "apps/dx11_viewer/src/GpuHairViewer.h",
        "apps/dx11_viewer/src/GpuHairViewer.cpp",
        "tools/final_stage/hairgpu_novelty01_preflight.py",
        "tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
        "tests/final_stage/test_novelty01_complete_contract.py",
        "tests/final_stage/test_novelty01_runtime_contract.py",
        "docs/experiments/NOVELTY01_MAIN_INTEGRATION_CHECKLIST.md",
    )
    for relative in required_existing:
        require((repo / relative).is_file(), f"Required existing file is missing: {relative}")
    for relative in REPLACEMENTS:
        require((files_root / relative).is_file(), f"Repair package file is missing: {relative}")

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_root = repo / "out" / f"before_novelty01_premerge_repair_{stamp}"
    backup_root.mkdir(parents=True, exist_ok=False)

    targets = set(REPLACEMENTS) | set(REMOVALS) | {"apps/dx11_viewer/src/GpuHairViewer.h"}
    for relative_text in sorted(targets):
        relative = Path(relative_text)
        backup_path(repo / relative, backup_root, relative)

    changed: list[str] = []
    if patch_header(repo / "apps/dx11_viewer/src/GpuHairViewer.h"):
        changed.append("apps/dx11_viewer/src/GpuHairViewer.h")

    for relative_text in REPLACEMENTS:
        source = files_root / relative_text
        destination = repo / relative_text
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        changed.append(relative_text)

    removed: list[str] = []
    for relative_text in REMOVALS:
        path = repo / relative_text
        if path.is_dir():
            shutil.rmtree(path)
            removed.append(relative_text + "/")
        elif path.exists():
            path.unlink()
            removed.append(relative_text)

    validate_result(repo)

    checks: list[dict[str, object]] = []
    if not args.skip_static_checks:
        python_targets = (
            repo / "tools/final_stage/hairgpu_novelty01_preflight.py",
            repo / "tests/final_stage/test_novelty01_complete_contract.py",
            repo / "tests/final_stage/test_novelty01_runtime_contract.py",
        )
        for target in python_targets:
            result = run([sys.executable, "-m", "py_compile", str(target)], cwd=repo)
            checks.append({"name": f"py_compile:{target.name}", "exitCode": result.returncode, "output": result.stdout})
            require(result.returncode == 0, f"Python syntax failed for {target}:\n{result.stdout}")

    status = git(repo, "status", "--short", "--untracked-files=all")
    summary = {
        "schema": "novelty01-premerge-repair-v1",
        "branch": branch,
        "headBeforeRepair": head,
        "sourceBaseline": SOURCE_BASELINE,
        "backup": str(backup_root),
        "changed": sorted(set(changed)),
        "removed": sorted(removed),
        "staticChecks": checks,
        "gitStatus": status.stdout.splitlines(),
        "commitPerformed": False,
        "pushPerformed": False,
    }
    summary_path = backup_root / "repair_summary.json"
    summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print("============================================================")
    print("NOVELTY01 69.9 PRE-MERGE REPAIR APPLIED")
    print("============================================================")
    print(f"Repository : {repo}")
    print(f"Branch     : {branch}")
    print(f"HEAD       : {head}")
    print(f"Backup     : {backup_root}")
    print(f"Summary    : {summary_path}")
    print("No commit or push was performed.")
    print("============================================================")
    if status.stdout.strip():
        print(status.stdout.rstrip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
