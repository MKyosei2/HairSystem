from __future__ import annotations

from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, observed {count}")
    return text.replace(old, new, 1)


runner_path = Path("tools/final_stage/Run_Thesis_B0_P_Formal.ps1")
runner = runner_path.read_text(encoding="utf-8")

helper_marker = "\n\nfunction Invoke-PreFormalQualificationSuite {"
helper_block = r'''

# ----------------------------------------------------------------------
# 70.27 soak thermal-stability hardening.
#
# Formal thresholds are not weakened. The GPU is brought to a repeatable
# loaded thermal state using a balanced B0/P Pilot pair, and the exact four-run
# Soak matrix is exercised outside sessions\ before thesis evidence starts.
# ----------------------------------------------------------------------
function Get-NvidiaFormalGpuState {
    $nvidia = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
    if ($null -eq $nvidia) {
        throw "nvidia-smi.exe is required for formal GPU thermal conditioning."
    }

    $raw = @(
        & $nvidia.Source `
            "--query-gpu=index,temperature.gpu,clocks.current.graphics,utilization.gpu,pstate,power.draw,power.limit" `
            "--format=csv,noheader,nounits"
    )
    if ($LASTEXITCODE -ne 0 -or $raw.Count -lt 1) {
        throw "Could not query NVIDIA GPU state for formal thermal conditioning."
    }

    $fields = @(([string]$raw[0]).Split(',') | ForEach-Object { $_.Trim() })
    if ($fields.Count -lt 7) {
        throw "Unexpected nvidia-smi thermal-state output: $($raw[0])"
    }

    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    return [PSCustomObject]@{
        index = [int]::Parse($fields[0], $culture)
        temperatureC = [double]::Parse($fields[1], $culture)
        graphicsClockMHz = [double]::Parse($fields[2], $culture)
        utilizationPercent = [double]::Parse($fields[3], $culture)
        pstate = [string]$fields[4]
        powerDrawW = [double]::Parse($fields[5], $culture)
        powerLimitW = [double]::Parse($fields[6], $culture)
    }
}

function Invoke-SoakThermalConditioning {
    param(
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Assert-SessionInvariant -Session $Session | Out-Null
    Assert-FrozenInputs -Session $Session
    Assert-HardwareProfileReady -Session $Session
    Assert-NewOutputDirectory -Path $Root
    New-Item -ItemType Directory -Path $Root -Force | Out-Null

    $hardwareProfiles = Read-JsonFile -Path (
        Join-Path $Repo "benchmarks\novelty01\hardware_profiles.json"
    )
    $executionContract = $hardwareProfiles.profiles.measurement_rtx4070ti_super.executionContract
    [double]$maximumTemperatureC = [double]$executionContract.maximumGpuTemperatureCelsius

    [int]$maximumCycles = 6
    [int]$requiredStableCycles = 3
    [double]$maximumEndpointSpreadC = 3.0
    $states = @()
    $stable = $false
    [double]$stableSpreadC = [double]::PositiveInfinity

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "SOAK THERMAL CONDITIONING (NOT THESIS EVIDENCE)"
    Write-Host "============================================================"
    Write-Host "Label                     : $Label"
    Write-Host "Load                      : counterbalanced B0/P Pilot S13"
    Write-Host "Required stable cycles    : $requiredStableCycles"
    Write-Host "Endpoint temperature span : <= $maximumEndpointSpreadC C"
    Write-Host "Maximum GPU temperature   : <= $maximumTemperatureC C"
    Write-Host "Formal thresholds         : UNCHANGED"
    Write-Host "============================================================"

    for ($cycle = 1; $cycle -le $maximumCycles; ++$cycle) {
        $cycleOutput = Join-Path $Root ("cycle_{0:D2}" -f $cycle)

        # Non-strict, non-evidence load. Both methods are used so conditioning
        # does not favor B0 or P.
        Invoke-Matrix `
            -Session $Session `
            -Profile "pilot" `
            -Scenarios "S13" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output $cycleOutput `
            -ExpectedRunCount 2

        Start-Sleep -Milliseconds 500
        $gpu = Get-NvidiaFormalGpuState
        $state = [PSCustomObject]@{
            cycle = $cycle
            capturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
            temperatureC = [double]$gpu.temperatureC
            graphicsClockMHz = [double]$gpu.graphicsClockMHz
            utilizationPercent = [double]$gpu.utilizationPercent
            pstate = [string]$gpu.pstate
            powerDrawW = [double]$gpu.powerDrawW
            powerLimitW = [double]$gpu.powerLimitW
        }
        $states += $state

        Write-Host ("Thermal cycle {0}: temp={1:N1}C clock={2:N0}MHz util={3:N0}% pstate={4} power={5:N1}/{6:N1}W" -f `
            $cycle, $state.temperatureC, $state.graphicsClockMHz, `
            $state.utilizationPercent, $state.pstate, $state.powerDrawW, $state.powerLimitW)

        if ([double]$state.temperatureC -gt $maximumTemperatureC) {
            $failure = [ordered]@{
                schema = "hairsystem-soak-thermal-conditioning-v1"
                status = "FAIL"
                reason = "maximum_gpu_temperature_exceeded"
                label = $Label
                maximumTemperatureC = $maximumTemperatureC
                states = @($states)
            }
            Write-JsonFile -Value $failure -Path (Join-Path $Root "thermal_conditioning.json")
            throw "GPU exceeded the unchanged formal maximum temperature during conditioning. Observed=$($state.temperatureC)C Limit=$maximumTemperatureC C"
        }

        if ($states.Count -ge $requiredStableCycles) {
            $window = @($states | Select-Object -Last $requiredStableCycles)
            $temperatures = @($window | ForEach-Object { [double]$_.temperatureC })
            [double]$windowMaximum = ($temperatures | Measure-Object -Maximum).Maximum
            [double]$windowMinimum = ($temperatures | Measure-Object -Minimum).Minimum
            $stableSpreadC = $windowMaximum - $windowMinimum
            if ($stableSpreadC -le $maximumEndpointSpreadC) {
                $stable = $true
                break
            }
        }
    }

    $result = [ordered]@{
        schema = "hairsystem-soak-thermal-conditioning-v1"
        status = if ($stable) { "PASS" } else { "FAIL" }
        formalEvidence = $false
        label = $Label
        methodBalance = "counterbalanced B0/P"
        profile = "pilot"
        scenario = "S13"
        maximumCycles = $maximumCycles
        requiredStableCycles = $requiredStableCycles
        maximumEndpointSpreadC = $maximumEndpointSpreadC
        observedStableSpreadC = if ([double]::IsPositiveInfinity($stableSpreadC)) { $null } else { $stableSpreadC }
        maximumTemperatureC = $maximumTemperatureC
        thresholdsChanged = $false
        states = @($states)
        completedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    }
    Write-JsonFile -Value $result -Path (Join-Path $Root "thermal_conditioning.json")

    if (-not $stable) {
        throw "GPU did not reach a repeatable loaded thermal plateau before Soak. Required endpoint spread <= $maximumEndpointSpreadC C across $requiredStableCycles consecutive B0/P load cycles."
    }
    Write-Host ("Soak thermal conditioning: PASS (endpoint spread={0:N1}C)" -f $stableSpreadC) -ForegroundColor Green
}
'''
runner = replace_once(runner, helper_marker, helper_block + helper_marker, "insert thermal conditioning helpers")

runner = replace_once(
    runner,
    '    Write-Host "4) Ground-truth stress S13 with formal profile"\n',
    '    Write-Host "4) Ground-truth stress S13 with formal profile"\n'
    '    Write-Host "5) Loaded B0/P thermal conditioning until a stable endpoint plateau"\n'
    '    Write-Host "6) Exact full Soak S01/S13 B0/P before thesis evidence"\n',
    "qualification legend",
)

qualification_old = '''        -ExpectedRunCount 2 `
        -DiagnosticCheckpoints

    $summary = [ordered]@{'''
qualification_new = '''        -ExpectedRunCount 2 `
        -DiagnosticCheckpoints

    Invoke-SoakThermalConditioning `
        -Session $Session `
        -Root (Join-Path $root "thermal_conditioning") `
        -Label "preformal_exact_soak"

    Invoke-Matrix `
        -Session $Session `
        -Profile "soak" `
        -Scenarios "S01,S13" `
        -SeedList "1" `
        -RepeatCount 1 `
        -Output (Join-Path $root "soak_full_prequalification") `
        -ExpectedRunCount 4 `
        -DiagnosticCheckpoints

    $summary = [ordered]@{'''
runner = replace_once(runner, qualification_old, qualification_new, "insert exact soak prequalification")

runner = replace_once(
    runner,
    '            "ground_truth_repair_stress"\n',
    '            "ground_truth_repair_stress",\n'
    '            "thermal_conditioning",\n'
    '            "soak_full_prequalification"\n',
    "qualification check list",
)

official_old = '''        Assert-HardwareProfileReady -Session $session
        Assert-FreeDiskSpace -Path $OutputRoot -MinimumFreeGiB 20.0
        Invoke-Matrix `
            -Session $session `
            -Profile "soak" `
            -Scenarios "S01,S13" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output (Join-Path $session.Directory "soak\\B0_vs_P") `
            -ExpectedRunCount 4'''
official_new = '''        Assert-HardwareProfileReady -Session $session
        Assert-FreeDiskSpace -Path $OutputRoot -MinimumFreeGiB 20.0

        $preSoakRoot = Join-Path (
            Join-Path $OutputRoot "pre_soak_conditioning"
        ) ([string]$session.Value.sessionId)
        Invoke-SoakThermalConditioning `
            -Session $session `
            -Root $preSoakRoot `
            -Label "official_soak_entry"

        Invoke-Matrix `
            -Session $session `
            -Profile "soak" `
            -Scenarios "S01,S13" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output (Join-Path $session.Directory "soak\\B0_vs_P") `
            -ExpectedRunCount 4'''
runner = replace_once(runner, official_old, official_new, "official soak thermal conditioning")
runner_path.write_text(runner, encoding="utf-8", newline="\n")


final_path = Path("tools/final_stage/hairgpu_final_run.py")
final = final_path.read_text(encoding="utf-8")
final = replace_once(
    final,
    'VALIDATION_VERSION = "01-safe-v3.9-performance-exit-hardening"',
    'VALIDATION_VERSION = "01-safe-v3.10-soak-loaded-gpu-stability"',
    "validation version",
)

measurement_old = '''        measurement_health_rows, health_discarded = self._measurement_rows(
            health_rows, warmup_frames, measurement_frames
        )

        private = values(lifetime_rows, "privateBytesMiB")'''
measurement_new = '''        measurement_health_rows, health_discarded = self._measurement_rows(
            health_rows, warmup_frames, measurement_frames
        )

        # SG-APT legitimately reduces GPU work. A graphics-clock drop while the
        # GPU is idle is therefore normal P-state management, not thermal
        # throttling. Stability metrics use activity-qualified samples whenever
        # NVIDIA utilization/power telemetry is available. Thresholds are not
        # changed, and drivers without activity telemetry keep the old behavior.
        def activity_telemetry_available(row: Mapping[str, Any]) -> bool:
            utilization = _to_float(row.get("gpuUtilizationPercent"))
            power_draw = _to_float(row.get("gpuPowerDrawW"))
            power_limit = _to_float(row.get("gpuPowerLimitW"))
            return utilization is not None or (
                power_draw is not None and power_limit is not None and power_limit > 0.0
            )

        def gpu_is_active(row: Mapping[str, Any]) -> bool:
            utilization = _to_float(row.get("gpuUtilizationPercent"))
            if utilization is not None and utilization >= 1.0:
                return True
            power_draw = _to_float(row.get("gpuPowerDrawW"))
            power_limit = _to_float(row.get("gpuPowerLimitW"))
            return (
                power_draw is not None
                and power_limit is not None
                and power_limit > 0.0
                and (power_draw / power_limit) >= 0.10
            )

        gpu_activity_filter_applied = any(
            activity_telemetry_available(row) for row in measurement_health_rows
        )
        stability_health_rows = (
            [row for row in measurement_health_rows if gpu_is_active(row)]
            if gpu_activity_filter_applied
            else list(measurement_health_rows)
        )

        private = values(lifetime_rows, "privateBytesMiB")'''
final = replace_once(final, measurement_old, measurement_new, "activity-qualified hardware window")

series_old = '''        measurement_temperatures = values(measurement_health_rows, "gpuTemperatureC")
        measurement_clocks = values(measurement_health_rows, "gpuGraphicsClockMHz")'''
series_new = '''        measurement_temperatures = values(stability_health_rows, "gpuTemperatureC")
        measurement_clocks = values(stability_health_rows, "gpuGraphicsClockMHz")
        measurement_utilization = values(measurement_health_rows, "gpuUtilizationPercent")
        loaded_utilization = values(stability_health_rows, "gpuUtilizationPercent")
        loaded_power_draw = values(stability_health_rows, "gpuPowerDrawW")'''
final = replace_once(final, series_old, series_new, "loaded stability series")

summary_old = '''            "gpuHealthSampleCount": len(measurement_health_rows),
            "gpuHealthLifetimeSampleCount": len(health_rows),
            "gpuHealthWarmupDiscardedSampleCount": health_discarded,
            "measurementSampleCount": len(measurement_rows),
            "measurementWarmupDiscardedSampleCount": measurement_discarded,
            "gpuHealthWindowPolicy": "actual-probe-sequence-warmup-fraction-v1",
            "gpuClockMetric": "max-directional-drawdown-measurement-window-v1",'''
summary_new = '''            "gpuHealthSampleCount": len(stability_health_rows),
            "gpuHealthMeasurementSampleCount": len(measurement_health_rows),
            "gpuHealthLoadedSampleCount": len(stability_health_rows),
            "gpuHealthActivityFilterApplied": gpu_activity_filter_applied,
            "gpuHealthActivityFilter": "utilization>=1pct-or-power>=10pct-limit-v1" if gpu_activity_filter_applied else "telemetry-unavailable-fallback-v1",
            "gpuUtilizationMeasurementMinPercent": min(measurement_utilization) if measurement_utilization else None,
            "gpuUtilizationMeasurementMaxPercent": max(measurement_utilization) if measurement_utilization else None,
            "gpuUtilizationLoadedMinPercent": min(loaded_utilization) if loaded_utilization else None,
            "gpuUtilizationLoadedMaxPercent": max(loaded_utilization) if loaded_utilization else None,
            "gpuPowerLoadedMaxW": max(loaded_power_draw) if loaded_power_draw else None,
            "gpuHealthLifetimeSampleCount": len(health_rows),
            "gpuHealthWarmupDiscardedSampleCount": health_discarded,
            "measurementSampleCount": len(measurement_rows),
            "measurementWarmupDiscardedSampleCount": measurement_discarded,
            "gpuHealthWindowPolicy": "actual-probe-sequence-warmup-fraction-loaded-v2",
            "gpuClockMetric": "max-directional-drawdown-loaded-measurement-window-v2",'''
final = replace_once(final, summary_old, summary_new, "hardware summary diagnostics")
final_path.write_text(final, encoding="utf-8", newline="\n")


test_path = Path("tests/final_stage/test_thesis_formal_experiment_contract.py")
tests = test_path.read_text(encoding="utf-8")
marker = '\n\nif __name__ == "__main__":'
method = r'''

    def test_soak_is_prequalified_and_gpu_stability_uses_loaded_samples(self) -> None:
        runner = (ROOT / "tools/final_stage/Run_Thesis_B0_P_Formal.ps1").read_text(encoding="utf-8")
        for required in (
            "function Invoke-SoakThermalConditioning",
            "SOAK THERMAL CONDITIONING (NOT THESIS EVIDENCE)",
            '"soak_full_prequalification"',
            '"pre_soak_conditioning"',
            "maximumEndpointSpreadC = 3.0",
            "thresholdsChanged = $false",
        ):
            self.assertIn(required, runner)
        self.assertLess(
            runner.index('"soak_full_prequalification"'),
            runner.index('Add-CompletedStage -Session $session -Name "Asset"'),
        )
        official_soak = runner.index('"Soak" {')
        self.assertLess(
            runner.index('"pre_soak_conditioning"', official_soak),
            runner.index('-Output (Join-Path $session.Directory "soak\\B0_vs_P")', official_soak),
        )

        final_path = ROOT / "tools/final_stage/hairgpu_final_run.py"
        source = final_path.read_text(encoding="utf-8")
        for required in (
            'VALIDATION_VERSION = "01-safe-v3.10-soak-loaded-gpu-stability"',
            'gpuHealthActivityFilterApplied',
            'utilization>=1pct-or-power>=10pct-limit-v1',
            'actual-probe-sequence-warmup-fraction-loaded-v2',
            'max-directional-drawdown-loaded-measurement-window-v2',
        ):
            self.assertIn(required, source)

        spec = importlib.util.spec_from_file_location("hairgpu_final_run_loaded_gpu_test", final_path)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        sampler = module.ProcessSampler(interval_seconds=1.0)
        sampler.samples = [
            {"elapsedSeconds": 0.0, "gpuTemperatureC": 65.0, "gpuGraphicsClockMHz": 2700.0, "gpuUtilizationPercent": 2.0, "gpuPowerDrawW": 31.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 1.0, "gpuTemperatureC": 66.0, "gpuGraphicsClockMHz": 2685.0, "gpuUtilizationPercent": 2.0, "gpuPowerDrawW": 30.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 2.0, "gpuTemperatureC": 52.0, "gpuGraphicsClockMHz": 210.0, "gpuUtilizationPercent": 0.0, "gpuPowerDrawW": 18.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 3.0, "gpuTemperatureC": 67.0, "gpuGraphicsClockMHz": 2670.0, "gpuUtilizationPercent": 2.0, "gpuPowerDrawW": 30.0, "gpuPowerLimitW": 285.0},
        ]
        summary = sampler.summary(warmup_frames=0, measurement_frames=4)
        self.assertTrue(summary["gpuHealthActivityFilterApplied"])
        self.assertEqual(summary["gpuHealthMeasurementSampleCount"], 4)
        self.assertEqual(summary["gpuHealthLoadedSampleCount"], 3)
        self.assertEqual(summary["gpuHealthSampleCount"], 3)
        self.assertLessEqual(summary["gpuTemperatureSpreadC"], 2.0)
        self.assertLess(summary["gpuClockDropRatio"], 0.15)

        throttled = module.ProcessSampler(interval_seconds=1.0)
        throttled.samples = [
            {"elapsedSeconds": 0.0, "gpuTemperatureC": 70.0, "gpuGraphicsClockMHz": 2700.0, "gpuUtilizationPercent": 3.0, "gpuPowerDrawW": 34.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 1.0, "gpuTemperatureC": 72.0, "gpuGraphicsClockMHz": 2715.0, "gpuUtilizationPercent": 3.0, "gpuPowerDrawW": 34.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 2.0, "gpuTemperatureC": 73.0, "gpuGraphicsClockMHz": 1800.0, "gpuUtilizationPercent": 3.0, "gpuPowerDrawW": 33.0, "gpuPowerLimitW": 285.0},
        ]
        throttled_summary = throttled.summary(warmup_frames=0, measurement_frames=3)
        self.assertEqual(throttled_summary["gpuHealthLoadedSampleCount"], 3)
        self.assertGreater(throttled_summary["gpuClockDropRatio"], 0.15)

        hardware = json.loads((ROOT / "benchmarks/novelty01/hardware_profiles.json").read_text(encoding="utf-8"))
        contract = hardware["profiles"]["measurement_rtx4070ti_super"]["executionContract"]
        self.assertEqual(float(contract["maximumGpuTemperatureCelsius"]), 88.0)
        self.assertEqual(float(contract["maximumGpuTemperatureSpreadCelsius"]), 12.0)
        self.assertEqual(float(contract["maximumGpuClockDropRatio"]), 0.15)
'''
tests = replace_once(tests, marker, method + marker, "append soak stability regression")
test_path.write_text(tests, encoding="utf-8", newline="\n")

print("70.27 existing-file patch applied")
