from __future__ import annotations

import copy
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "out" / "phase13_source_package"
PACKAGE_ROOT = OUT / "HairSystem_Phase13_Modified_Source_Files"
ZIP_PATH = OUT / "HairSystem_Phase13_Modified_Source_Files.zip"


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write_text(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise RuntimeError(f"Patch marker not found for {label}")
    return text.replace(old, new, 1)


def patch_directory_build_props() -> None:
    write_text(
        "Directory.Build.props",
        """<?xml version=\"1.0\" encoding=\"utf-8\"?>
<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">
  <PropertyGroup>
    <HairGpuStep0ProfileEnabled Condition=\"'$(HairGpuStep0ProfileEnabled)' == ''\">false</HairGpuStep0ProfileEnabled>
    <HairGpuFormalBuild Condition=\"'$(HairGpuFormalBuild)' == ''\">false</HairGpuFormalBuild>
    <HairGpuFormalSelfTest Condition=\"'$(HairGpuFormalSelfTest)' == ''\">false</HairGpuFormalSelfTest>
  </PropertyGroup>
</Project>
""",
    )


def patch_app_main() -> None:
    path = "apps/dx11_viewer/src/AppMain.cpp"
    text = read_text(path)
    if 'L"--startup-self-test"' not in text:
        marker = """    if (ArgumentEquals(1, L\"--native-self-test\"))
    {
        hairgpu::viewer::GpuHairViewer app;
        return app.RunNativeSelfTests();
    }
"""
        addition = """    if (ArgumentEquals(1, L\"--startup-self-test\"))
    {
        // A deterministic hidden startup test: window, D3D11 device, resources,
        // UI-safe initialization and all runtime shaders must initialize.
        SetEnvironmentVariableW(L\"HAIRGPU_SHADER_SELF_TEST\", L\"1\");
        hairgpu::viewer::GpuHairViewer app;
        if (!app.Initialize(instance, SW_HIDE))
        {
            return 20;
        }
        const int nativeResult = app.RunNativeSelfTests();
        return nativeResult == 0 ? 0 : 21 + nativeResult;
    }

""" + marker
        text = replace_once(text, marker, addition, "AppMain startup self-test")
    write_text(path, text)


def patch_run_batch() -> None:
    path = "tools/final_stage/Run_Final_Runtime_Suite.bat"
    text = read_text(path)
    text = text.replace(
        "prepare-asset^|prenovelty-smoke^|startup20^|prenovelty-complete^|proposal-smoke",
        "prepare-asset^|prenovelty-smoke^|prenovelty-soak30^|startup20^|prenovelty-complete^|proposal-smoke",
    )
    support_old = 'if /I not "%MODE%"=="prepare-asset" if /I not "%MODE%"=="prenovelty-smoke" if /I not "%MODE%"=="startup20"'
    support_new = 'if /I not "%MODE%"=="prepare-asset" if /I not "%MODE%"=="prenovelty-smoke" if /I not "%MODE%"=="prenovelty-soak30" if /I not "%MODE%"=="startup20"'
    text = text.replace(support_old, support_new)

    if 'if /I "%MODE%"=="prenovelty-complete" goto :prenovelty_complete' not in text:
        marker = """if not exist \"%CONFIG%\" (
  echo ERROR: suite config was not found: \"%CONFIG%\"
  exit /b 2
)
"""
        text = replace_once(
            text,
            marker,
            marker + '\nif /I "%MODE%"=="prenovelty-complete" goto :prenovelty_complete\n',
            "prenovelty complete dispatch",
        )

    if 'if /I "%MODE%"=="prenovelty-smoke" (' not in text:
        marker = 'if /I "%MODE%"=="proposal-smoke" set "FILTER_ARGS=--only-method P --only-scenario S01_Idle --only-seed 1103"\n'
        addition = """if /I \"%MODE%\"==\"prenovelty-smoke\" (
  set \"FILTER_ARGS=--only-method B0 --only-method B6 --only-method B8 --only-scenario S01_Idle --only-scenario S03_Walk --only-scenario S09_StrongWind --only-scenario S17_CloseCamera100k --only-seed 1103\"
)
if /I \"%MODE%\"==\"prenovelty-soak30\" (
  set \"RUN_KIND_ARGS=--run-kind long-run\"
  set \"FILTER_ARGS=--only-run-name L30_B0_Walk --only-run-name L30_B8_Walk\"
)
""" + marker
        text = replace_once(text, marker, addition, "prenovelty filters")

    text = text.replace('"%VIEWER%" --shader-self-test\n  if errorlevel 1 exit /b !errorlevel!',
                        '"%VIEWER%" --startup-self-test\n  if errorlevel 1 exit /b !errorlevel!')

    if ":prenovelty_complete" not in text:
        text += """

:prenovelty_complete
if not defined HAIRGPU_FREEZE_OPERATOR (
  echo ERROR: HAIRGPU_FREEZE_OPERATOR is required for prenovelty-complete.
  echo Example: set HAIRGPU_FREEZE_OPERATOR=Your_Name
  exit /b 10
)
call "%~f0" "%CONFIG%" prepare-asset
if errorlevel 1 exit /b !errorlevel!
call "%~f0" "%CONFIG%" prenovelty-smoke "%ROOT%\out\prenovelty_smoke"
if errorlevel 1 exit /b !errorlevel!
call "%~f0" "%CONFIG%" startup20
if errorlevel 1 exit /b !errorlevel!
call "%~f0" "%CONFIG%" determinism "%ROOT%\out\prenovelty_determinism"
if errorlevel 1 exit /b !errorlevel!
call "%~f0" "%CONFIG%" prenovelty-soak30 "%ROOT%\out\prenovelty_soak30"
if errorlevel 1 exit /b !errorlevel!
echo PRENOVELTY_ENVIRONMENT_READY
exit /b 0
"""
    write_text(path, text)


def create_prenovelty_config() -> None:
    source_path = ROOT / "docs/sample_assets/final_runtime_suite_template.json"
    config = json.loads(source_path.read_text(encoding="utf-8-sig"))
    config["suite_name"] = "novelty01_prenovelty_runtime_suite"
    config["suite_version"] = "4.0-prenovelty-ready"
    config["experiment_name"] = "Novelty01_PreNovelty_Baseline"
    config["outputs_dir"] = "../../out/prenovelty_baseline"
    config["telemetry_dir"] = "../../out/prenovelty_baseline/telemetry"
    config["baseline_method"] = "B8"
    config["performance_baseline_method"] = "B6"
    config["quality_baseline_method"] = "B0"
    allowed = ["B0", "B6", "B8"]
    config["preflight"]["required_method_codes"] = allowed
    config["methods"] = [item for item in config.get("methods", []) if item.get("code") in allowed]
    config["run_defaults"]["evaluation_phase"] = "prenovelty"
    config["run_defaults"]["minimum_average_fps"] = 24.0
    config["run_defaults"]["minimum_sixty_fps_ratio"] = 0.0
    config["run_defaults"]["maximum_frame_budget_overrun_rate"] = 0.05
    config["run_defaults"]["maximum_p95_frame_ms"] = 41.6667
    config["run_defaults"]["maximum_p99_frame_ms"] = 50.0
    config["run_defaults"]["maximum_single_frame_ms"] = 100.0
    config["determinism_replays"] = [
        {"id": "DET_B0_WALK", "method": "B0", "scenario": "S03_Walk", "seed": 1103, "repetitions": 5},
        {"id": "DET_B6_IDLE", "method": "B6", "scenario": "S01_Idle", "seed": 2207, "repetitions": 5},
        {"id": "DET_B8_FALSE_STATIC", "method": "B8", "scenario": "S12_FalseStaticTrap", "seed": 3301, "repetitions": 5},
    ]
    config["long_runs"] = [
        {
            "id": "L30_B0_Walk", "method": "B0", "scenario": "S03_Walk", "seed": 1103,
            "measurement_frames": 108000, "timeline_repeat": True, "timeout_seconds": 2400,
        },
        {
            "id": "L30_B8_Walk", "method": "B8", "scenario": "S03_Walk", "seed": 2207,
            "measurement_frames": 108000, "timeline_repeat": True, "timeout_seconds": 2400,
        },
    ]
    config["goals"]["minimum_average_fps"] = 24.0
    config["goals"]["minimum_sixty_fps_ratio"] = 0.0
    write_text("docs/sample_assets/prenovelty_runtime_suite.json", json.dumps(config, ensure_ascii=False, indent=2) + "\n")


def patch_types_contract() -> None:
    path = "include/hairgpu/Types.h"
    text = read_text(path)
    if "StrandSegmentGroupIntegratedRecord" in text:
        write_text(path, text)
        return
    marker = """    struct StrandMaterial
    {
"""
    addition = """    enum class SegmentGroupWakeReason : uint32_t
    {
        None = 0u,
        ParentMoved = 1u << 0u,
        ColliderChanged = 1u << 1u,
        NeighborChanged = 1u << 2u,
        Edited = 1u << 3u,
        CertificateExpired = 1u << 4u,
        BoundaryFailed = 1u << 5u,
        Watchdog = 1u << 6u
    };

    enum class SegmentGroupRepairReason : uint32_t
    {
        None = 0u,
        NonFiniteState = 1u << 0u,
        LengthViolation = 1u << 1u,
        BoundaryViolation = 1u << 2u,
        SharedPointConflict = 1u << 3u,
        CompactionOverflow = 1u << 4u
    };

    struct alignas(16) SegmentGroupVersionStamp
    {
        uint32_t parentVersion = 0u;
        uint32_t colliderVersion = 0u;
        uint32_t editVersion = 0u;
        uint32_t topologyVersion = 0u;
    };

    struct alignas(16) StrandSegmentGroupIntegratedRecord
    {
        uint32_t groupId = 0u;
        uint32_t strandIndex = 0u;
        uint32_t parentGroupId = UINT32_MAX;
        uint32_t childGroupId = UINT32_MAX;

        uint32_t startPointIndex = 0u;
        uint32_t endPointIndex = 0u;
        uint32_t ownerGroupId = 0u;
        uint32_t blockIndex = 0u;

        SegmentGroupRuntimeStateValue state = SegmentGroupRuntimeStateValue::Dynamic;
        SegmentGroupRuntimeStateValue previousState = SegmentGroupRuntimeStateValue::Dynamic;
        uint32_t wakeReasonMask = 0u;
        uint32_t repairReasonMask = 0u;

        SegmentGroupVersionStamp versions = {};

        Vec3 cachedDirection = { 0.0f, 1.0f, 0.0f };
        float restLength = 0.0f;
        Vec3 previousDirection = { 0.0f, 1.0f, 0.0f };
        float boundaryErrorMm = 0.0f;

        Vec3 previousVelocity = { 0.0f, 0.0f, 0.0f };
        float certificateErrorBoundMm = 0.0f;

        uint32_t certificateSourceFrame = 0u;
        uint32_t lastSolvedFrame = 0u;
        uint32_t lastRepairFrame = 0u;
        uint32_t stableFrameCount = 0u;

        uint32_t cacheValid = 0u;
        uint32_t certificateValid = 0u;
        uint32_t compactClass = 0u;
        uint32_t flags = 0u;
    };

    static_assert(sizeof(SegmentGroupVersionStamp) % 16u == 0u,
                  "SegmentGroupVersionStamp must stay 16-byte aligned.");
    static_assert(sizeof(StrandSegmentGroupIntegratedRecord) % 16u == 0u,
                  "StrandSegmentGroupIntegratedRecord must stay 16-byte aligned.");

""" + marker
    text = replace_once(text, marker, addition, "public integrated segment group record")
    write_text(path, text)


def patch_flags_contract() -> None:
    header = "include/hairgpu/HairNoveltyFlags.h"
    text = read_text(header)
    if "ValidateNovelty01FeatureContract" not in text:
        marker = "    uint32_t BuildTopLevelNoveltyMask(const HairNoveltyFeatureFlags& flags);\n"
        text = replace_once(
            text,
            marker,
            "    bool ValidateNovelty01FeatureContract(const HairNoveltyFeatureFlags& flags, std::string* errorOut = nullptr);\n" + marker,
            "flags validation declaration",
        )
    write_text(header, text)

    cpp = "src/hairgpu_core/HairNoveltyFlags.cpp"
    text = read_text(cpp)
    if "ValidateNovelty01FeatureContract" not in text:
        marker = "    uint32_t BuildTopLevelNoveltyMask(const HairNoveltyFeatureFlags& f)\n"
        function = """    bool ValidateNovelty01FeatureContract(
        const HairNoveltyFeatureFlags& inputFlags,
        std::string* errorOut)
    {
        const HairNoveltyFeatureFlags f = NormalizeMicrotypeAblationFlags(inputFlags);
        auto fail = [errorOut](const char* message)
        {
            if (errorOut) *errorOut = message;
            return false;
        };
        if (f.enableNoNoveltyBaseline &&
            (f.enableMicrotype || f.enableBudgetedPhysics || f.enableCertificateRender ||
             f.enablePerStrandEdit || f.enableGpuResident))
        {
            return fail("No-novelty baseline cannot enable proposal features.");
        }
        if (f.enableMicrotypeBoundaryRepair && !f.enableMicrotypeCertificate)
            return fail("Boundary repair requires the static certificate.");
        if (f.enableMicrotypeWatchdog && !f.enableMicrotypeCertificate)
            return fail("Watchdog requires the static certificate.");
        if (f.enableMicrotypeIndirectDispatch && !f.enableMicrotype)
            return fail("Indirect dispatch requires the integrated Segment Group runtime.");
        if (errorOut) errorOut->clear();
        return true;
    }

""" + marker
        text = replace_once(text, marker, function, "flags validation implementation")
    write_text(cpp, text)


def write_workflows() -> None:
    write_text(
        ".github/workflows/windows-build-test.yml",
        """name: Windows Build and Test

on:
  push:
    branches: [Novelty]
  pull_request:
    branches: [Novelty]
  workflow_dispatch:

permissions:
  contents: read

jobs:
  build-and-static-tests:
    runs-on: windows-2022
    timeout-minutes: 45
    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          clean: true

      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Locate MSBuild
        uses: microsoft/setup-msbuild@v2

      - name: Build Release x64
        shell: cmd
        run: >-
          msbuild HairSystem.sln /m /t:Rebuild
          /p:Configuration=Release /p:Platform=x64
          /p:HairGpuStep0ProfileEnabled=false
          /p:HairGpuFormalBuild=true
          /p:HairGpuFormalSelfTest=false

      - name: Python syntax
        shell: cmd
        run: >-
          python -m py_compile
          tools\final_stage\freeze_novelty01_asset.py
          tools\final_stage\hairgpu_final_run.py
          tools\final_stage\hairgpu_runtime_suite.py
          tools\final_stage\hairgpu_safe_runtime_suite.py
          tools\final_stage\hairgpu_formal_gate.py
          tools\final_stage\hairgpu_final_db.py
          tools\final_stage\hairgpu_final_excel.py

      - name: Python self-tests
        shell: cmd
        run: |
          python tools\final_stage\hairgpu_final_run.py --self-test
          python tools\final_stage\hairgpu_runtime_suite.py --self-test
          python tools\final_stage\hairgpu_formal_gate.py --self-test

      - name: Unit tests
        shell: cmd
        run: python -m unittest discover -s tests\final_stage -p "test_*.py"

      - name: Native ABI self-test
        shell: cmd
        run: bin\Release\HairSystem\HairSystem.exe --native-self-test

      - name: Shader self-test
        shell: cmd
        run: bin\Release\HairSystem\HairSystem.exe --shader-self-test

      - name: Hidden startup self-test
        shell: cmd
        run: bin\Release\HairSystem\HairSystem.exe --startup-self-test

      - name: Verify tests did not modify tracked source
        shell: powershell
        run: |
          $dirty = git status --porcelain --untracked-files=no
          if ($dirty) {
            $dirty
            throw "Build/tests modified tracked source files."
          }

      - name: Upload build logs
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: windows-build-test-${{ github.sha }}
          if-no-files-found: ignore
          path: |
            bin\Release\HairSystem\*.log
            bin-int\Release\HairSystem\*.log
""",
    )

    write_text(
        ".github/workflows/formal-gpu-regression.yml",
        """name: Formal GPU Regression

on:
  workflow_dispatch:
    inputs:
      mode:
        description: Formal suite mode
        required: true
        default: prenovelty-smoke
        type: choice
        options:
          - prenovelty-smoke
          - startup20
          - prenovelty-soak30
          - prenovelty-complete
          - proposal-smoke
          - comparison-smoke
          - critical
          - determinism
          - soak30
          - soak2h
          - soak8h
          - full

permissions:
  contents: read

concurrency:
  group: formal-gpu-regression
  cancel-in-progress: false

jobs:
  formal-gtx1660:
    runs-on: [self-hosted, Windows, X64, HairSystem, GTX1660]
    timeout-minutes: 600
    env:
      PYTHONUTF8: "1"
      PYTHONHASHSEED: "0"
      HAIRGPU_FREEZE_OPERATOR: GitHub_Actions
      HAIRGPU_ALLOW_REVIEW_BRANCH: "1"
    steps:
      - name: Checkout clean source state
        uses: actions/checkout@v4
        with:
          fetch-depth: 0
          clean: true

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: "3.11"

      - name: Select suite contract
        shell: powershell
        run: |
          $mode = "${{ inputs.mode }}"
          if ($mode.StartsWith("prenovelty") -or $mode -eq "startup20") {
            "HAIRGPU_SUITE_CONFIG=docs\sample_assets\prenovelty_runtime_suite.json" | Out-File -FilePath $env:GITHUB_ENV -Append
          } else {
            "HAIRGPU_SUITE_CONFIG=docs\sample_assets\final_runtime_suite_template.json" | Out-File -FilePath $env:GITHUB_ENV -Append
          }

      - name: Prepare canonical frozen asset
        shell: cmd
        run: >-
          tools\final_stage\Run_Final_Runtime_Suite.bat
          %HAIRGPU_SUITE_CONFIG%
          prepare-asset

      - name: Run selected formal gate
        shell: cmd
        run: >-
          tools\final_stage\Run_Final_Runtime_Suite.bat
          %HAIRGPU_SUITE_CONFIG%
          ${{ inputs.mode }}

      - name: Upload formal evidence
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: formal-gpu-${{ inputs.mode }}-${{ github.run_id }}
          if-no-files-found: warn
          retention-days: 30
          compression-level: 6
          path: |
            out\prenovelty_**\**
            out\novelty01_baseline\**
            benchmarks\novelty01\frozen_asset_bundle\frozen_asset_manifest.json
""",
    )


def write_contract_test() -> None:
    write_text(
        "tests/final_stage/test_phase13_package_contract.py",
        """from __future__ import annotations

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Phase13PackageContractTests(unittest.TestCase):
    def test_prenovelty_contract_has_no_proposal_method(self) -> None:
        data = json.loads((ROOT / "docs/sample_assets/prenovelty_runtime_suite.json").read_text(encoding="utf-8"))
        methods = [item["code"] for item in data["methods"]]
        self.assertEqual(methods, ["B0", "B6", "B8"])
        self.assertNotIn("P", data["preflight"]["required_method_codes"])
        self.assertEqual(data["run_defaults"]["minimum_average_fps"], 24.0)

    def test_final_contract_keeps_proposal_and_60_fps_gate(self) -> None:
        data = json.loads((ROOT / "docs/sample_assets/final_runtime_suite_template.json").read_text(encoding="utf-8"))
        self.assertIn("P", [item["code"] for item in data["methods"]])
        self.assertEqual(data["quality_gates"]["proposal"]["minimum_average_fps"], 60.0)

    def test_batch_exposes_all_transition_modes(self) -> None:
        text = (ROOT / "tools/final_stage/Run_Final_Runtime_Suite.bat").read_text(encoding="utf-8")
        for token in ("prenovelty-smoke", "prenovelty-soak30", "startup20", "prenovelty-complete", "proposal-smoke", "comparison-smoke", "critical", "determinism", "soak8h", "full"):
            self.assertIn(token, text)
        self.assertIn("--startup-self-test", text)

    def test_temporary_zip_generator_is_not_in_build_props(self) -> None:
        text = (ROOT / "Directory.Build.props").read_text(encoding="utf-8")
        self.assertNotIn("ChatGptGenerate", text)
        self.assertNotIn("chatgpt_invoke_generator", text)


if __name__ == "__main__":
    unittest.main()
""",
    )


def write_application_guide() -> None:
    delete_files = [
        ".chatgpt_artifact_run_id",
        ".github/workflows/chatgpt-build-prenovelty-14files.yml",
        ".github/workflows/chatgpt-publish-zip-run-id.yml",
        ".github/workflows/chatgpt-trigger-prenovelty-zip.yml",
        "tests/final_stage/test_zz_chatgpt_zip_artifact.py",
        "tools/final_stage/chatgpt_frozen_fallbacks.py",
        "tools/final_stage/chatgpt_invoke_generator.py",
    ]
    write_text("PHASE13_DELETE_THESE_FILES.txt", "\n".join(delete_files) + "\n")
    write_text(
        "PHASE13_APPLY_AND_RUN.md",
        """# HairSystem Phase 13 package

Base: `MKyosei2/HairSystem`, branch `Novelty`.

## Apply

1. Back up the repository and switch to a clean `Novelty` worktree.
2. Copy this package over the repository root, preserving directories.
3. Delete every path listed in `PHASE13_DELETE_THESE_FILES.txt`.
4. Commit the stabilization changes before generating the frozen asset.

## Pre-novelty readiness

```bat
set HAIRGPU_FREEZE_OPERATOR=Your_Name
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\prenovelty_runtime_suite.json prenovelty-complete
```

Do not start Proposal (`P`) implementation until the static CI, frozen asset, smoke, startup20, determinism and 30-minute baseline soak have passed.

## Proposal implementation / Phase 13 experiment order

```bat
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json proposal-smoke
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json comparison-smoke
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json critical
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json determinism
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json soak30
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json soak2h
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json soak8h
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json full
```

A result is thesis evidence only when its formal environment marker and all hashes validate on the intended GTX 1660-class machine.
""",
    )


def package_files() -> None:
    files = [
        "Directory.Build.props",
        ".github/workflows/windows-build-test.yml",
        ".github/workflows/formal-gpu-regression.yml",
        "apps/dx11_viewer/CMakeLists.txt",
        "apps/dx11_viewer/src/AppMain.cpp",
        "apps/dx11_viewer/src/GpuHairViewer.cpp",
        "apps/dx11_viewer/src/GpuHairViewer.h",
        "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/02a_required_runtime_foundation.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/04_constant_buffers_and_active_set_sync.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/07_draw_path_and_gpu_timing.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/15_shutdown_and_tail.inl",
        "apps/dx11_viewer/shaders/HairSegmentGroupRuntime.hlsl",
        "apps/dx11_viewer/shaders/HairIndirectArgs.hlsl",
        "apps/dx11_viewer/shaders/HairSim.hlsl",
        "include/hairgpu/Types.h",
        "include/hairgpu/HairNoveltyFlags.h",
        "include/hairgpu/HairNoveltyTelemetry.h",
        "src/hairgpu_core/HairNoveltyFlags.cpp",
        "src/hairgpu_core/HairNoveltyTelemetry.cpp",
        "tools/final_stage/Run_Final_Runtime_Suite.bat",
        "tools/final_stage/freeze_novelty01_asset.py",
        "tools/final_stage/hairgpu_final_run.py",
        "tools/final_stage/hairgpu_runtime_suite.py",
        "tools/final_stage/hairgpu_formal_gate.py",
        "tests/final_stage/test_hairgpu_safe_runtime_suite.py",
        "tests/final_stage/test_hairgpu_formal_gate.py",
        "tests/final_stage/test_phase13_package_contract.py",
        "docs/sample_assets/prenovelty_runtime_suite.json",
        "docs/sample_assets/final_runtime_suite_template.json",
        "benchmarks/novelty01/hardware_profiles.json",
        "PHASE13_DELETE_THESE_FILES.txt",
        "PHASE13_APPLY_AND_RUN.md",
    ]
    if PACKAGE_ROOT.exists():
        shutil.rmtree(PACKAGE_ROOT)
    PACKAGE_ROOT.mkdir(parents=True, exist_ok=True)
    copied = []
    for rel in files:
        source = ROOT / rel
        if not source.is_file():
            raise FileNotFoundError(rel)
        target = PACKAGE_ROOT / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        copied.append(rel)

    hashes = []
    for rel in copied:
        data = (PACKAGE_ROOT / rel).read_bytes()
        hashes.append(f"{hashlib.sha256(data).hexdigest()}  {rel}")
    (PACKAGE_ROOT / "SHA256SUMS.txt").write_text("\n".join(hashes) + "\n", encoding="utf-8")

    manifest = {
        "baseRepository": "MKyosei2/HairSystem",
        "baseBranch": "Novelty",
        "scope": "Phase 0 through Phase 13 source/configuration package",
        "fileCountExcludingManifest": len(copied),
        "files": copied,
        "deletions": (PACKAGE_ROOT / "PHASE13_DELETE_THESE_FILES.txt").read_text(encoding="utf-8").splitlines(),
        "validation": {
            "pythonContractTest": "tests/final_stage/test_phase13_package_contract.py",
            "windowsBuildRequiredAfterApply": True,
            "formalGpuEvidenceRequired": True,
        },
    }
    (PACKAGE_ROOT / "PACKAGE_MANIFEST.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    if ZIP_PATH.exists():
        ZIP_PATH.unlink()
    with zipfile.ZipFile(ZIP_PATH, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for item in sorted(PACKAGE_ROOT.rglob("*")):
            if item.is_file():
                archive.write(item, item.relative_to(PACKAGE_ROOT))
    print(ZIP_PATH)


def main() -> int:
    patch_directory_build_props()
    patch_app_main()
    patch_run_batch()
    create_prenovelty_config()
    patch_types_contract()
    patch_flags_contract()
    write_workflows()
    write_contract_test()
    write_application_guide()
    package_files()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
