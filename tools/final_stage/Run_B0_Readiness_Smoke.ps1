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
    $exitCode = 999
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
$SmokeScript = Join-Path $Repo "tools\final_stage\hairgpu_b0_readiness_smoke.py"
$TestsDir = Join-Path $Repo "tests\final_stage"
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$Output = Join-Path $Repo "out\b0_readiness_smoke_$Stamp"
$Result = Join-Path $Output "READINESS_SMOKE_RESULT.json"

try {
    Get-Process HairSystem -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    foreach ($required in @($Solution, $SmokeScript)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Required file is missing: $required"
        }
    }

    Set-Location -LiteralPath $Repo

    $branch = (& git.exe -C $Repo branch --show-current | Out-String).Trim()
    $commit = (& git.exe -C $Repo rev-parse HEAD | Out-String).Trim()
    if ($branch -ne "main") {
        throw "main branch is required. Current=$branch"
    }

    $Python = Get-PythonCommand
    $MSBuild = Get-MSBuildPath
    New-Item -ItemType Directory -Path $Output -Force | Out-Null

    Write-Host "============================================================"
    Write-Host "HairSystem B0 Readiness Smoke"
    Write-Host "============================================================"
    Write-Host "Branch : $branch"
    Write-Host "Commit : $commit"
    Write-Host "Output : $Output"
    Write-Host "Steps  : unit tests -> Release rebuild -> self-tests -> 120+600 frames"
    Write-Host "============================================================"

    $compileArguments = @($Python.Prefix) + @(
        "-m", "py_compile", $SmokeScript
    )
    Invoke-NativeChecked `
        -FilePath $Python.FilePath `
        -Arguments $compileArguments `
        -Label "readiness smoke Python syntax"

    $unitArguments = @($Python.Prefix) + @(
        "-m", "unittest", "discover",
        "-s", $TestsDir,
        "-p", "test_b0_readiness_smoke.py",
        "-v"
    )
    Invoke-NativeChecked `
        -FilePath $Python.FilePath `
        -Arguments $unitArguments `
        -Label "B0 readiness validator tests"

    $contractArguments = @($Python.Prefix) + @(
        "-m", "unittest", "discover",
        "-s", $TestsDir,
        "-p", "test_prenovelty_test_environment_contract.py",
        "-v"
    )
    Invoke-NativeChecked `
        -FilePath $Python.FilePath `
        -Arguments $contractArguments `
        -Label "formal environment contract tests"

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
        -Label "shader self-test"

    $smokeArguments = @($Python.Prefix) + @(
        $SmokeScript,
        "--repo-root", $Repo,
        "--viewer-exe", $Viewer,
        "--output", $Output,
        "--warmup-frames", "120",
        "--measurement-frames", "600",
        "--timeout-seconds", "420"
    )
    Invoke-NativeChecked `
        -FilePath $Python.FilePath `
        -Arguments $smokeArguments `
        -Label "B0/S01 100k readiness smoke"

    if (-not (Test-Path -LiteralPath $Result -PathType Leaf)) {
        throw "Readiness result was not generated: $Result"
    }

    $payload = Get-Content -LiteralPath $Result -Raw | ConvertFrom-Json
    if ($payload.status -ne "PASS") {
        throw "Readiness result is not PASS: $Result"
    }
    if ([int]$payload.csvRows -ne 600 -or [int]$payload.jsonlRows -ne 600) {
        throw "Readiness output does not contain exactly 600 CSV and JSONL rows."
    }
    if ([int]$payload.totalStrandCount -ne 100000) {
        throw "Readiness output is not the exact 100k workload."
    }

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "B0 READINESS TEST COMPLETE: PASS"
    Write-Host "============================================================"
    Write-Host "CSV rows     : $($payload.csvRows)"
    Write-Host "JSONL rows   : $($payload.jsonlRows)"
    Write-Host "Strands      : $($payload.totalStrandCount)"
    Write-Host "Timing field : $($payload.timingField)"
    Write-Host "Timing cover : $($payload.timingCoverage)"
    Write-Host "Result       : $Result"
    Write-Host "Output       : $Output"
    Write-Host "============================================================"

    Invoke-Item -LiteralPath $Output
}
catch {
    Get-Process HairSystem -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    New-Item -ItemType Directory -Path $Output -Force | Out-Null
    $failure = Join-Path $Output "READINESS_SMOKE_FAILURE.txt"
    @(
        "status=FAIL",
        "failed_at=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
        "message=$($_.Exception.Message)",
        "output=$Output"
    ) | Set-Content -LiteralPath $failure -Encoding UTF8

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "B0 READINESS TEST COMPLETE: FAIL"
    Write-Host "============================================================"
    Write-Host $_.Exception.Message
    Write-Host "Failure: $failure"
    Write-Host "Output : $Output"
    Write-Host "============================================================"
    exit 1
}
