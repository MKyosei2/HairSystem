from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODIFIED: list[str] = []


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")
    if path not in MODIFIED:
        MODIFIED.append(path)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def append_once(text: str, marker: str, block: str) -> str:
    if marker in text:
        return text
    if not text.endswith("\n"):
        text += "\n"
    return text + "\n" + block.strip("\n") + "\n"


# 1. Remove the temporary ZIP-generation build hook while keeping a valid MSBuild file.
write(
    "Directory.Build.props",
    """<?xml version=\"1.0\" encoding=\"utf-8\"?>
<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">
</Project>
""",
)


# 2. Add a real hidden startup contract path: window/device/shaders + native ABI + clean destruction.
path = "apps/dx11_viewer/src/AppMain.cpp"
text = read(path)
startup_block = r'''    if (ArgumentEquals(1, L"--startup-self-test"))
    {
        SetEnvironmentVariableW(L"HAIRGPU_SHADER_SELF_TEST", L"1");
        SetEnvironmentVariableW(L"HAIRGPU_STARTUP_SELF_TEST", L"1");
        hairgpu::viewer::GpuHairViewer app;
        if (!app.Initialize(instance, SW_HIDE))
        {
            return 2;
        }
        return app.RunNativeSelfTests();
    }

'''
text = replace_once(
    text,
    '    if (ArgumentEquals(1, L"--shader-self-test"))\n',
    startup_block + '    if (ArgumentEquals(1, L"--shader-self-test"))\n',
    "AppMain startup self-test",
)
write(path, text)


# 3. Define the integrated Segment Group runtime record explicitly in the public data model.
path = "include/hairgpu/Types.h"
text = read(path)
record_block = r'''
    enum SegmentGroupWakeReasonFlags : uint32_t
    {
        SegmentGroupWake_None = 0u,
        SegmentGroupWake_ParentMoved = 1u << 0u,
        SegmentGroupWake_ColliderChanged = 1u << 1u,
        SegmentGroupWake_NeighborChanged = 1u << 2u,
        SegmentGroupWake_Edited = 1u << 3u,
        SegmentGroupWake_CertificateInvalid = 1u << 4u,
        SegmentGroupWake_BoundaryFailed = 1u << 5u,
        SegmentGroupWake_Watchdog = 1u << 6u,
        SegmentGroupWake_ForcedDynamic = 1u << 7u
    };

    enum SegmentGroupRuntimeRecordFlags : uint32_t
    {
        SegmentGroupRecord_None = 0u,
        SegmentGroupRecord_CacheValid = 1u << 0u,
        SegmentGroupRecord_SharedPointOwner = 1u << 1u,
        SegmentGroupRecord_ForceDynamic = 1u << 2u,
        SegmentGroupRecord_ForceStatic = 1u << 3u,
        SegmentGroupRecord_Reject = 1u << 4u
    };

    // CPU-side formal definition of Novelty 01's integrated data type.
    // The GPU representation carries the same semantic fields in
    // HairSegmentGroupRuntime.hlsl. Keeping this record 16-byte aligned makes it
    // safe to upload as a StructuredBuffer record in later implementation steps.
    struct alignas(16) SegmentGroupRuntimeRecord
    {
        uint32_t groupId = 0u;
        uint32_t strandIndex = 0u;
        uint32_t blockIndex = 0u;
        SegmentGroupRuntimeStateValue state = SegmentGroupRuntimeStateValue::Dynamic;

        uint32_t parentGroupId = 0xffffffffu;
        uint32_t childGroupId = 0xffffffffu;
        uint32_t sharedPointOwnerGroupId = 0xffffffffu;
        uint32_t wakeReasonMask = SegmentGroupWake_None;

        uint32_t parentVersion = 0u;
        uint32_t colliderVersion = 0u;
        uint32_t editVersion = 0u;
        uint32_t topologyVersion = 0u;

        float cachedDirectionX = 0.0f;
        float cachedDirectionY = 1.0f;
        float cachedDirectionZ = 0.0f;
        float restLength = 0.0f;

        float previousDirectionX = 0.0f;
        float previousDirectionY = 1.0f;
        float previousDirectionZ = 0.0f;
        float boundaryErrorMm = 0.0f;

        uint32_t lastSolvedFrame = 0u;
        uint32_t lastRepairFrame = 0u;
        uint32_t stableFrameCount = 0u;
        uint32_t flags = SegmentGroupRecord_None;
    };

    static_assert((sizeof(SegmentGroupRuntimeRecord) % 16u) == 0u,
                  "SegmentGroupRuntimeRecord must remain StructuredBuffer aligned.");
'''
needle = '''    enum class SegmentGroupRuntimeStateValue : uint32_t
    {
        StaticCached = 0u,
        StaticTransformed = 1u,
        Dynamic = 2u,
        Deferred = 3u,
        Repair = 4u
    };
'''
text = replace_once(text, needle, needle + record_block, "Types integrated record")
write(path, text)


# 4. Add deterministic priority and hysteresis to the existing runtime shader.
path = "apps/dx11_viewer/shaders/HairSegmentGroupRuntime.hlsl"
text = read(path)
flags = r'''
static const uint GROUP_FLAG_FORCE_DYNAMIC = 1u << 24u;
static const uint GROUP_FLAG_FORCE_STATIC = 1u << 25u;
static const uint GROUP_FLAG_REJECT = 1u << 26u;
static const uint GROUP_FLAG_SHARED_POINT_OWNER = 1u << 27u;
static const uint STATIC_ENTER_HOLD_FRAMES = 8u;
'''
text = replace_once(
    text,
    "static const uint DIRTY_WATCHDOG = 1u << 8u;\n",
    "static const uint DIRTY_WATCHDOG = 1u << 8u;\n" + flags,
    "HLSL priority flags",
)
old_transition = r'''    group.previousState = group.state;
    if (!finite || lengthError > gLengthErrorThreshold || group.boundaryErrorMm > gBoundaryErrorThresholdMm)
    {
        group.state = GROUP_STATE_REPAIR;
    }
    else if (certificateValid)
    {
        group.state = GROUP_STATE_STATIC_CACHED;
        group.stateStableFrameCount += 1u;
    }
    else
    {
        group.state = GROUP_STATE_DYNAMIC;
        group.stateStableFrameCount = 0u;
    }
'''
new_transition = r'''    group.previousState = group.state;
    const bool reject = (group.flags & GROUP_FLAG_REJECT) != 0u;
    const bool forceDynamic = (group.flags & GROUP_FLAG_FORCE_DYNAMIC) != 0u;
    const bool forceStatic = (group.flags & GROUP_FLAG_FORCE_STATIC) != 0u;
    const bool wasStatic =
        group.previousState == GROUP_STATE_STATIC_CACHED ||
        group.previousState == GROUP_STATE_STATIC_TRANSFORMED;

    // Priority: reject/invalid > repair > forced dynamic > forced static > normal.
    if (reject || !finite || lengthError > gLengthErrorThreshold ||
        group.boundaryErrorMm > gBoundaryErrorThresholdMm)
    {
        group.state = GROUP_STATE_REPAIR;
        group.stateStableFrameCount = 0u;
    }
    else if (forceDynamic)
    {
        group.state = GROUP_STATE_DYNAMIC;
        group.stateStableFrameCount = 0u;
    }
    else if (forceStatic && certificateValid)
    {
        group.state = GROUP_STATE_STATIC_CACHED;
        group.stateStableFrameCount = STATIC_ENTER_HOLD_FRAMES;
    }
    else if (certificateValid)
    {
        group.stateStableFrameCount = min(group.stateStableFrameCount + 1u, 0xfffffffeu);
        group.state = (wasStatic || group.stateStableFrameCount >= STATIC_ENTER_HOLD_FRAMES)
            ? GROUP_STATE_STATIC_CACHED
            : GROUP_STATE_DYNAMIC;
    }
    else
    {
        group.state = GROUP_STATE_DYNAMIC;
        group.stateStableFrameCount = 0u;
    }
'''
text = replace_once(text, old_transition, new_transition, "HLSL state transition")
write(path, text)


# 5. Strengthen the native self-test with the integrated record contract.
path = "apps/dx11_viewer/src/GpuHairViewer_parts/15_shutdown_and_tail.inl"
text = read(path)
old = r'''        if (!std::isfinite(SegmentLengthForParticleCount(kFixedParticlesPerStrand)) ||
            SegmentLengthForParticleCount(kFixedParticlesPerStrand) <= 0.0f) return 15;
        return 0;
'''
new = r'''        if (!std::isfinite(SegmentLengthForParticleCount(kFixedParticlesPerStrand)) ||
            SegmentLengthForParticleCount(kFixedParticlesPerStrand) <= 0.0f) return 15;
        if ((sizeof(SegmentGroupRuntimeRecord) % 16u) != 0u) return 16;
        if (static_cast<uint32_t>(SegmentGroupRuntimeStateValue::StaticCached) != 0u ||
            static_cast<uint32_t>(SegmentGroupRuntimeStateValue::Dynamic) != 2u ||
            static_cast<uint32_t>(SegmentGroupRuntimeStateValue::Repair) != 4u) return 17;
        return 0;
'''
text = replace_once(text, old, new, "native runtime record test")
write(path, text)


# 6. Make the batch runner actually implement all pre-novelty and phase-13 modes.
path = "tools/final_stage/Run_Final_Runtime_Suite.bat"
text = read(path)
text = re.sub(
    r"echo Usage: Run_Final_Runtime_Suite\.bat .*",
    "echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^> [prepare-asset^|prenovelty-smoke^|startup20^|prenovelty-soak30^|prenovelty-complete^|proposal-smoke^|comparison-smoke^|critical^|baseline^|full^|determinism^|soak30^|soak2h^|soak8h^|phase13-full^|all] [output_dir]",
    text,
    count=1,
)
old_supported = next(line for line in text.splitlines() if line.startswith('if /I not "%MODE%"=="prepare-asset"'))
new_supported = 'if /I not "%MODE%"=="prepare-asset" if /I not "%MODE%"=="prenovelty-smoke" if /I not "%MODE%"=="startup20" if /I not "%MODE%"=="prenovelty-soak30" if /I not "%MODE%"=="prenovelty-complete" if /I not "%MODE%"=="proposal-smoke" if /I not "%MODE%"=="comparison-smoke" if /I not "%MODE%"=="critical" if /I not "%MODE%"=="baseline" if /I not "%MODE%"=="full" if /I not "%MODE%"=="determinism" if /I not "%MODE%"=="soak30" if /I not "%MODE%"=="soak2h" if /I not "%MODE%"=="soak8h" if /I not "%MODE%"=="phase13-full" if /I not "%MODE%"=="all" ('
text = text.replace(old_supported, new_supported, 1)
text = replace_once(
    text,
    'set "FREEZER=%~dp0freeze_novelty01_asset.py"\n',
    'set "FREEZER=%~dp0freeze_novelty01_asset.py"\nset "PRENOVELTY_CONFIG=%ROOT%\\docs\\sample_assets\\prenovelty_runtime_suite.json"\nset "FINAL_CONFIG=%ROOT%\\docs\\sample_assets\\final_runtime_suite_template.json"\n\nif /I "%MODE%"=="prenovelty-complete" goto :prenovelty_complete\nif /I "%MODE%"=="phase13-full" goto :phase13_full\n',
    "runner config and phase routing",
)
text = replace_once(
    text,
    'set "RUN_KIND_ARGS=--run-kind formal"\n',
    'set "RUN_KIND_ARGS=--run-kind formal"\nif /I "%MODE%"=="prenovelty-smoke" set "FILTER_ARGS=--only-method B0 --only-method B6 --only-method B8 --only-scenario S01_Idle --only-scenario S03_Walk --only-scenario S09_StrongWind --only-scenario S17_CloseCamera100k --only-seed 1103"\nif /I "%MODE%"=="prenovelty-soak30" (\n  set "RUN_KIND_ARGS=--run-kind long-run"\n  set "FILTER_ARGS=--only-run-name L30_B0_Walk --only-run-name L30_B8_Walk"\n)\n',
    "runner prenovelty filters",
)
text = text.replace('"%VIEWER%" --shader-self-test\n  if errorlevel 1 exit /b !errorlevel!\n)\necho Startup20 completed without failure.', '"%VIEWER%" --startup-self-test\n  if errorlevel 1 exit /b !errorlevel!\n)\necho Startup20 completed without failure.', 1)
labels = r'''

:prenovelty_complete
if not exist "%PRENOVELTY_CONFIG%" (
  echo ERROR: Pre-novelty config was not found: "%PRENOVELTY_CONFIG%"
  exit /b 12
)
call "%~f0" "%PRENOVELTY_CONFIG%" prepare-asset
if errorlevel 1 exit /b !errorlevel!
call "%~f0" "%PRENOVELTY_CONFIG%" prenovelty-smoke "%ROOT%\out\prenovelty_smoke"
if errorlevel 1 exit /b !errorlevel!
call "%~f0" "%PRENOVELTY_CONFIG%" startup20
if errorlevel 1 exit /b !errorlevel!
call "%~f0" "%PRENOVELTY_CONFIG%" determinism "%ROOT%\out\prenovelty_determinism"
if errorlevel 1 exit /b !errorlevel!
call "%~f0" "%PRENOVELTY_CONFIG%" prenovelty-soak30 "%ROOT%\out\prenovelty_soak30"
if errorlevel 1 exit /b !errorlevel!
echo PRENOVELTY_ENVIRONMENT_READY
exit /b 0

:phase13_full
call "%~f0" "%PRENOVELTY_CONFIG%" prenovelty-complete
if errorlevel 1 exit /b !errorlevel!
for %%M in (proposal-smoke comparison-smoke critical determinism soak30 soak2h soak8h full) do (
  call "%~f0" "%FINAL_CONFIG%" %%M
  if errorlevel 1 exit /b !errorlevel!
)
echo PHASE13_FORMAL_EXPERIMENT_COMPLETE
exit /b 0
'''
text = append_once(text, ":phase13_full", labels)
write(path, text)


# 7. Produce a dedicated pre-novelty configuration without P-dependent runs.
final_config = json.loads(read("docs/sample_assets/final_runtime_suite_template.json"))
final_config["suite_name"] = "novelty01_prenovelty_runtime_suite"
final_config["suite_version"] = "4.0-prenovelty-ready"
final_config["experiment_name"] = "Novelty01_PreNovelty_Baseline"
final_config["outputs_dir"] = "../../out/prenovelty_baseline"
final_config["telemetry_dir"] = "../../out/prenovelty_baseline/telemetry"
final_config["preflight"]["required_method_codes"] = ["B0", "B6", "B8"]
final_config["methods"] = [item for item in final_config["methods"] if item.get("code") in {"B0", "B6", "B8"}]
final_config["run_defaults"].update({
    "evaluation_phase": "prenovelty",
    "minimum_average_fps": 24.0,
    "maximum_p95_frame_ms": 41.6667,
    "maximum_p99_frame_ms": 50.0,
    "maximum_single_frame_ms": 100.0,
})
final_config["goals"].update({
    "minimum_average_fps": 24.0,
    "frame_time_p999_ms_max": 50.0,
})
final_config["determinism_replays"] = [
    {"id": "DET_B0_WALK", "method": "B0", "scenario": "S03_Walk", "seed": 1103, "repetitions": 5},
    {"id": "DET_B6_IDLE", "method": "B6", "scenario": "S01_Idle", "seed": 2207, "repetitions": 5},
    {"id": "DET_B8_FALSE_STATIC", "method": "B8", "scenario": "S12_FalseStaticTrap", "seed": 3301, "repetitions": 5},
]
final_config["long_runs"] = [
    {"id": "L30_B0_Walk", "method": "B0", "scenario": "S03_Walk", "seed": 1103, "measurement_frames": 108000, "timeline_repeat": True, "timeout_seconds": 2400},
    {"id": "L30_B8_Walk", "method": "B8", "scenario": "S03_Walk", "seed": 2207, "measurement_frames": 108000, "timeline_repeat": True, "timeout_seconds": 2400},
]
write("docs/sample_assets/prenovelty_runtime_suite.json", json.dumps(final_config, ensure_ascii=False, indent=2) + "\n")


# 8. Ensure compatibility materialization never mutates tracked scenario JSON.
path = "tools/final_stage/hairgpu_runtime_suite.py"
text = read(path)
old_materialization = r'''        # Legacy configs store scenario assertions in the suite config rather than
        # in the timeline JSON itself. Mirror them into the timeline file so the
        # stricter current implementation sees identical assertions on both sides.
        for scenario in working.get('scenarios', []):
            if not isinstance(scenario, dict):
                continue
            timeline = resolve_path(Path(config_dir), scenario.get('timeline'))
            if timeline is None or not timeline.is_file():
                continue
            payload = read_json(timeline)
            payload['assertions'] = dict(scenario.get('assertions', {}))
            write_json(timeline, payload)
'''
new_materialization = r'''        # Legacy configs may store assertions in the suite config. Materialize a
        # temporary timeline copy instead of mutating tracked benchmark sources.
        materialized_scenarios = []
        materialized_root = Path(tempfile.mkdtemp(prefix='hairgpu_timeline_contract_'))
        for raw_scenario in working.get('scenarios', []):
            if not isinstance(raw_scenario, dict):
                continue
            scenario = dict(raw_scenario)
            timeline = resolve_path(Path(config_dir), scenario.get('timeline'))
            if timeline is not None and timeline.is_file():
                payload = read_json(timeline)
                payload['assertions'] = dict(scenario.get('assertions', {}))
                materialized = materialized_root / timeline.name
                write_json(materialized, payload)
                scenario['timeline'] = str(materialized)
            materialized_scenarios.append(scenario)
        working['scenarios'] = materialized_scenarios
'''
text = replace_once(text, old_materialization, new_materialization, "runtime suite non-mutating timeline")
phase13_runtime = r'''

# HAIRGPU_PHASE13_EXECUTION_CONTRACT_V1
PHASE13_EXECUTION_ORDER = (
    "prepare-asset",
    "prenovelty-smoke",
    "startup20",
    "determinism",
    "prenovelty-soak30",
    "proposal-smoke",
    "comparison-smoke",
    "critical",
    "soak30",
    "soak2h",
    "soak8h",
    "full",
)


def phase13_execution_order() -> tuple[str, ...]:
    return PHASE13_EXECUTION_ORDER
'''
text = append_once(text, "HAIRGPU_PHASE13_EXECUTION_CONTRACT_V1", phase13_runtime)
write(path, text)


# 9. Add a machine-readable completion check without weakening evidence gates.
path = "tools/final_stage/hairgpu_final_run.py"
text = read(path)
phase13_final = r'''

# HAIRGPU_PHASE13_COMPLETION_CONTRACT_V1
PHASE13_REQUIRED_EVIDENCE_FILES = (
    "suite_summary.json",
    "suite_metrics.json",
    "final_runtime_suite.sqlite",
    "final_runtime_suite_artifacts.zip",
    "formal_host_preflight.json",
    "formal_environment_gate.json",
    "formal_environment_gate.md",
    "FORMAL_ENVIRONMENT_COMPLETE.txt",
)


def phase13_completion_status(output_dir: Path) -> dict[str, Any]:
    root = Path(output_dir)
    missing = [name for name in PHASE13_REQUIRED_EVIDENCE_FILES if not (root / name).is_file()]
    return {
        "complete": not missing,
        "missing": missing,
        "outputDir": str(root.resolve()),
    }
'''
text = append_once(text, "HAIRGPU_PHASE13_COMPLETION_CONTRACT_V1", phase13_final)
write(path, text)


# 10. Add regression tests for the new phase contract and non-mutating baseline gates.
path = "tests/final_stage/test_hairgpu_safe_runtime_suite.py"
text = read(path)
phase13_tests = r'''

class Phase13ExecutionContractTests(unittest.TestCase):
    def test_phase13_order_starts_with_prenovelty_and_ends_with_full(self):
        order = suite.phase13_execution_order()
        self.assertEqual(order[0], "prepare-asset")
        self.assertIn("prenovelty-smoke", order)
        self.assertIn("proposal-smoke", order)
        self.assertEqual(order[-1], "full")

    def test_phase13_completion_requires_all_evidence_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            status = final_run.phase13_completion_status(root)
            self.assertFalse(status["complete"])
            for name in final_run.PHASE13_REQUIRED_EVIDENCE_FILES:
                (root / name).write_bytes(b"ok")
            status = final_run.phase13_completion_status(root)
            self.assertTrue(status["complete"])
            self.assertEqual(status["missing"], [])

    def test_prenovelty_and_proposal_gates_remain_separate(self):
        baseline = final_run._normalize_case({"method": "B0", "scenario": "S01_Idle"})
        proposal = final_run._normalize_case({"method": "P", "scenario": "S01_Idle"})
        self.assertEqual(baseline["minimum_average_fps"], 24.0)
        self.assertEqual(proposal["minimum_average_fps"], 60.0)
'''
text = append_once(text, "class Phase13ExecutionContractTests", phase13_tests)
write(path, text)


# 11. Extend shared Windows CI with hidden startup validation and source-mutation detection.
path = ".github/workflows/windows-build-test.yml"
text = read(path)
startup_step = r'''
      - name: Startup self-test
        shell: cmd
        run: bin\Release\HairSystem\HairSystem.exe --startup-self-test
'''
text = replace_once(
    text,
    '      - name: Upload build logs\n',
    startup_step + '\n      - name: Verify tests did not mutate source contracts\n        shell: cmd\n        run: git diff --exit-code -- apps include src tools tests docs .github Directory.Build.props\n\n      - name: Upload build logs\n',
    "Windows CI startup and mutation checks",
)
write(path, text)


# 12. Let the dedicated GPU workflow select pre-novelty and phase-13 modes.
path = ".github/workflows/formal-gpu-regression.yml"
text = read(path)
text = replace_once(
    text,
    '        options:\n          - proposal-smoke\n',
    '        options:\n          - prenovelty-smoke\n          - startup20\n          - prenovelty-complete\n          - phase13-full\n          - proposal-smoke\n',
    "formal workflow phase options",
)
select_step = r'''
      - name: Select suite configuration
        shell: cmd
        run: |
          set "CONFIG=docs\sample_assets\final_runtime_suite_template.json"
          if /I "${{ inputs.mode }}"=="prenovelty-smoke" set "CONFIG=docs\sample_assets\prenovelty_runtime_suite.json"
          if /I "${{ inputs.mode }}"=="startup20" set "CONFIG=docs\sample_assets\prenovelty_runtime_suite.json"
          if /I "${{ inputs.mode }}"=="prenovelty-complete" set "CONFIG=docs\sample_assets\prenovelty_runtime_suite.json"
          if /I "${{ inputs.mode }}"=="phase13-full" set "CONFIG=docs\sample_assets\prenovelty_runtime_suite.json"
          echo HAIRGPU_SUITE_CONFIG=%CONFIG%>>%GITHUB_ENV%
'''
text = replace_once(
    text,
    '      - name: Prepare canonical frozen asset\n',
    select_step + '\n      - name: Prepare canonical frozen asset\n',
    "formal workflow select config",
)
text = text.replace('docs\\sample_assets\\final_runtime_suite_template.json\n           prepare-asset', '%HAIRGPU_SUITE_CONFIG%\n           prepare-asset', 1)
text = text.replace('docs\\sample_assets\\final_runtime_suite_template.json\n           ${{ inputs.mode }}', '%HAIRGPU_SUITE_CONFIG%\n           ${{ inputs.mode }}', 1)
write(path, text)


# Record the exact package content for the workflow. This manifest itself is not
# included in the user ZIP.
manifest = ROOT / "phase13_modified_files.txt"
manifest.write_text("\n".join(MODIFIED) + "\n", encoding="utf-8", newline="\n")
print(json.dumps({"modified": MODIFIED, "count": len(MODIFIED)}, ensure_ascii=False, indent=2))
