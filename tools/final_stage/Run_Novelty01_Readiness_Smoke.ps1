param(
    [string]$Repo = "C:\Users\kyose\source\repos\HairSystem"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "[RUN] $Label"
    Write-Host "============================================================"
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $FilePath @Arguments 2>&1 |
            ForEach-Object { Write-Host $_ }
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }
    Write-Host "Exit code: $exitCode"
    if ($exitCode -ne 0) {
        throw "$Label failed. ExitCode=$exitCode"
    }
}

function Invoke-GuiChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$Label,
        [int]$TimeoutSeconds = 180
    )

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "[RUN] $Label"
    Write-Host "============================================================"

    $process = Start-Process `
        -FilePath $FilePath `
        -ArgumentList $Arguments `
        -WorkingDirectory (Split-Path -Parent $FilePath) `
        -PassThru
    $exited = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $exited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "$Label timed out after $TimeoutSeconds seconds."
    }
    $process.Refresh()
    Write-Host "Exit code: $($process.ExitCode)"
    if ($process.ExitCode -ne 0) {
        throw "$Label failed. ExitCode=$($process.ExitCode)"
    }
}

function Get-MSBuildPath {
    $direct = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($null -ne $direct) {
        return $direct.Source
    }

    $vswhere = Join-Path `
        ${env:ProgramFiles(x86)} `
        "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "vswhere.exe was not found: $vswhere"
    }
    $found = @(
        & $vswhere `
            -latest `
            -products "*" `
            -requires Microsoft.Component.MSBuild `
            -find "MSBuild\**\Bin\MSBuild.exe"
    )
    if ($LASTEXITCODE -ne 0 -or $found.Count -eq 0) {
        throw "MSBuild.exe was not found."
    }
    return ([string]$found[0]).Trim()
}

function Get-PythonCommand {
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($null -ne $py) {
        return [PSCustomObject]@{
            FilePath = $py.Source
            Prefix = @("-3")
        }
    }
    $python = Get-Command python.exe -ErrorAction Stop
    return [PSCustomObject]@{
        FilePath = $python.Source
        Prefix = @()
    }
}

$Repo = [IO.Path]::GetFullPath($Repo)
$Solution = Join-Path $Repo "HairSystem.sln"
$Viewer = Join-Path $Repo "bin\Release\HairSystem\HairSystem.exe"
$Readiness = Join-Path `
    $Repo `
    "tools\final_stage\hairgpu_novelty01_readiness_smoke.py"
$Preflight = Join-Path `
    $Repo `
    "tools\final_stage\hairgpu_novelty01_preflight.py"
$TestsDir = Join-Path $Repo "tests\final_stage"
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$Output = Join-Path $Repo "out\novelty01_readiness_smoke_$Stamp"
$Result = Join-Path $Output "NOVELTY01_READINESS_RESULT.json"

try {
    Get-Process HairSystem -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    foreach ($required in @(
        $Solution,
        $Readiness,
        $Preflight,
        (Join-Path $Repo "apps\dx11_viewer\src\GpuHairViewer_parts\18_novelty01_render_dirty_range_consumer.inl"),
        (Join-Path $Repo "apps\dx11_viewer\shaders\HairNovelty01RenderDirtyCache.hlsl")
    )) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Required file is missing: $required"
        }
    }

    Set-Location -LiteralPath $Repo
    $Branch = (& git.exe -C $Repo branch --show-current | Out-String).Trim()
    if ($Branch -ne "main") {
        throw "main branch is required. Current=$Branch"
    }

    $Allowed = @(
        "apps/dx11_viewer/src/GpuHairViewer.cpp",
        "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl",
        "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl",
        "apps/dx11_viewer/shaders/HairNovelty01RenderDirtyCache.hlsl",
        "include/hairgpu/SegmentGroupAdaptivePayload.h",
        "tools/final_stage/hairgpu_novelty01_preflight.py",
        "tools/final_stage/hairgpu_novelty01_matrix.py",
        "tools/final_stage/hairgpu_novelty01_readiness_smoke.py",
        "tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
        "tests/final_stage/test_novelty01_complete_contract.py",
        "tests/final_stage/test_novelty01_runtime_contract.py",
        "docs/experiments/NOVELTY01_MAIN_INTEGRATION_CHECKLIST.md"
    )

    $Unexpected = @()
    foreach ($line in @(git.exe -C $Repo status --porcelain --untracked-files=all)) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $path = $line.Substring(3).Trim().Replace("\", "/")
        if ($path -notin $Allowed -and -not $path.StartsWith("out/")) {
            $Unexpected += $line
        }
    }
    if ($Unexpected.Count -gt 0) {
        Write-Host "Unexpected worktree changes:"
        $Unexpected | ForEach-Object { Write-Host $_ }
        throw "Verification requires only the Novelty01 package changes."
    }

    $Python = Get-PythonCommand
    $MSBuild = Get-MSBuildPath
    New-Item -ItemType Directory -Path $Output -Force | Out-Null

    Write-Host "============================================================"
    Write-Host "HairSystem Novelty01 Readiness Verification"
    Write-Host "============================================================"
    Write-Host "Branch : $Branch"
    Write-Host "Output : $Output"
    Write-Host "Steps  : syntax -> tests -> preflight -> rebuild -> self-tests -> P/S01 120+600"
    Write-Host "============================================================"

    $compileTargets = @(
        (Join-Path $Repo "tools\final_stage\hairgpu_novelty01_matrix.py"),
        $Readiness,
        $Preflight,
        (Join-Path $Repo "tests\final_stage\test_novelty01_complete_contract.py"),
        (Join-Path $Repo "tests\final_stage\test_novelty01_runtime_contract.py")
    )
    foreach ($target in $compileTargets) {
        Invoke-NativeChecked `
            -FilePath $Python.FilePath `
            -Arguments (@($Python.Prefix) + @("-m", "py_compile", $target)) `
            -Label "Python syntax: $([IO.Path]::GetFileName($target))"
    }

    foreach ($pattern in @(
        "test_novelty01_complete_contract.py",
        "test_novelty01_runtime_contract.py",
        "test_prenovelty_test_environment_contract.py"
    )) {
        Invoke-NativeChecked `
            -FilePath $Python.FilePath `
            -Arguments (@($Python.Prefix) + @(
                "-m", "unittest", "discover",
                "-s", $TestsDir,
                "-p", $pattern,
                "-v"
            )) `
            -Label "Unit tests: $pattern"
    }

    Invoke-NativeChecked `
        -FilePath $Python.FilePath `
        -Arguments (@($Python.Prefix) + @(
            $Preflight,
            "--repo", $Repo,
            "--json", (Join-Path $Output "novelty01_preflight.json")
        )) `
        -Label "Novelty01 strict source preflight"

    Invoke-NativeChecked `
        -FilePath $MSBuild `
        -Arguments @(
            $Solution,
            "/m",
            "/t:Rebuild",
            "/nologo",
            "/verbosity:minimal",
            "/p:Configuration=Release",
            "/p:Platform=x64",
            "/p:HairGpuStep0ProfileEnabled=false",
            "/p:HairGpuFormalBuild=true"
        ) `
        -Label "Release x64 formal rebuild"

    if (-not (Test-Path -LiteralPath $Viewer -PathType Leaf)) {
        throw "Release Viewer was not generated: $Viewer"
    }

    Invoke-GuiChecked `
        -FilePath $Viewer `
        -Arguments @("--native-self-test") `
        -Label "native self-test"

    Invoke-GuiChecked `
        -FilePath $Viewer `
        -Arguments @("--shader-self-test") `
        -Label "baseline shader self-test"

    Invoke-NativeChecked `
        -FilePath $Python.FilePath `
        -Arguments (@($Python.Prefix) + @(
            $Readiness,
            "--repo-root", $Repo,
            "--viewer-exe", $Viewer,
            "--output", $Output,
            "--warmup-frames", "120",
            "--measurement-frames", "600",
            "--timeout-seconds", "600"
        )) `
        -Label "P/S01 100k Novelty01 readiness smoke"

    if (-not (Test-Path -LiteralPath $Result -PathType Leaf)) {
        throw "Novelty01 readiness result was not generated: $Result"
    }
    $Payload = Get-Content -LiteralPath $Result -Raw | ConvertFrom-Json
    if ($Payload.status -ne "PASS") {
        throw "Novelty01 readiness result is not PASS: $Result"
    }
    if ([int]$Payload.csvRows -ne 600 -or [int]$Payload.jsonlRows -ne 600) {
        throw "Novelty01 readiness output does not contain exactly 600 rows."
    }
    if ([int]$Payload.totalStrandCount -ne 100000) {
        throw "Novelty01 readiness did not use the exact 100k workload."
    }

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "NOVELTY01 VERIFICATION COMPLETE: PASS"
    Write-Host "============================================================"
    Write-Host "CSV rows              : $($Payload.csvRows)"
    Write-Host "JSONL rows            : $($Payload.jsonlRows)"
    Write-Host "SG-APT rows           : $($Payload.noveltyRows)"
    Write-Host "Payload active max    : $($Payload.payloadActiveMax)"
    Write-Host "Render regenerated max: $($Payload.renderRegeneratedVerticesMax)"
    Write-Host "Ground-truth samples  : $($Payload.groundTruthSamplesMax)"
    Write-Host "Result                : $Result"
    Write-Host "Output                : $Output"
    Write-Host "============================================================"
}
catch {
    Get-Process HairSystem -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $Output -Force | Out-Null
    $Failure = Join-Path $Output "NOVELTY01_READINESS_FAILURE.txt"
    @(
        "status=FAIL",
        "failed_at=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
        "message=$($_.Exception.Message)",
        "output=$Output"
    ) | Set-Content -LiteralPath $Failure -Encoding UTF8

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "NOVELTY01 VERIFICATION COMPLETE: FAIL"
    Write-Host "============================================================"
    Write-Host $_.Exception.Message
    Write-Host "Failure: $Failure"
    Write-Host "Output : $Output"
    Write-Host "============================================================"
    exit 1
}
