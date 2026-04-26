param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [string]$SourceDir = "",
    [string]$Configuration = "Unknown",
    [string]$Platform = "Windows"
)

$ErrorActionPreference = "Stop"

if ($env:HAIRGPU_DISABLE_AUTO_PROFILE_POST_BUILD -eq "1") {
    Write-Host "[HairGPU AutoProfile] Skipped by HAIRGPU_DISABLE_AUTO_PROFILE_POST_BUILD=1"
    exit 0
}

if (-not (Test-Path $Exe)) {
    Write-Warning "[HairGPU AutoProfile] Viewer executable not found: $Exe"
    exit 0
}

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
$SourceDir = (Resolve-Path $SourceDir).Path

$presetPath = Join-Path $PSScriptRoot "profile_presets.json"
if (-not (Test-Path $presetPath)) {
    Write-Warning "[HairGPU AutoProfile] Missing preset file: $presetPath"
    exit 0
}

$warmupFrames = if ($env:HAIRGPU_PROFILE_WARMUP_FRAMES) { $env:HAIRGPU_PROFILE_WARMUP_FRAMES } else { "120" }
$measureFrames = if ($env:HAIRGPU_PROFILE_MEASURE_FRAMES) { $env:HAIRGPU_PROFILE_MEASURE_FRAMES } else { "600" }
$timeoutSeconds = if ($env:HAIRGPU_PROFILE_TIMEOUT_SECONDS) { [int]$env:HAIRGPU_PROFILE_TIMEOUT_SECONDS } else { 180 }

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$resultRoot = Join-Path $SourceDir ("profile_results\{0}_{1}_{2}" -f $timestamp, $Configuration, $Platform)
New-Item -ItemType Directory -Force -Path $resultRoot | Out-Null

$presets = Get-Content $presetPath -Raw | ConvertFrom-Json

Write-Host "[HairGPU AutoProfile] Exe: $Exe"
Write-Host "[HairGPU AutoProfile] Result: $resultRoot"
Write-Host "[HairGPU AutoProfile] Warmup=$warmupFrames Measure=$measureFrames Timeout=${timeoutSeconds}s"

function Set-ScopedEnv([hashtable]$pairs) {
    $old = @{}
    foreach ($key in $pairs.Keys) {
        $old[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, [string]$pairs[$key], "Process")
    }
    return $old
}

function Restore-ScopedEnv([hashtable]$old) {
    foreach ($key in $old.Keys) {
        [Environment]::SetEnvironmentVariable($key, $old[$key], "Process")
    }
}

foreach ($preset in $presets) {
    $name = [string]$preset.name
    if ([string]::IsNullOrWhiteSpace($name)) { continue }
    $presetDir = Join-Path $resultRoot $name
    New-Item -ItemType Directory -Force -Path $presetDir | Out-Null

    $envMap = @{
        "HAIRGPU_BENCHMARK" = "1";
        "HAIRGPU_BENCHMARK_WARMUP" = $warmupFrames;
        "HAIRGPU_BENCHMARK_FRAMES" = $measureFrames;
        "HAIRGPU_AUTO_TELEMETRY" = "1";
        "HAIRGPU_TELEMETRY_DIR" = $presetDir;
        "HAIRGPU_TELEMETRY_RUN_NAME" = $name
    }
    if ($preset.env) {
        foreach ($property in $preset.env.PSObject.Properties) {
            $envMap[$property.Name] = [string]$property.Value
        }
    }

    Write-Host "[HairGPU AutoProfile] Running $name - $($preset.description)"
    $oldEnv = Set-ScopedEnv $envMap
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe -Parent) -PassThru
        if (-not $proc.WaitForExit($timeoutSeconds * 1000)) {
            Write-Warning "[HairGPU AutoProfile] Timeout: $name. Killing viewer process."
            try { $proc.Kill() } catch {}
        }
        "exit_code=$($proc.ExitCode)" | Out-File -FilePath (Join-Path $presetDir "process_status.txt") -Encoding utf8
    }
    catch {
        Write-Warning "[HairGPU AutoProfile] Failed to run preset $name : $_"
        "error=$($_.Exception.Message)" | Out-File -FilePath (Join-Path $presetDir "process_status.txt") -Encoding utf8
    }
    finally {
        Restore-ScopedEnv $oldEnv
    }
}

$analyzer = Join-Path $PSScriptRoot "Analyze-HairGpuProfile.ps1"
if (Test-Path $analyzer) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $analyzer -ResultRoot $resultRoot
}

Write-Host "[HairGPU AutoProfile] Complete: $resultRoot"
exit 0
