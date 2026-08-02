from __future__ import annotations

import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OLD = "37ff9749fe530e8ab08d849892c4c6326a2578bf"
NEW = "4f4c6210d7be40a0b5d522789d2684ade86d3de9"


def replace_all(relative: str, old: str, new: str) -> None:
    path = ROOT / relative
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"expected marker missing in {relative}: {old}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


def patch_hlsl_fxc_compatibility() -> None:
    path = ROOT / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
    text = path.read_text(encoding="utf-8")

    triangle_declarations = text.count("const TriangleData triangle")
    if triangle_declarations < 1:
        raise RuntimeError("current SG-APT HLSL has no TriangleData compatibility target")
    text = text.replace("const TriangleData triangle", "const TriangleData triIndices")
    text = text.replace("triangle.a", "triIndices.a")
    text = text.replace("triangle.b", "triIndices.b")
    text = text.replace("triangle.c", "triIndices.c")

    payload_old = (
        "        DynamicPayload payload = extension.payloadIndex < gPayloadCapacity\n"
        "            ? N01Payloads[extension.payloadIndex]\n"
        "            : (DynamicPayload)0;"
    )
    payload_new = (
        "        DynamicPayload payload = (DynamicPayload)0;\n"
        "        if (extension.payloadIndex < gPayloadCapacity)\n"
        "        {\n"
        "            payload = N01Payloads[extension.payloadIndex];\n"
        "        }"
    )
    payload_count = text.count(payload_old)
    if payload_count < 1:
        raise RuntimeError("current SG-APT HLSL has no DynamicPayload compatibility target")
    text = text.replace(payload_old, payload_new)

    candidate_old = (
        "    SharedPointCandidate winner =\n"
        "        N01FeatureEnabled(N01_FEATURE_STABLE_SHARED_POINT)\n"
        "        ? (N01CandidateBetter(child, owner) ? child : owner)\n"
        "        : owner;"
    )
    candidate_new = (
        "    SharedPointCandidate winner = owner;\n"
        "    if (N01FeatureEnabled(N01_FEATURE_STABLE_SHARED_POINT) &&\n"
        "        N01CandidateBetter(child, owner))\n"
        "    {\n"
        "        winner = child;\n"
        "    }"
    )
    candidate_count = text.count(candidate_old)
    if candidate_count < 1:
        raise RuntimeError("current SG-APT HLSL has no SharedPointCandidate compatibility target")
    text = text.replace(candidate_old, candidate_new)

    path.write_text(text, encoding="utf-8", newline="\n")
    packaged = ROOT / "files/apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
    packaged.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, packaged)


def patch_formal_contract_input() -> None:
    path = ROOT / "replacement/docs/experiments/NOVELTY01_FORMAL_CONTRACT.json"
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    baselines = payload.get("baselines")
    if not isinstance(baselines, dict):
        raise RuntimeError("formal contract baseline table is missing")
    for variant in ("B1", "B8"):
        row = baselines.get(variant)
        if not isinstance(row, dict):
            raise RuntimeError(f"formal contract missing {variant}")
        row["method"] = variant
        row["usesControlledDispatcher"] = False
        row["usesSgaptRepresentation"] = False
        row["requiresDedicatedPerFrameRoute"] = True
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def patch_packager() -> None:
    path = ROOT / "tools/final_stage/build_novelty01_69_10_complete_repair_package.py"
    text = path.read_text(encoding="utf-8")

    old_ending = '        dtext = dtext.rstrip() + status + "\\n"'
    new_ending = '        dtext = dtext.rstrip() + status.rstrip() + "\\n"'
    if old_ending in text:
        text = text.replace(old_ending, new_ending, 1)

    hlsl_entry = '    "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl",\n'
    cpp_entry = '    "apps/dx11_viewer/src/GpuHairViewer.cpp",\n'
    if hlsl_entry not in text.split("REPLACEMENT_FILES =", 1)[0]:
        if cpp_entry not in text:
            raise RuntimeError("packager FILES_REPLACEMENTS insertion marker missing")
        text = text.replace(cpp_entry, hlsl_entry + cpp_entry, 1)

    incompatible = (
        '    "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl",\n',
        '    "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl",\n',
        '    "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl",\n',
        '    "include/hairgpu/SegmentGroupAdaptivePayload.h",\n',
    )
    before, separator, after = text.partition("REPLACEMENT_FILES = [")
    if not separator:
        raise RuntimeError("packager REPLACEMENT_FILES table missing")
    table, close, suffix = after.partition("]\n\nHEADER_DECLARATIONS")
    if not close:
        raise RuntimeError("packager REPLACEMENT_FILES closing marker missing")
    for entry in incompatible:
        if entry not in table:
            raise RuntimeError(f"incompatible replacement entry missing: {entry.strip()}")
        table = table.replace(entry, "", 1)
    text = before + separator + table + close + suffix

    delete_anchor = '    "patches/hairgpu_final_run_formal_asset_binding.patch",\n]'
    delete_replacement = (
        '    "patches/hairgpu_final_run_formal_asset_binding.patch",\n'
        '    ".github/workflows/build-novelty01-complete-repair-artifact.yml",\n'
        '    "tools/final_stage/build_novelty01_69_10_complete_repair_package.py",\n'
        '    "tools/final_stage/prepare_novelty01_repair_inputs.py",\n'
        ']'
    )
    if delete_anchor not in text:
        raise RuntimeError("packager DELETE_PATHS insertion marker missing")
    text = text.replace(delete_anchor, delete_replacement, 1)

    replacements = {
        "- 新世代SG-APT Header/HLSL/Runtime/Evidenceの正式パス昇格":
            "- 現行69.10 SG-APT Runtimeを維持し、FXC互換修正を正式パスへ適用",
        "- B1/B8独立BaselineとVariant Execution Contract":
            "- B1/B8を既存の独立Method経路へ固定し、PだけをSG-APTへ接続",
        "- B1/B8は`VariantExecutionContract`でSG-APT表現資源を禁止する。":
            "- B1/B8は既存B1/B8 Methodを直接使用し、PだけをSG-APT経路へ通す。",
        "- B2～B7: `VariantExecutionContract`が必須Featureと禁止FeatureをFail-Closedで検証する。":
            "- B2～B7: PのControlled Route上でFeature Maskを固定し、各Baselineの除外要素を検証する。",
        "- B1/B8は提案表現を共有しない独立Baseline契約で実行する。":
            "- B1/B8は既存の独立Method IDで実行し、SG-APT Runtimeを生成しない。",
    }
    for old, new in replacements.items():
        if old not in text:
            raise RuntimeError(f"packager documentation marker missing: {old}")
        text = text.replace(old, new)

    pattern = re.compile(
        r"    def test_header_and_baseline_contracts_are_formal\(self\) -> None:\n.*?"
        r"\n    def test_branch_and_asset_contracts_are_consistent",
        flags=re.DOTALL,
    )
    completion_block = '''    def test_header_and_baseline_contracts_are_formal(self) -> None:
        cpp = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        runtime = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8")
        matrix = (ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py").read_text(encoding="utf-8")
        contract = json.loads((ROOT / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json").read_text(encoding="utf-8-sig"))
        self.assertNotIn("#define private", cpp)
        self.assertIn("struct Novelty01RuntimeState;", header)
        self.assertIn("ExperimentMethodId::P_IntegratedSegmentGroup", runtime)
        self.assertIn("ExecuteExperimentSimulationTickLegacy(dt);", runtime)
        self.assertIn('baseline_contract.get("method", "P")', matrix)
        self.assertEqual(contract["baselines"]["B1"]["method"], "B1")
        self.assertEqual(contract["baselines"]["B8"]["method"], "B8")

    def test_branch_and_asset_contracts_are_consistent'''
    text, count = pattern.subn(lambda _: completion_block, text, count=1)
    if count != 1:
        raise RuntimeError(f"packager completion-test block replacement count={count}")

    path.write_text(text, encoding="utf-8", newline="\n")


def main() -> int:
    replace_all(
        "files/tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
        OLD,
        NEW,
    )
    replace_all(
        "files/tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
        "Novelty01 69.9 baseline",
        "Novelty01 69.10 baseline",
    )
    replace_all(
        "files/tests/final_stage/test_novelty01_runtime_contract.py",
        OLD,
        NEW,
    )
    patch_hlsl_fxc_compatibility()
    patch_formal_contract_input()
    patch_packager()
    print("Prepared Main-compatible Novelty01 69.10 repair inputs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
