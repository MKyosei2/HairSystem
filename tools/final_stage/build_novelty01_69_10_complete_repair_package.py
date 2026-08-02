from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

SOURCE_BASELINE = "4f4c6210d7be40a0b5d522789d2684ade86d3de9"
PACKAGE_NAME = "HairSystem_Novelty01_69_10_CompleteRepair"
SNAPSHOT_NAME = "HairSystem_Novelty01_69_10_RepairedSourceSnapshot"

ROOT = Path(__file__).resolve().parents[2]
ARTIFACTS = ROOT / "artifacts" / "novelty01_69_10_complete_repair"
PACKAGE_ROOT = ARTIFACTS / PACKAGE_NAME
FILES_ROOT = PACKAGE_ROOT / "files"
WRITTEN: set[str] = set()

DELETE_PATHS = [
    "files",
    "overlay",
    "replacement",
    "tools/repair_novelty01_premerge.py",
    "tools/apply_novelty01_69_3_complete.py",
    "tools/apply_novelty01_69_4_complete.py",
    "tools/apply_novelty01_complete.py",
    "tools/apply_novelty01_sgapt.py",
    "tools/verify_novelty01_sgapt.py",
    "tools/final_stage/apply_formal_asset_binding_patch.py",
    "patches/hairgpu_final_run_formal_asset_binding.patch",
]

FILES_REPLACEMENTS = [
    ".github/workflows/novelty01-formal-gate.yml",
    ".github/workflows/windows-build-test.yml",
    "apps/dx11_viewer/src/GpuHairViewer.cpp",
    "docs/experiments/NOVELTY01_MAIN_INTEGRATION_CHECKLIST.md",
    "tests/final_stage/test_novelty01_complete_contract.py",
    "tests/final_stage/test_novelty01_runtime_contract.py",
    "tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
    "tools/final_stage/hairgpu_novelty01_preflight.py",
]

REPLACEMENT_FILES = [
    "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl",
    "include/hairgpu/SegmentGroupAdaptivePayload.h",
    "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json",
    "docs/experiments/NOVELTY01_IMPLEMENTATION_TRACEABILITY.json",
    "docs/experiments/NOVELTY01_RESOURCE_BINDING_CONTRACT.md",
    "docs/sample_assets/novelty01_frozen_parameters.json",
    "tools/final_stage/Run_Novelty01_Complete_Verification.bat",
    "tools/final_stage/Run_Novelty01_Formal_Matrix_And_Gate.bat",
    "tools/final_stage/hairgpu_novelty01_formal_suite.py",
    "tools/final_stage/hairgpu_novelty01_gate.py",
    "tools/final_stage/hairgpu_novelty01_matrix.py",
    "tools/final_stage/hairgpu_novelty01_report.py",
    "tools/final_stage/hairgpu_novelty01_resource_audit.py",
]

HEADER_DECLARATIONS = """

        // Novelty 01 SG-APT runtime declarations.
        // Implemented by ordered parts 16-18 and selected only by the formal
        // method/variant dispatcher. These stay private and must not be injected
        // by rewriting the C++ access-control keyword.
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


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    print("$", " ".join(command))
    print(result.stdout)
    if check and result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{result.stdout}"
        )
    return result


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    WRITTEN.add(rel(path))


def copy_file(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    WRITTEN.add(rel(destination))


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one occurrence, found {count}: {old!r}")
    return text.replace(old, new, 1)


def promote_sources() -> None:
    for relative in FILES_REPLACEMENTS:
        copy_file(ROOT / "files" / relative, ROOT / relative)

    for relative in REPLACEMENT_FILES:
        copy_file(ROOT / "replacement" / relative, ROOT / relative)

    scenario_source = ROOT / "replacement/benchmarks/novelty01/sgapt_scenarios"
    scenario_target = ROOT / "benchmarks/novelty01/sgapt_scenarios"
    for index in range(1, 19):
        name = f"S{index:02d}.json"
        copy_file(scenario_source / name, scenario_target / name)


def patch_header() -> None:
    path = ROOT / "apps/dx11_viewer/src/GpuHairViewer.h"
    text = path.read_text(encoding="utf-8")
    if "struct Novelty01RuntimeState;" not in text:
        marker = "        struct StrandEditSnapshot;"
        text = replace_once(text, marker, marker + HEADER_DECLARATIONS, "header insertion")
    for marker in (
        "struct Novelty01RuntimeState;",
        "struct Novelty01RenderCacheState;",
        "void ExecuteNovelty01ExperimentTick(float dt);",
        "void StepNovelty01SingleFrame();",
        "void RenderLegacy();",
        "void ShutdownNovelty01Routed();",
    ):
        if marker not in text:
            raise RuntimeError(f"GpuHairViewer.h missing declaration: {marker}")
    write_text(path, text)


def patch_cpp() -> None:
    path = ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp"
    text = path.read_text(encoding="utf-8")
    if "#define private" in text or "#undef private" in text:
        raise RuntimeError("69.10 replacement GpuHairViewer.cpp still rewrites private")
    required = (
        '#include "GpuHairViewer.h"',
        "16_segment_group_adaptive_payload_complete.inl",
        "17_novelty01_formal_evidence.inl",
        "18_novelty01_render_dirty_range_consumer.inl",
        "02a_required_runtime_foundation.inl",
    )
    for marker in required:
        if marker not in text:
            raise RuntimeError(f"GpuHairViewer.cpp missing route marker: {marker}")
    write_text(path, text)


def patch_preflight() -> None:
    path = ROOT / "tools/final_stage/hairgpu_novelty01_preflight.py"
    text = path.read_text(encoding="utf-8")
    text = re.sub(
        r'SOURCE_BASELINE_COMMIT\s*=\s*"[0-9a-fA-F]{40}"',
        f'SOURCE_BASELINE_COMMIT = "{SOURCE_BASELINE}"',
        text,
        count=1,
    )
    if "tools/verify_novelty01_sgapt.py" not in text:
        needle = '    "patches/hairgpu_final_run_formal_asset_binding.patch",\n)'
        replacement = (
            '    "patches/hairgpu_final_run_formal_asset_binding.patch",\n'
            '    "files",\n'
            '    "tools/repair_novelty01_premerge.py",\n'
            '    "tools/verify_novelty01_sgapt.py",\n'
            ')'
        )
        text = replace_once(text, needle, replacement, "preflight obsolete paths")
    write_text(path, text)


def patch_readiness() -> None:
    path = ROOT / "tools/final_stage/hairgpu_novelty01_readiness_smoke.py"
    text = path.read_text(encoding="utf-8")
    old = (
        '    if branch != "main":\n'
        '        raise NoveltyReadinessError(\n'
        '            f"main branch required: observed={branch}"\n'
        '        )'
    )
    new = (
        '    allowed_branches = {"Novelty01", "main"}\n'
        '    if branch not in allowed_branches:\n'
        '        raise NoveltyReadinessError(\n'
        '            "Novelty01 or main branch required: "\n'
        '            f"observed={branch}"\n'
        '        )'
    )
    if old in text:
        text = text.replace(old, new, 1)
    elif "allowed_branches = {\"Novelty01\", \"main\"}" not in text:
        raise RuntimeError("readiness branch guard layout changed")
    write_text(path, text)


def patch_matrix() -> None:
    path = ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py"
    text = path.read_text(encoding="utf-8")
    text = text.replace('"PYTHONUTF8": "0"', '"PYTHONUTF8": "1"')
    if "CANONICAL_SCENARIO_ROOT_RELATIVE" not in text:
        raise RuntimeError("promoted matrix lacks canonical scenario root")
    write_text(path, text)


def patch_freeze_tool() -> None:
    path = ROOT / "tools/final_stage/freeze_novelty01_asset.py"
    text = path.read_text(encoding="utf-8")
    text = text.replace('branch != "Novelty"', 'branch != "Novelty01"')
    text = text.replace('requires branch Novelty, observed:', 'requires branch Novelty01, observed:')
    text = text.replace('"groupsPerBlock": 8', '"groupsPerBlock": 32')
    text = text.replace('int(contract.get("required_groups_per_block", 8))', 'int(contract.get("required_groups_per_block", 32))')
    if 'MANIFEST_VERSION = "novelty01-100k-v1.6"' not in text:
        raise RuntimeError("unexpected freeze manifest version")
    write_text(path, text)


def patch_asset_manifest() -> None:
    path = ROOT / "docs/sample_assets/novelty01_100k_asset_manifest.json"
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    payload["manifestVersion"] = "novelty01-100k-v1.6"
    payload["sourceBranch"] = "Novelty01"
    generation = payload.setdefault("generation", {})
    generation["groupsPerBlock"] = 32
    write_text(path, json.dumps(payload, ensure_ascii=False, indent=2) + "\n")


def patch_frozen_parameters() -> None:
    path = ROOT / "docs/sample_assets/novelty01_frozen_parameters.json"
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    payload["branch"] = "Novelty01"
    payload["baseMainCommit"] = "8bf5c77bc7826575cecfa1943f842642d1538a6c"
    payload["sourceCommit"] = SOURCE_BASELINE
    payload["freezeRevision"] = "69.10-complete-repair"
    write_text(path, json.dumps(payload, ensure_ascii=False, indent=2) + "\n")


def patch_formal_contract() -> None:
    path = ROOT / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json"
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    payload["documentRevision"] = "69.10-complete-repair"
    payload["branch"] = "Novelty01"
    payload["sourceCommit"] = SOURCE_BASELINE
    source_of_truth = payload.setdefault("sourceOfTruth", {})
    source_of_truth["overlayDirectoryAllowed"] = False
    source_of_truth["oldApplyScriptsAllowedInRepository"] = False
    source_of_truth["replacementDirectoryAllowed"] = False
    source_of_truth["distributionCopiesMayRemainUntilCompressionPass"] = False
    source_of_truth["distributionCopiesMustNotBeReferencedByBuild"] = True
    source_of_truth["compressionDeferredByUser"] = False
    write_text(path, json.dumps(payload, ensure_ascii=False, indent=2) + "\n")


def patch_docs() -> None:
    checklist = ROOT / "docs/experiments/NOVELTY01_MAIN_INTEGRATION_CHECKLIST.md"
    text = checklist.read_text(encoding="utf-8")
    text = re.sub(
        r"^# Novelty01 .*?Main統合チェックリスト$",
        "# Novelty01 69.10完全修復・Main統合チェックリスト",
        text,
        count=1,
        flags=re.MULTILINE,
    )
    appendix = f"""

## 69.10完全修復のSource of Truth

- 修復基準Commit: `{SOURCE_BASELINE}`
- `files/`、`overlay/`、`replacement/`、旧Apply/Patcher/Verifierは正式ソースから除去する。
- Novelty01宣言は`GpuHairViewer.h`のprivate領域へ正式配置する。
- B1/B8は`VariantExecutionContract`でSG-APT表現資源を禁止する。
- S01～S18、Formal Contract、Frozen Parameters、Gate、Report、Resource Auditを正式パスに固定する。
- Readinessは`Novelty01`と統合後`main`の両方を許可する。
- Asset Freezeは`Novelty01`、manifest v1.6、groupsPerBlock=32を要求する。

この修復はSource/実験経路を完成させるものであり、Release x64、FXC、実GPU Readiness、Formal MatrixのPASS結果そのものではない。
"""
    if "69.10完全修復のSource of Truth" not in text:
        text = text.rstrip() + appendix
    write_text(checklist, text)

    protocol = ROOT / "docs/experiments/NOVELTY01_SAFE_EVALUATION_PROTOCOL.md"
    ptext = protocol.read_text(encoding="utf-8")
    ptext = ptext.replace('"branch": "Novelty01",', '"branch": "Novelty01",')
    write_text(protocol, ptext)

    design = ROOT / "設計書/01_TopNovelty_NewStrandDataType.md"
    dtext = design.read_text(encoding="utf-8")
    status = f"""

---

## 実装基準追記：Novelty01 69.10完全修復

- 実装基準Commit：`{SOURCE_BASELINE}`
- Fixed Core、Optional Dynamic Payload、Payload Owner/Lifetime、Stable Compaction、Shared Point single-writer、Follower/Render Dirty Rangeを正式Runtimeへ接続する。
- B1/B8は提案表現を共有しない独立Baseline契約で実行する。
- `files/`、`overlay/`、`replacement/`および旧Patcher/Apply Scriptは正式実装に含めない。
- 正式な実装対応表は`設計書/01A_SGAPT_Implementation_Contract_and_Evidence.md`、実験契約は`docs/experiments/NOVELTY01_FORMAL_CONTRACT.json`をSource of Truthとする。
- 本追記は実装経路の定義であり、60 FPS、品質、安全性、新規性を実験前に保証するものではない。
"""
    if "実装基準追記：Novelty01 69.10完全修復" not in dtext:
        dtext = dtext.rstrip() + status + "\n"
    write_text(design, dtext)


def create_implementation_contract() -> None:
    path = ROOT / "設計書/01A_SGAPT_Implementation_Contract_and_Evidence.md"
    content = f"""# SG-APT 実装契約・証拠対応表

## 1. 固定識別情報

- Repository: `MKyosei2/HairSystem`
- Branch: `Novelty01`
- 修復基準Commit: `{SOURCE_BASELINE}`
- 提案: GPU-Resident Segment-Group Adaptive Payload Type（SG-APT）

## 2. 正式Source of Truth

| 契約 | 正式ファイル |
|---|---|
| C++型・Variant契約 | `include/hairgpu/SegmentGroupAdaptivePayload.h` |
| SG-APT GPU処理 | `apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl` |
| Runtime/Dispatch | `apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl` |
| Ground Truth/Telemetry | `apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl` |
| Render Dirty Consumer | `apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl` |
| Formal Contract | `docs/experiments/NOVELTY01_FORMAL_CONTRACT.json` |
| Frozen Parameters | `docs/sample_assets/novelty01_frozen_parameters.json` |
| Canonical Scenarios | `benchmarks/novelty01/sgapt_scenarios/S01.json`～`S18.json` |
| Matrix/Gate/Report | `tools/final_stage/hairgpu_novelty01_*.py` |

## 3. 必須型不変条件

1. Segment Groupの固定IndexはTopologyが不変な間変化しない。
2. Static状態ではOptional Dynamic Payloadを所有しない。
3. Dynamic/Repair/ForcedDynamicでPayloadを参照する場合、Owner、Version、Generationが一致する。
4. Retire済みPayloadはSafe Reuse Epoch前に再利用しない。
5. Shared Pointは候補生成後、安定優先順位で単一Writerが確定する。
6. Version不一致、非有限値、Pool不足、Descriptor違反は黙って継続せずBaseline FallbackまたはSafe Stopへ移る。
7. Follower/Render Dirty RangeはDependency Versionと整合し、範囲外Dispatchを行わない。

## 4. Baseline独立性

- B0: MainのLegacy Full Dynamic。
- B1: SG-APT Core、Payload Pool、Owner Table、Descriptor、Compactionを使用しない独立Full Dynamic。
- B8: 一般的GPU最適化は許可するが、Fixed Core＋Optional Payload Lifetimeを使用しない独立Strongest Practical。
- B2～B7: `VariantExecutionContract`が必須Featureと禁止FeatureをFail-Closedで検証する。
- A1～A15: 除去対象Featureが実行時Feature Maskに残っていないことを検証する。

## 5. 証拠段階

| 段階 | 証明すること | 証明しないこと |
|---|---|---|
| Source/Contract | 型、Entry Point、経路、固定条件の存在 | 実GPUの正しさ・性能 |
| Build/Shader | Release x64と全HLSL Entry PointがCompile | 長時間安定・60 FPS |
| Readiness | P/S01、100k、600測定フレーム、出力経路 | 論文性能主張 |
| Formal Matrix | B0～B8/P/Ablationの比較可能データ | 統計的優位性 |
| Statistical Gate | 事前定義Gateと信頼区間を満たす | 未測定GPUへの一般化 |

## 6. Main統合禁止条件

- `files/`、`overlay/`、`replacement/`、旧Apply/Patcher/Verifierが残る。
- `GpuHairViewer.cpp`に`#define private`が残る。
- Root Workflowが存在しない、またはRuntime Patcherを実行する。
- Novelty01 ReadinessがBranch条件の矛盾で起動できない。
- Frozen AssetのManifest Version、Branch、groupsPerBlockが一致しない。
- B1/B8がSG-APT表現資源をBindする。
- Release x64、FXC、Native/Shader Self-Test、P/S01 Readinessのいずれかが未PASS。
"""
    write_text(path, content)


def create_completion_test() -> None:
    path = ROOT / "tests/final_stage/test_novelty01_package_completion_contract.py"
    content = '''from __future__ import annotations

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Novelty01PackageCompletionContractTests(unittest.TestCase):
    def test_obsolete_distribution_trees_are_absent(self) -> None:
        for relative in (
            "files", "overlay", "replacement",
            "tools/repair_novelty01_premerge.py",
            "tools/verify_novelty01_sgapt.py",
            "tools/apply_novelty01_69_3_complete.py",
            "tools/apply_novelty01_69_4_complete.py",
            "tools/apply_novelty01_complete.py",
            "tools/apply_novelty01_sgapt.py",
            "tools/final_stage/apply_formal_asset_binding_patch.py",
            "patches/hairgpu_final_run_formal_asset_binding.patch",
        ):
            self.assertFalse((ROOT / relative).exists(), relative)

    def test_formal_source_tree_is_complete(self) -> None:
        required = (
            ".github/workflows/novelty01-formal-gate.yml",
            "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json",
            "docs/experiments/NOVELTY01_IMPLEMENTATION_TRACEABILITY.json",
            "docs/experiments/NOVELTY01_RESOURCE_BINDING_CONTRACT.md",
            "docs/sample_assets/novelty01_frozen_parameters.json",
            "tools/final_stage/Run_Novelty01_Complete_Verification.bat",
            "tools/final_stage/Run_Novelty01_Formal_Matrix_And_Gate.bat",
            "tools/final_stage/hairgpu_novelty01_formal_suite.py",
            "tools/final_stage/hairgpu_novelty01_gate.py",
            "tools/final_stage/hairgpu_novelty01_report.py",
            "tools/final_stage/hairgpu_novelty01_resource_audit.py",
            "設計書/01A_SGAPT_Implementation_Contract_and_Evidence.md",
        )
        for relative in required:
            self.assertTrue((ROOT / relative).is_file(), relative)
        for index in range(1, 19):
            self.assertTrue(
                (ROOT / f"benchmarks/novelty01/sgapt_scenarios/S{index:02d}.json").is_file()
            )

    def test_header_and_baseline_contracts_are_formal(self) -> None:
        cpp = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        contract = (ROOT / "include/hairgpu/SegmentGroupAdaptivePayload.h").read_text(encoding="utf-8")
        self.assertNotIn("#define private", cpp)
        self.assertIn("struct Novelty01RuntimeState;", header)
        for marker in (
            "enum class ExecutionRoute", "struct VariantExecutionContract",
            "GetVariantExecutionContract", "VariantContractAcceptsFeatureMask",
            "IndependentFullDynamicB1", "IndependentStrongestPracticalB8",
        ):
            self.assertIn(marker, contract)

    def test_branch_and_asset_contracts_are_consistent(self) -> None:
        readiness = (ROOT / "tools/final_stage/hairgpu_novelty01_readiness_smoke.py").read_text(encoding="utf-8")
        freeze = (ROOT / "tools/final_stage/freeze_novelty01_asset.py").read_text(encoding="utf-8")
        manifest = json.loads((ROOT / "docs/sample_assets/novelty01_100k_asset_manifest.json").read_text(encoding="utf-8-sig"))
        self.assertIn('{"Novelty01", "main"}', readiness)
        self.assertIn('branch != "Novelty01"', freeze)
        self.assertEqual(manifest["manifestVersion"], "novelty01-100k-v1.6")
        self.assertEqual(manifest["sourceBranch"], "Novelty01")
        self.assertEqual(manifest["generation"]["groupsPerBlock"], 32)


if __name__ == "__main__":
    unittest.main()
'''
    write_text(path, content)


def remove_obsolete() -> None:
    for relative in DELETE_PATHS:
        path = ROOT / relative
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()


def static_validation() -> dict[str, object]:
    run([sys.executable, "-m", "py_compile", *[
        str(ROOT / relative)
        for relative in sorted(WRITTEN)
        if relative.endswith(".py")
    ]])

    patterns = [
        "test_novelty01_complete_contract.py",
        "test_novelty01_runtime_contract.py",
        "test_novelty01_formal_asset_binding_contract.py",
        "test_novelty01_package_completion_contract.py",
    ]
    for pattern in patterns:
        run([
            sys.executable, "-m", "unittest", "discover",
            "-s", "tests/final_stage", "-p", pattern, "-v",
        ])

    result = run([
        sys.executable,
        "tools/final_stage/hairgpu_novelty01_preflight.py",
        "--repo", str(ROOT),
        "--json", str(ARTIFACTS / "novelty01_preflight_linux.json"),
    ], check=False)
    preflight_path = ARTIFACTS / "novelty01_preflight_linux.json"
    preflight = json.loads(preflight_path.read_text(encoding="utf-8"))
    errors = preflight.get("errors", [])
    if errors:
        raise RuntimeError(
            "Linux source preflight returned errors:\n" +
            json.dumps(errors, ensure_ascii=False, indent=2)
        )

    run(["git", "diff", "--check"])
    return {
        "pythonCompile": "PASS",
        "contractTests": "PASS",
        "linuxSourcePreflight": "PASS_WITH_FXC_WARNING_ALLOWED",
        "gitDiffCheck": "PASS",
        "preflightExitCode": result.returncode,
    }


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def create_apply_script() -> None:
    script = f'''param(
    [string]$Repo = "C:\\Users\\kyose\\source\\repos\\HairSystem",
    [switch]$AllowDirty
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$SourceBaseline = "{SOURCE_BASELINE}"
$PackageRoot = $PSScriptRoot
$FilesRoot = Join-Path $PackageRoot "files"
$Repo = [IO.Path]::GetFullPath($Repo)

if (-not (Test-Path -LiteralPath (Join-Path $Repo ".git"))) {{
    throw "Git管理されたHairSystemが必要です: $Repo"
}}
$branch = (& git.exe -C $Repo branch --show-current | Out-String).Trim()
if ($branch -notin @("Novelty01", "main")) {{
    throw "Novelty01または統合後mainが必要です。Current=$branch"
}}
& git.exe -C $Repo merge-base --is-ancestor $SourceBaseline HEAD
if ($LASTEXITCODE -ne 0) {{
    throw "HEADは69.10基準Commitの子孫ではありません。Required=$SourceBaseline"
}}
$status = @(git.exe -C $Repo status --porcelain --untracked-files=all)
if ($status.Count -gt 0 -and -not $AllowDirty) {{
    $status | ForEach-Object {{ Write-Host $_ }}
    throw "既存変更を保護するため停止しました。先にcommitまたはstashしてください。"
}}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$backup = Join-Path $Repo "out\\before_novelty01_69_10_complete_repair_$stamp"
New-Item -ItemType Directory -Force -Path $backup | Out-Null

Get-ChildItem -LiteralPath $FilesRoot -File -Recurse | ForEach-Object {{
    $relative = $_.FullName.Substring($FilesRoot.Length).TrimStart('\\','/').Replace('\\','/')
    $destination = Join-Path $Repo $relative
    if (Test-Path -LiteralPath $destination -PathType Leaf) {{
        $backupPath = Join-Path $backup $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backupPath) | Out-Null
        Copy-Item -LiteralPath $destination -Destination $backupPath -Force
    }}
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
}}

$deleteList = Get-Content -LiteralPath (Join-Path $PackageRoot "DELETE_PATHS.txt")
foreach ($relative in $deleteList) {{
    if ([string]::IsNullOrWhiteSpace($relative)) {{ continue }}
    $target = Join-Path $Repo $relative.Trim()
    if (Test-Path -LiteralPath $target) {{
        Remove-Item -LiteralPath $target -Recurse -Force
    }}
}}

& git.exe -C $Repo diff --check
if ($LASTEXITCODE -ne 0) {{ throw "git diff --check failed" }}

$pythonTargets = Get-ChildItem -LiteralPath (Join-Path $Repo "tools\\final_stage") -Filter "hairgpu_novelty01_*.py" -File
foreach ($target in $pythonTargets) {{
    py.exe -3 -m py_compile $target.FullName
    if ($LASTEXITCODE -ne 0) {{ throw "Python syntax failed: $($target.FullName)" }}
}}

Write-Host "Novelty01 69.10 complete repair applied."
Write-Host "Backup: $backup"
Write-Host "No commit, push, merge, or experiment was performed."
'''
    write_text(PACKAGE_ROOT / "Apply_CompleteRepair.ps1", script)


def create_readme() -> None:
    content = f"""# HairSystem Novelty01 69.10 完全修復ファイル

このZIPには、`Novelty01` 69.10（`{SOURCE_BASELINE}`）を基準に、修正・追加が必要なファイルを**修正後全文**で収録しています。既存Checkoutのルートへ上書きする構成です。

## 含まれる修正

- `GpuHairViewer.h`への正式宣言統合と`#define private`廃止
- Root GitHub Actions修復
- 新世代SG-APT Header/HLSL/Runtime/Evidenceの正式パス昇格
- B1/B8独立BaselineとVariant Execution Contract
- Readinessの`Novelty01`/`main`条件統一
- Asset FreezeのBranch、Manifest v1.6、groupsPerBlock=32統一
- S01～S18、Formal Contract、Frozen Parameters、Traceability、Resource Binding Contract
- Matrix、Formal Suite、Gate、Report、Resource Audit、Complete Verification
- 旧`files/`、`overlay/`、`replacement/`、Apply/Patcher/Verifierの削除

## 適用

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\\Apply_CompleteRepair.ps1 -Repo "C:\\Users\\kyose\\source\\repos\\HairSystem"
```

適用スクリプトはcommit、push、Main統合を行いません。既存変更がある場合は停止し、上書き前ファイルを`out/before_novelty01_69_10_complete_repair_*`へ保存します。

## 適用後の必須確認

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\\final_stage\\Run_Novelty01_Readiness_Smoke.ps1 `
  -Repo $PWD.Path
```

このパッケージの生成環境ではPython構文、契約テスト、Linux上のFail-Closed Preflight、`git diff --check`を実行します。WindowsのMSBuild、FXC、D3D11実GPU、60 FPS、Formal MatrixのPASSはWindows実機で別途必要です。
"""
    write_text(PACKAGE_ROOT / "README_APPLY_JA.md", content)


def create_package(validation: dict[str, object]) -> None:
    if PACKAGE_ROOT.exists():
        shutil.rmtree(PACKAGE_ROOT)
    FILES_ROOT.mkdir(parents=True, exist_ok=True)

    source_files = sorted(WRITTEN)
    for relative in source_files:
        source = ROOT / relative
        if not source.is_file():
            raise FileNotFoundError(source)
        destination = FILES_ROOT / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    (PACKAGE_ROOT / "DELETE_PATHS.txt").write_text(
        "\n".join(DELETE_PATHS) + "\n", encoding="utf-8", newline="\n"
    )
    create_apply_script()
    create_readme()

    manifest_entries: list[dict[str, object]] = []
    for path in sorted(PACKAGE_ROOT.rglob("*")):
        if path.is_file() and path.name != "MANIFEST.json":
            manifest_entries.append({
                "path": path.relative_to(PACKAGE_ROOT).as_posix(),
                "sizeBytes": path.stat().st_size,
                "sha256": sha256(path),
            })
    manifest = {
        "schema": "hairsystem-novelty01-complete-repair-v1",
        "sourceBranch": "Novelty01",
        "sourceCommit": SOURCE_BASELINE,
        "packageType": "complete modified-and-added files with preserved repository paths",
        "modifiedOrAddedFileCount": len(source_files),
        "deletePathCount": len(DELETE_PATHS),
        "validation": validation,
        "files": manifest_entries,
    }
    (PACKAGE_ROOT / "MANIFEST.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    package_zip = ARTIFACTS / f"{PACKAGE_NAME}.zip"
    if package_zip.exists():
        package_zip.unlink()
    with zipfile.ZipFile(package_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(PACKAGE_ROOT.rglob("*")):
            if path.is_file():
                archive.write(path, f"{PACKAGE_NAME}/{path.relative_to(PACKAGE_ROOT).as_posix()}")

    (ARTIFACTS / f"{PACKAGE_NAME}.sha256.txt").write_text(
        f"{sha256(package_zip)}  {package_zip.name}\n", encoding="utf-8"
    )


def create_source_snapshot() -> None:
    snapshot_root = ARTIFACTS / SNAPSHOT_NAME
    if snapshot_root.exists():
        shutil.rmtree(snapshot_root)
    snapshot_root.mkdir(parents=True)

    excluded_prefixes = (
        ".git/", ".ci_payload/", "artifacts/", "bin/", "bin-int/", "out/",
        "profile_results/", "results/committed/", "benchmarks/novelty01/frozen_asset_bundle/",
    )
    tracked = run(["git", "ls-files", "-z"]).stdout.split("\0")
    candidates = {item for item in tracked if item}
    candidates.update(WRITTEN)
    for relative in sorted(candidates):
        normalized = relative.replace("\\", "/")
        if normalized in DELETE_PATHS or any(
            normalized == prefix.rstrip("/") or normalized.startswith(prefix)
            for prefix in excluded_prefixes
        ):
            continue
        if any(normalized == item or normalized.startswith(item + "/") for item in DELETE_PATHS):
            continue
        source = ROOT / normalized
        if not source.is_file():
            continue
        destination = snapshot_root / normalized
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    notice = (
        "Repaired source snapshot generated from Novelty01 69.10.\n"
        "Excluded: .git, build outputs, experiment outputs, committed local-debug results, "
        "and frozen binary bundles.\n"
        "Windows MSBuild/FXC/D3D11 validation is still required.\n"
    )
    (snapshot_root / "SNAPSHOT_NOTICE.txt").write_text(notice, encoding="utf-8")
    snapshot_zip = ARTIFACTS / f"{SNAPSHOT_NAME}.zip"
    if snapshot_zip.exists():
        snapshot_zip.unlink()
    with zipfile.ZipFile(snapshot_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(snapshot_root.rglob("*")):
            if path.is_file():
                archive.write(path, f"{SNAPSHOT_NAME}/{path.relative_to(snapshot_root).as_posix()}")
    (ARTIFACTS / f"{SNAPSHOT_NAME}.sha256.txt").write_text(
        f"{sha256(snapshot_zip)}  {snapshot_zip.name}\n", encoding="utf-8"
    )


def main() -> int:
    if not (ROOT / ".git").exists():
        raise RuntimeError("Git checkout required")
    run(["git", "merge-base", "--is-ancestor", SOURCE_BASELINE, "HEAD"])
    if ARTIFACTS.exists():
        shutil.rmtree(ARTIFACTS)
    ARTIFACTS.mkdir(parents=True)

    promote_sources()
    patch_header()
    patch_cpp()
    patch_preflight()
    patch_readiness()
    patch_matrix()
    patch_freeze_tool()
    patch_asset_manifest()
    patch_frozen_parameters()
    patch_formal_contract()
    patch_docs()
    create_implementation_contract()
    create_completion_test()
    remove_obsolete()

    validation = static_validation()
    create_package(validation)
    create_source_snapshot()

    summary = {
        "sourceCommit": SOURCE_BASELINE,
        "modifiedOrAddedFiles": sorted(WRITTEN),
        "deletePaths": DELETE_PATHS,
        "validation": validation,
        "packageZip": str(ARTIFACTS / f"{PACKAGE_NAME}.zip"),
        "sourceSnapshotZip": str(ARTIFACTS / f"{SNAPSHOT_NAME}.zip"),
    }
    (ARTIFACTS / "BUILD_SUMMARY.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
