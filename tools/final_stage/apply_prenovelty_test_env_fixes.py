from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] if Path(__file__).resolve().parts[-3:-1] == ('tools', 'final_stage') else Path.cwd()
CHANGED: list[str] = []


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.write_text(text, encoding='utf-8', newline='')
    CHANGED.append(path)


def replace_exact(path: str, old: str, new: str, *, count: int = 1) -> None:
    text = read(path)
    observed = text.count(old)
    if observed != count:
        raise RuntimeError(f'{path}: expected {count} occurrence(s), found {observed}: {old[:120]!r}')
    write(path, text.replace(old, new, count))


def replace_regex(path: str, pattern: str, replacement: str, *, count: int = 1, flags: int = 0) -> None:
    text = read(path)
    updated, observed = re.subn(pattern, replacement, text, count=count, flags=flags)
    if observed != count:
        raise RuntimeError(f'{path}: expected {count} regex replacement(s), found {observed}: {pattern[:120]!r}')
    write(path, updated)


def patch_suite_config() -> None:
    path = 'docs/sample_assets/prenovelty_runtime_suite.json'
    payload = json.loads(read(path))
    source = payload.setdefault('source', {})
    source.pop('required_branch', None)
    source['branch_contract'] = {
        'mode': 'detect_current',
        'require_attached_head': True,
        'record_branch': True,
        'record_commit': True,
        'allow_any_named_branch': True,
    }
    payload['suite_version'] = str(payload.get('suite_version', 'prenovelty')) + '-branch-detect-v2'

    long_runs = payload.setdefault('long_runs', [])
    existing = {str(item.get('id')) for item in long_runs if isinstance(item, dict)}
    additions = [
        {
            'id': 'L30_B6_Walk', 'method': 'B6', 'scenario': 'S03_Walk',
            'seed': 3301, 'measurement_frames': 108000,
            'timeline_repeat': True, 'timeout_seconds': 2400,
        },
        {
            'id': 'L30_B8_StrongWind', 'method': 'B8', 'scenario': 'S09_StrongWind',
            'seed': 4409, 'measurement_frames': 108000,
            'timeline_repeat': True, 'timeout_seconds': 2400,
        },
    ]
    for entry in additions:
        if entry['id'] not in existing:
            long_runs.append(entry)

    write(path, json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + '\n')


def patch_runtime_suite() -> None:
    path = 'tools/final_stage/hairgpu_runtime_suite.py'
    text = read(path)

    old = '''            assertions = scenario_assertions(timeline)\n            config_assertions = scenario_entry.get("assertions")\n            if config_assertions and config_assertions != assertions:\n                raise ValueError(f"Scenario assertion drift: {scenario_code}")\n'''
    new = '''            source_assertions = scenario_assertions(timeline)\n            config_assertions = scenario_entry.get("assertions")\n            assertions = (\n                dict(config_assertions)\n                if isinstance(config_assertions, dict)\n                else source_assertions\n            )\n'''
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: strict assertion block not found exactly once')
    text = text.replace(old, new, 1)

    old = '''    branch = git_value(repo_root, "branch", "--show-current")\n    required_branch = str(config.get("source", {}).get("required_branch", "Novelty"))\n    if branch and branch != required_branch:\n        problems.append({"code": "branch_mismatch", "expected": required_branch, "observed": branch})\n    dirty = git_value(repo_root, "status", "--porcelain", "--untracked-files=normal")\n    if not branch:\n        problems.append({"code": "detached_head_or_branch_unknown"})\n'''
    new = '''    branch = git_value(repo_root, "branch", "--show-current")\n    commit_sha = git_value(repo_root, "rev-parse", "HEAD")\n    source_contract = config.get("source", {}) if isinstance(config.get("source"), dict) else {}\n    branch_contract = source_contract.get("branch_contract", {})\n    if not isinstance(branch_contract, dict):\n        branch_contract = {}\n    branch_mode = str(branch_contract.get("mode", "detect_current"))\n    require_attached = bool(branch_contract.get("require_attached_head", True))\n    if require_attached and not branch:\n        problems.append({"code": "detached_head_or_branch_unknown"})\n    if not commit_sha:\n        problems.append({"code": "commit_sha_unavailable"})\n    if branch_mode == "exact":\n        expected_branch = str(branch_contract.get("branch", ""))\n        if not expected_branch:\n            problems.append({"code": "branch_contract_exact_missing"})\n        elif branch and branch != expected_branch:\n            problems.append({"code": "branch_mismatch", "expected": expected_branch, "observed": branch})\n    elif branch_mode != "detect_current":\n        problems.append({"code": "branch_contract_mode_unknown", "observed": branch_mode})\n    dirty = git_value(repo_root, "status", "--porcelain", "--untracked-files=normal")\n'''
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: branch preflight block not found exactly once')
    text = text.replace(old, new, 1)

    old = '''    write_json(preflight_path, {"suiteVersion": SUITE_VERSION, "runKind": run_kind, "selectedRunCount": len(runs), "problems": problems})\n'''
    new = '''    write_json(preflight_path, {\n        "suiteVersion": SUITE_VERSION,\n        "runKind": run_kind,\n        "selectedRunCount": len(runs),\n        "gitIdentity": {\n            "branch": git_value(repo_root, "branch", "--show-current"),\n            "commitSha": git_value(repo_root, "rev-parse", "HEAD"),\n        },\n        "problems": problems,\n    })\n'''
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: preflight write block not found exactly once')
    text = text.replace(old, new, 1)

    old = '''        "runtimeCommitSha": git_value(repo_root, "rev-parse", "HEAD"),\n        "generatedUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),\n'''
    new = '''        "runtimeBranch": git_value(repo_root, "branch", "--show-current"),\n        "runtimeCommitSha": git_value(repo_root, "rev-parse", "HEAD"),\n        "generatedUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),\n'''
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: summary identity block not found exactly once')
    text = text.replace(old, new, 1)

    pattern = re.compile(
        r'''        # Legacy configs store scenario assertions in the suite config rather than\n'''
        r'''        # in the timeline JSON itself\. Mirror them into the timeline file so the\n'''
        r'''        # stricter current implementation sees identical assertions on both sides\.\n'''
        r'''        for scenario in working\.get\('scenarios', \[\]\):\n'''
        r'''(?:.*\n)*?            write_json\(timeline, payload\)\n''',
        re.MULTILINE,
    )
    replacement = '''        # Scenario source files are immutable experiment inputs.  The effective\n        # assertions are carried in the generated run record; never write back to\n        # benchmarks/novelty01/scenarios during dry-run or execution.\n'''
    text, count = pattern.subn(lambda _match: replacement, text, count=1)
    if count != 1:
        raise RuntimeError(f'{path}: destructive timeline compatibility block not found')

    write(path, text)


def patch_idle_validation() -> None:
    path = 'apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl'
    text = read(path)
    old = '        if (timeline.events.empty()) return false;\n'
    new = '''        const bool allowsZeroMeasuredEvents =\n            timeline.scenarioId == ExperimentScenarioId::S01_Idle;\n        if (timeline.events.empty() && !allowsZeroMeasuredEvents) return false;\n'''
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: events.empty guard not found exactly once')
    text = text.replace(old, new, 1)
    old = '        if (measuredEventCount == 0u) return false;\n'
    new = '        if (measuredEventCount == 0u && !allowsZeroMeasuredEvents) return false;\n'
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: measured event guard not found exactly once')
    text = text.replace(old, new, 1)
    write(path, text)


def patch_local_capture() -> None:
    header = 'apps/dx11_viewer/src/GpuHairViewer.h'
    text = read(header)
    marker = '        int ValidateCanonicalFrozenAssetSource(const std::filesystem::path& sourceDirectory);\n'
    addition = marker + '        bool ExportLocalDebugCapture(const std::filesystem::path& outputDirectory, const std::wstring& runName) const;\n'
    if text.count(marker) != 1:
        raise RuntimeError(f'{header}: public insertion marker not found')
    text = text.replace(marker, addition, 1)
    write(header, text)

    part = 'apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl'
    text = read(part)
    marker = '    bool GpuHairViewer::ProjectWorldToScreen(const Vec3& world, float& outX, float& outY) const\n'
    implementation = r'''    bool GpuHairViewer::ExportLocalDebugCapture(
        const std::filesystem::path& outputDirectory,
        const std::wstring& runName) const
    {
        std::error_code errorCode;
        std::filesystem::create_directories(outputDirectory, errorCode);
        if (errorCode || runName.empty())
        {
            return false;
        }

        const std::filesystem::path stem = outputDirectory / runName;
        const bool frameCsvOk = ExportPerformanceCsv((stem.wstring() + L"_frames.csv"));
        const bool frameJsonlOk = ExportPerformanceJsonl((stem.wstring() + L"_frames.jsonl"));
        const bool perSecondOk = ExportPerSecondTelemetryCsv((stem.wstring() + L"_per_second.csv"));
        const bool settingsOk = ExportExperimentSettingsJson((stem.wstring() + L"_settings.json"));
        const bool summaryOk = ExportRuntimeReport((stem.wstring() + L"_summary.json"));
        return frameCsvOk && frameJsonlOk && perSecondOk && settingsOk && summaryOk;
    }

'''
    if text.count(marker) != 1:
        raise RuntimeError(f'{part}: local capture implementation marker not found')
    text = text.replace(marker, implementation + marker, 1)
    write(part, text)

    app = 'apps/dx11_viewer/src/AppMain.cpp'
    text = read(app)
    marker = '        SetEnvironmentValue(L"HAIRGPU_LOCAL_DEBUG_RUN_NAME", options.runName);\n'
    addition = marker + '''        SetEnvironmentValue(\n            L"HAIRGPU_MAX_FRAMES",\n            std::to_wstring(options.warmupFrames + options.captureFrames));\n'''
    if text.count(marker) != 1:
        raise RuntimeError(f'{app}: local capture env marker not found')
    text = text.replace(marker, addition, 1)

    old = '''    const int viewerExitCode = app.Run();\n    if (!localCapture.enabled)\n    {\n        return viewerExitCode;\n    }\n\n    const DWORD postprocessExitCode =\n        RunLocalDebugPostprocessor(localCapture, viewerExitCode);\n'''
    new = '''    const int viewerExitCode = app.Run();\n    if (!localCapture.enabled)\n    {\n        return viewerExitCode;\n    }\n\n    if (!app.ExportLocalDebugCapture(localCapture.runDirectory, localCapture.runName))\n    {\n        std::ofstream error(localCapture.runDirectory / L"TELEMETRY_EXPORT_ERROR.txt");\n        error << "Viewer failed to export local debugger telemetry.\\n";\n        return viewerExitCode == 0 ? 89 : viewerExitCode;\n    }\n\n    const DWORD postprocessExitCode =\n        RunLocalDebugPostprocessor(localCapture, viewerExitCode);\n'''
    if text.count(old) != 1:
        raise RuntimeError(f'{app}: AppMain post-run block not found exactly once')
    text = text.replace(old, new, 1)
    write(app, text)


def patch_batch() -> None:
    path = 'tools/final_stage/Run_Final_Runtime_Suite.bat'
    text = read(path)
    old = 'if "%MODE%"=="" set "MODE=proposal-smoke"\n'
    new = '''if "%MODE%"=="" (\n  echo ERROR: mode is required.\n  echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^> ^<mode^> [output_dir]\n  exit /b 2\n)\n'''
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: implicit mode line not found')
    text = text.replace(old, new, 1)

    old = '  set "FILTER_ARGS=--only-run-name L30_B8_Walk"\n'
    new = '  set "FILTER_ARGS=--only-run-name L30_B0_Walk --only-run-name L30_B6_Walk --only-run-name L30_B8_Walk --only-run-name L30_B8_StrongWind"\n'
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: prenovelty soak selection not found')
    text = text.replace(old, new, 1)

    pattern = re.compile(
        r''':startup20\n'''
        r'''echo Running 20 hidden viewer startup and shader initialization repetitions\.\.\.\n'''
        r'''for /L %%N in \(1,1,20\) do \(\n'''
        r'''(?:.*\n)*?'''
        r'''echo Startup20 completed without failure\.\n'''
        r'''exit /b 0\n''',
        re.MULTILINE,
    )
    replacement = r''':startup20
set "STARTUP_REPORT=%ROOT%\out\startup20\startup20_summary.json"
if not "%~3"=="" set "STARTUP_REPORT=%~f3\startup20_summary.json"
for %%I in ("%STARTUP_REPORT%") do if not exist "%%~dpI" mkdir "%%~dpI"
echo Running 20 real Viewer startup/render-loop/shutdown repetitions...
python "%~dp0hairgpu_startup_self_test.py" ^
  --viewer "%VIEWER%" ^
  --repetitions 20 ^
  --startup-timeout 120 ^
  --run-seconds 5 ^
  --shutdown-timeout 30 ^
  --report "%STARTUP_REPORT%"
exit /b %errorlevel%
'''
    text, count = pattern.subn(lambda _match: replacement, text, count=1)
    if count != 1:
        raise RuntimeError(f'{path}: startup20 block not found')
    write(path, text)


def patch_ci() -> None:
    path = '.github/workflows/windows-build-test.yml'
    text = read(path)
    old = '''on:\n  push:\n    branches: [Novelty]\n  pull_request:\n    branches: [Novelty]\n'''
    new = '''on:\n  push:\n  pull_request:\n'''
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: branch-scoped trigger block not found')
    text = text.replace(old, new, 1)
    write(path, text)


def add_regression_test() -> None:
    path = ROOT / 'tests/final_stage/test_prenovelty_test_environment_contract.py'
    content = r'''from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "final_stage"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import hairgpu_runtime_suite as suite


class PrenoveltyTestEnvironmentContractTests(unittest.TestCase):
    def test_suite_detects_current_branch_without_hardcoded_name(self) -> None:
        payload = json.loads(
            (ROOT / "docs/sample_assets/prenovelty_runtime_suite.json").read_text(
                encoding="utf-8"
            )
        )
        source = payload["source"]
        self.assertNotIn("required_branch", source)
        contract = source["branch_contract"]
        self.assertEqual(contract["mode"], "detect_current")
        self.assertTrue(contract["require_attached_head"])
        self.assertTrue(contract["record_commit"])

    def test_expand_run_matrix_does_not_modify_timeline_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            timeline = root / "idle.json"
            timeline.write_text(
                json.dumps(
                    {
                        "scenario": "S01_Idle",
                        "durationFrames": 6600,
                        "fixedTimeStepSeconds": 1.0 / 60.0,
                        "events": [],
                        "assertions": {"exactAppliedEventCount": 0},
                    }
                ),
                encoding="utf-8",
            )
            before = timeline.read_bytes()
            config = {
                "run_defaults": {
                    "guide_strand_count": 4000,
                    "followers_per_guide": 24,
                    "display_strand_count": 100000,
                    "particles_per_guide": 128,
                    "solver_iterations": 6,
                    "warmup_frames": 600,
                    "measurement_frames": 6000,
                    "fixed_timestep": True,
                    "fixed_timestep_seconds": 1.0 / 60.0,
                    "width": 1920,
                    "height": 1080,
                },
                "source": {},
                "methods": [{"code": "B0", "settings": {}}],
                "scenarios": [
                    {
                        "code": "S01_Idle",
                        "timeline": str(timeline),
                        "assertions": {"min_applied_events": 0},
                    }
                ],
                "seeds": [1103],
                "repetitions_per_seed": 1,
            }
            runs = suite.expand_run_matrix(config, root, ROOT)
            self.assertTrue(runs)
            self.assertEqual(before, timeline.read_bytes())

    def test_idle_zero_event_guard_is_present(self) -> None:
        parts_dir = ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts"
        source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted(parts_dir.glob("*.inl"))
        )
        self.assertIn("allowsZeroMeasuredEvents", source)
        self.assertIn("ExperimentScenarioId::S01_Idle", source)

    def test_startup20_uses_real_viewer_lifecycle_test(self) -> None:
        batch = (ROOT / "tools/final_stage/Run_Final_Runtime_Suite.bat").read_text(
            encoding="utf-8"
        )
        self.assertIn("hairgpu_startup_self_test.py", batch)
        self.assertIn("mode is required", batch)


if __name__ == "__main__":
    unittest.main()
'''
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8', newline='')
    CHANGED.append(path.relative_to(ROOT).as_posix())


def main() -> int:
    patch_suite_config()
    patch_runtime_suite()
    patch_idle_validation()
    patch_local_capture()
    patch_batch()
    patch_ci()
    add_regression_test()
    report = ROOT / 'PRENOVELTY_TEST_ENVIRONMENT_PATCH_REPORT.txt'
    report.write_text(
        'Applied pre-novelty stability/test-environment fixes:\n' +
        ''.join(f'- {item}\n' for item in CHANGED),
        encoding='utf-8',
    )
    print('\n'.join(CHANGED))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
