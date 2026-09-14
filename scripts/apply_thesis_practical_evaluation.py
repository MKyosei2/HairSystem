from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

BASE_COMMIT = "05346c8505cf08367002c7c7af03c3bc9a91255e"
MARKER = "HAIRGPU_THESIS_PRACTICAL_EDIT_EVAL_V1"

EXPECTED = {
    "apps/dx11_viewer/src/GpuHairViewer.cpp":
        "34f9b9e311a0bc9855ed882a0baa37bb108ce688",
    "apps/dx11_viewer/src/GpuHairViewer.h":
        "f78b17e1fe86e748ea496cb64b50a6dbd4ef2b02",
    "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl":
        "72de03ecf2eda4ae20044f2026bf3f00517e8412",
    "apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl":
        "d6ecfd0c1b51a0c747eb8176c650f649d6ce59ba",
    "apps/dx11_viewer/src/GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl":
        "5c4aabd22fc385d289c876b63ee55a8a56857185",
    "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl":
        "f0885c23db15c974558b991c8b54be430aafc28d",
}

def sha1_git_blob(path: Path) -> str:
    data = path.read_bytes()
    header = f"blob {len(data)}\\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()

def git_head(repo: Path) -> str | None:
    try:
        cp = subprocess.run(
            ["git", "-C", str(repo), "rev-parse", "HEAD"],
            text=True, capture_output=True, check=False
        )
        if cp.returncode == 0:
            return cp.stdout.strip()
    except OSError:
        pass
    return None

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one anchor, found {count}"
        )
    return text.replace(old, new, 1)

def patch_header(text: str) -> str:
    if MARKER in text:
        return text
    anchor = (
        "        void CommitEditedPointIndices(const std::vector<uint32_t>& pointIndices);\\n"
        "        std::vector<uint32_t> ExpandGuideFamilies"
    )
    replacement = (
        "        void CommitEditedPointIndices(const std::vector<uint32_t>& pointIndices);\\n"
        f"        // {MARKER}\\n"
        "        void RecordThesisPracticalEditEpisode(const std::vector<uint32_t>& pointIndices);\\n"
        "        bool InjectThesisPracticalLocalEdit();\\n"
        "        void ApplyThesisPracticalPendingEditInvalidation();\\n"
        "        std::vector<uint32_t> ExpandGuideFamilies"
    )
    return replace_once(text, anchor, replacement, "GpuHairViewer.h")

def patch_cpp(text: str) -> str:
    if "19_thesis_practical_edit_evaluation.inl" in text:
        return text
    include_anchor = (
        '#include "hairgpu/HairNoveltyTelemetry.h"\\n'
        '#include "hairgpu/Authoring.h"'
    )
    text = replace_once(
        text,
        include_anchor,
        '#include "hairgpu/HairNoveltyTelemetry.h"\\n'
        '#include "hairgpu/ThesisEditEvaluation.h"\\n'
        '#include "hairgpu/Authoring.h"',
        "GpuHairViewer.cpp header include",
    )
    tail_anchor = (
        '#include "GpuHairViewer_parts/15_shutdown_and_tail.inl"\\n'
        '#undef StepSingleFrame'
    )
    text = replace_once(
        text,
        tail_anchor,
        '#include "GpuHairViewer_parts/15_shutdown_and_tail.inl"\\n'
        '#include "GpuHairViewer_parts/19_thesis_practical_edit_evaluation.inl"\\n'
        '#undef StepSingleFrame',
        "GpuHairViewer.cpp ordered include",
    )
    return text

def patch_part12(text: str) -> str:
    if "RecordThesisPracticalEditEpisode(sorted);" in text:
        return text
    function_pos = text.find(
        "void GpuHairViewer::CommitEditedPointIndices("
    )
    if function_pos < 0:
        raise RuntimeError("part12: CommitEditedPointIndices not found")
    empty_pos = text.find("if (sorted.empty()) return;", function_pos)
    if empty_pos < 0:
        raise RuntimeError("part12: sorted.empty anchor not found")
    insert_pos = empty_pos + len("if (sorted.empty()) return;")
    insertion = (
        "\\n\\n        // " + MARKER + "\\n"
        "        RecordThesisPracticalEditEpisode(sorted);"
    )
    return text[:insert_pos] + insertion + text[insert_pos:]

def patch_part03(text: str) -> str:
    marker = "const bool thesisLocalEdit = InjectThesisPracticalLocalEdit();"
    if marker in text:
        return text
    old = (
        "                ++m_editVersion;\\n"
        "                m_forceAllSegmentGroupsDirty = true;\\n"
        "                ++m_scenarioEditInvalidationCount;"
    )
    new = (
        f"                // {MARKER}\\n"
        "                const bool thesisLocalEdit = InjectThesisPracticalLocalEdit();\\n"
        "                if (!thesisLocalEdit)\\n"
        "                {\\n"
        "                    ++m_editVersion;\\n"
        "                    m_forceAllSegmentGroupsDirty = true;\\n"
        "                }\\n"
        "                ++m_scenarioEditInvalidationCount;"
    )
    return replace_once(text, old, new, "part03 S11 local edit")

def patch_part10(text: str) -> str:
    marker = "const bool thesisLocalEdit = InjectThesisPracticalLocalEdit();"
    if marker in text:
        return text
    old = (
        "                ++m_editVersion;\\n"
        "                ++m_scenarioEditInvalidationCount;\\n"
        "                m_forceAllSegmentGroupsDirty = true;"
    )
    new = (
        f"                // {MARKER}\\n"
        "                const bool thesisLocalEdit = InjectThesisPracticalLocalEdit();\\n"
        "                if (!thesisLocalEdit)\\n"
        "                {\\n"
        "                    ++m_editVersion;\\n"
        "                    m_forceAllSegmentGroupsDirty = true;\\n"
        "                }\\n"
        "                ++m_scenarioEditInvalidationCount;"
    )
    return replace_once(text, old, new, "part10 timeline local edit")

def patch_part16(text: str) -> str:
    if "ApplyThesisPracticalPendingEditInvalidation();" in text:
        return text
    anchor = (
        "        ApplyNovelty01DesignScenarioForTick(dt);\\n\\n"
        "        for (Novelty01RuntimeState::ReadbackSlot& slot : runtime.readbacks)"
    )
    replacement = (
        "        ApplyNovelty01DesignScenarioForTick(dt);\\n"
        f"        // {MARKER}\\n"
        "        ApplyThesisPracticalPendingEditInvalidation();\\n\\n"
        "        for (Novelty01RuntimeState::ReadbackSlot& slot : runtime.readbacks)"
    )
    return replace_once(text, anchor, replacement, "part16 pending invalidation")

def patch_runtime_shader(text: str) -> str:
    marker = "HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1"
    if marker in text:
        return text
    anchor = (
        "    uint hardFlags = 0u;\\n"
        "    const bool finite ="
    )
    replacement = (
        "    uint hardFlags = 0u;\\n"
        "    // HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1\\n"
        "    // A thesis local edit dirties only selected Blocks on the GPU.\\n"
        "    // Promote that Block-local reason to the existing hard Edit flag so\\n"
        "    // the ordinary Dynamic/repair/cache machinery owns the update.\\n"
        "    if ((block.dirtyFlags & N01_DIRTY_EDIT) != 0u)\\n"
        "    {\\n"
        "        hardFlags |= N01_DIRTY_EDIT;\\n"
        "    }\\n"
        "    const bool finite ="
    )
    return replace_once(
        text, anchor, replacement, "Novelty01 runtime shader"
    )

def copy_payload(package_root: Path, repo: Path, backup: Path) -> list[str]:
    copied = []
    payload = package_root / "payload"
    for source in sorted(payload.rglob("*")):
        if not source.is_file():
            continue
        relative = source.relative_to(payload)
        destination = repo / relative
        if destination.exists():
            backup_target = backup / relative
            backup_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(destination, backup_target)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        copied.append(relative.as_posix())
    return copied

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    repo = args.repo.resolve()
    package_root = Path(__file__).resolve().parents[1]

    required = [
        repo / "apps/dx11_viewer/src/GpuHairViewer.cpp",
        repo / "apps/dx11_viewer/src/GpuHairViewer.h",
        repo / "include/hairgpu/Types.h",
    ]
    missing = [str(p) for p in required if not p.is_file()]
    if missing:
        raise RuntimeError("Not a HairSystem repository or files are missing: " + ", ".join(missing))

    head = git_head(repo)
    if head and head != BASE_COMMIT and not args.force:
        raise RuntimeError(
            f"HEAD {head} differs from supported base {BASE_COMMIT}. "
            "Re-run with --force only after reviewing the source anchors."
        )

    # Blob checks are advisory when a working tree already contains local edits.
    blob_mismatches = []
    for relative, expected in EXPECTED.items():
        path = repo / relative
        if not path.is_file():
            raise RuntimeError(f"Required source file missing: {relative}")
        actual = sha1_git_blob(path)
        if actual != expected:
            blob_mismatches.append((relative, expected, actual))

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = repo / f".thesis_practical_backup_{stamp}"
    backup.mkdir(parents=True, exist_ok=False)

    modified_sources = [
        "apps/dx11_viewer/src/GpuHairViewer.cpp",
        "apps/dx11_viewer/src/GpuHairViewer.h",
        "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl",
        "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl",
    ]
    for relative in modified_sources:
        src = repo / relative
        dst = backup / relative
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    copied = copy_payload(package_root, repo, backup)

    patch_functions = {
        "apps/dx11_viewer/src/GpuHairViewer.cpp": patch_cpp,
        "apps/dx11_viewer/src/GpuHairViewer.h": patch_header,
        "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl": patch_part03,
        "apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl": patch_part10,
        "apps/dx11_viewer/src/GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl": patch_part12,
        "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl": patch_part16,
        "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl": patch_runtime_shader,
    }

    patched = []
    try:
        for relative, function in patch_functions.items():
            path = repo / relative
            text = path.read_text(encoding="utf-8")
            updated = function(text)
            path.write_text(updated, encoding="utf-8", newline="\\n")
            patched.append(relative)
    except Exception:
        # Fail closed: restore every source file already backed up.
        for relative in modified_sources:
            backup_source = backup / relative
            if backup_source.is_file():
                destination = repo / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(backup_source, destination)
        raise

    # Static contract checks.
    checks = {
        "cpp_part19": '19_thesis_practical_edit_evaluation.inl' in
            (repo / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8"),
        "part12_record": "RecordThesisPracticalEditEpisode(sorted);" in
            (repo / "apps/dx11_viewer/src/GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl").read_text(encoding="utf-8"),
        "part16_apply": "ApplyThesisPracticalPendingEditInvalidation();" in
            (repo / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8"),
        "shader_local_block": "HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1" in
            (repo / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl").read_text(encoding="utf-8"),
        "new_shader": (repo / "apps/dx11_viewer/shaders/HairThesisLocalEditInvalidate.hlsl").is_file(),
        "report": (repo / "tools/final_stage/hairgpu_thesis_practical_report.py").is_file(),
    }
    if not all(checks.values()):
        raise RuntimeError("Static contract check failed: " + json.dumps(checks))

    manifest = {
        "schema": "hairgpu-thesis-practical-overlay-apply-v1",
        "baseCommit": BASE_COMMIT,
        "headObserved": head,
        "force": bool(args.force),
        "backup": str(backup),
        "sourceBlobMismatchesBeforePatch": [
            {"path": p, "expected": e, "actual": a}
            for p, e, a in blob_mismatches
        ],
        "patchedExistingFiles": patched,
        "copiedPayloadFiles": copied,
        "checks": checks,
    }
    (repo / "THESIS_PRACTICAL_PATCH_APPLIED.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
