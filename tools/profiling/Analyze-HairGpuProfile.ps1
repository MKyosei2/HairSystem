param(
    [Parameter(Mandatory=$true)][string]$ResultRoot
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $ResultRoot)) {
    throw "ResultRoot not found: $ResultRoot"
}

function Get-Number($obj, [string]$name, [double]$fallback = 0.0) {
    if ($null -eq $obj) { return $fallback }
    $p = $obj.PSObject.Properties[$name]
    if ($null -eq $p -or $null -eq $p.Value) { return $fallback }
    return [double]$p.Value
}

function Get-Field($obj, [string]$name, $fallback = "") {
    if ($null -eq $obj) { return $fallback }
    $p = $obj.PSObject.Properties[$name]
    if ($null -eq $p -or $null -eq $p.Value) { return $fallback }
    return $p.Value
}

$rows = @()
foreach ($dir in Get-ChildItem -Path $ResultRoot -Directory | Sort-Object Name) {
    $summary = Get-ChildItem -Path $dir.FullName -Filter "*_summary.json" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($summary) {
        try {
            $json = Get-Content $summary.FullName -Raw | ConvertFrom-Json
            $rows += [pscustomobject]@{
                Preset = $dir.Name
                AvgFPS = Get-Number $json "avg_fps"
                AvgFrameMs = Get-Number $json "avg_frame_ms"
                P95FrameMs = Get-Number $json "p95_frame_ms"
                WorstFrameMs = Get-Number $json "worst_frame_ms"
                SixtyFpsRatio = Get-Number $json "sixty_fps_ratio"
                AvgSimMs = Get-Number $json "avg_simulation_ms"
                AvgGpuFrameMs = Get-Number $json "avg_gpu_frame_ms"
                AvgGpuSimMs = Get-Number $json "avg_gpu_simulation_ms"
                AvgGpuHairMs = Get-Number $json "avg_gpu_hair_ms"
                AvgGpuDebugMs = Get-Number $json "avg_gpu_debug_ms"
                AvgActiveSetMs = Get-Number $json "avg_active_set_eval_ms"
                SolverIterations = Get-Field $json "solver_iterations" 0
                SelfCollision = Get-Field $json "self_collision_enabled" $false
                CpuReadbackMode = Get-Field $json "cpu_readback_mode" 0
                CpuFallbackFrames = Get-Field $json "cpu_fallback_frames" 0
                ReadbackFrames = Get-Field $json "readback_frames" 0
                AvgHairPrimitives = Get-Number $json "avg_hair_primitives"
                AvgHairPS = Get-Number $json "avg_hair_ps_invocations"
                SummaryPath = $summary.FullName
            }
        } catch {
            Write-Warning "Failed to parse $($summary.FullName): $_"
        }
    }
}

$csvPath = Join-Path $ResultRoot "RESULT.csv"
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csvPath

$baseline = $rows | Where-Object { $_.Preset -eq "00_current" } | Select-Object -First 1
$diagnosis = New-Object System.Collections.Generic.List[string]
$actions = New-Object System.Collections.Generic.List[string]

if ($baseline) {
    foreach ($r in $rows) {
        if ($r.Preset -eq $baseline.Preset) { continue }
        $delta = $baseline.AvgFrameMs - $r.AvgFrameMs
        if ($delta -ge 20.0) {
            $diagnosis.Add(("{0}: avg frame improved by {1:N2} ms, likely a primary bottleneck." -f $r.Preset, $delta))
        } elseif ($delta -ge 10.0) {
            $diagnosis.Add(("{0}: avg frame improved by {1:N2} ms, strong bottleneck candidate." -f $r.Preset, $delta))
        } elseif ($delta -ge 5.0) {
            $diagnosis.Add(("{0}: avg frame improved by {1:N2} ms, secondary bottleneck candidate." -f $r.Preset, $delta))
        }
    }

    $noReadback = $rows | Where-Object { $_.Preset -eq "01_no_readback" } | Select-Object -First 1
    if ($noReadback -and (($baseline.AvgFrameMs - $noReadback.AvgFrameMs) -ge 5.0)) {
        $actions.Add("CPU readback / GPU-CPU synchronization is costly. Move ActiveSet feature extraction to GPU or lower readback frequency.")
    }
    $noSelf = $rows | Where-Object { $_.Preset -eq "02_no_self_collision" } | Select-Object -First 1
    if ($noSelf -and (($baseline.AvgFrameMs - $noSelf.AvgFrameMs) -ge 5.0)) {
        $actions.Add("Self collision is costly. Restrict self collision to active/high-risk strands or run it at a lower frequency.")
    }
    $lowSolver = $rows | Where-Object { $_.Preset -eq "03_low_solver" } | Select-Object -First 1
    if ($lowSolver -and (($baseline.AvgFrameMs - $lowSolver.AvgFrameMs) -ge 5.0)) {
        $actions.Add("Solver iteration count is costly. Use a frame-budget controller to keep global iterations low and spend extra iterations only on high-risk regions.")
    }
    $renderLight = $rows | Where-Object { $_.Preset -eq "05_render_light" } | Select-Object -First 1
    if ($renderLight -and (($baseline.AvgFrameMs - $renderLight.AvgFrameMs) -ge 5.0)) {
        $actions.Add("Rendering is costly. Add render LOD, follower stride, or replace expensive debug/GS paths with lighter representations.")
    }
}

if ($diagnosis.Count -eq 0) { $diagnosis.Add("No single preset improved average frame time by 5 ms or more. Check combined presets, Release build, and GPU timing availability.") }
if ($actions.Count -eq 0) { $actions.Add("No dominant bottleneck was isolated. Keep the current profile as baseline and add combined presets if needed.") }

$mdPath = Join-Path $ResultRoot "RESULT.md"
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# HairGPU Auto Profile Result")
[void]$sb.AppendLine("")
[void]$sb.AppendLine(("Generated: {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss")))
[void]$sb.AppendLine("")
[void]$sb.AppendLine("## Summary")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("| Preset | Avg FPS | Avg Frame ms | P95 Frame ms | Worst Frame ms | 60FPS Ratio | Avg Sim ms | GPU Frame ms | GPU Hair ms |")
[void]$sb.AppendLine("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
foreach ($r in $rows) {
    [void]$sb.AppendLine(("| {0} | {1:N2} | {2:N2} | {3:N2} | {4:N2} | {5:P1} | {6:N2} | {7:N2} | {8:N2} |" -f $r.Preset, $r.AvgFPS, $r.AvgFrameMs, $r.P95FrameMs, $r.WorstFrameMs, $r.SixtyFpsRatio, $r.AvgSimMs, $r.AvgGpuFrameMs, $r.AvgGpuHairMs))
}
[void]$sb.AppendLine("")
[void]$sb.AppendLine("## Bottleneck Guess")
[void]$sb.AppendLine("")
foreach ($d in $diagnosis) { [void]$sb.AppendLine("- " + $d) }
[void]$sb.AppendLine("")
[void]$sb.AppendLine("## Recommended Next Action")
[void]$sb.AppendLine("")
foreach ($a in $actions) { [void]$sb.AppendLine("- " + $a) }
[void]$sb.AppendLine("")
[void]$sb.AppendLine("## Files")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("- RESULT.csv")
foreach ($r in $rows) { [void]$sb.AppendLine("- " + $r.SummaryPath) }

$sb.ToString() | Out-File -FilePath $mdPath -Encoding utf8

Write-Host "[HairGPU AutoProfile] RESULT.md: $mdPath"
Write-Host "[HairGPU AutoProfile] RESULT.csv: $csvPath"
Write-Host "[HairGPU AutoProfile] Diagnosis:"
foreach ($d in $diagnosis) { Write-Host "  - $d" }
foreach ($a in $actions) { Write-Host "  * $a" }
