param(
    [string]$Repo = "C:\Users\kyose\source\repos\HairSystem",
    [switch]$RunVerification
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Repo = [IO.Path]::GetFullPath($Repo)
$PackageRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$Backup = Join-Path $Repo "out\before_novelty01_completion_$Stamp"
$Extract = Join-Path $Backup "Novelty01_source"
$Archive = Join-Path $Backup "Novelty01_source.zip"

function Invoke-GitChecked {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & git.exe -C $Repo @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git command failed: git $($Arguments -join ' ')"
    }
}

if (-not (Test-Path -LiteralPath $Repo -PathType Container)) {
    throw "Repository does not exist: $Repo"
}
if (-not (Test-Path -LiteralPath (Join-Path $Repo ".git"))) {
    throw "Not a Git repository: $Repo"
}

Set-Location -LiteralPath $Repo
Get-Process HairSystem -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue

$Branch = (& git.exe -C $Repo branch --show-current | Out-String).Trim()
if ($Branch -ne "main") {
    throw "Apply this package on main. Current=$Branch"
}

$TrackedStatus = @(
    git.exe -C $Repo status --porcelain --untracked-files=no
)
if ($TrackedStatus.Count -gt 0) {
    Write-Host "Tracked changes already exist:"
    $TrackedStatus | ForEach-Object { Write-Host $_ }
    throw "Commit, restore, or separately back up existing tracked changes before applying."
}

New-Item -ItemType Directory -Path $Backup -Force | Out-Null
Invoke-GitChecked fetch --prune origin main Novelty01
Invoke-GitChecked pull --ff-only origin main

$MainHead = (& git.exe -C $Repo rev-parse HEAD | Out-String).Trim()
$OriginMain = (& git.exe -C $Repo rev-parse origin/main | Out-String).Trim()
if ($MainHead -ne $OriginMain) {
    throw "HEAD does not match origin/main. HEAD=$MainHead origin/main=$OriginMain"
}

$NoveltyRef = ""
& git.exe -C $Repo rev-parse --verify --quiet origin/Novelty01 *> $null
if ($LASTEXITCODE -eq 0) {
    $NoveltyRef = "origin/Novelty01"
}
else {
    & git.exe -C $Repo rev-parse --verify --quiet Novelty01 *> $null
    if ($LASTEXITCODE -eq 0) {
        $NoveltyRef = "Novelty01"
    }
}
if ([string]::IsNullOrWhiteSpace($NoveltyRef)) {
    throw "Novelty01 branch/ref was not found."
}

$NoveltySourceFiles = @(
    "include/hairgpu/SegmentGroupAdaptivePayload.h",
    "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl",
    "tools/final_stage/hairgpu_novelty01_preflight.py"
)

$ArchiveArguments = @(
    "archive",
    "--format=zip",
    "--output=$Archive",
    $NoveltyRef,
    "--"
) + $NoveltySourceFiles
& git.exe -C $Repo @ArchiveArguments
if ($LASTEXITCODE -ne 0) {
    throw "Could not archive required Novelty01 source files."
}

New-Item -ItemType Directory -Path $Extract -Force | Out-Null
Expand-Archive -LiteralPath $Archive -DestinationPath $Extract -Force

foreach ($relative in $NoveltySourceFiles) {
    $source = Join-Path $Extract $relative
    $destination = Join-Path $Repo $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Archived Novelty01 source is missing: $relative"
    }
    New-Item `
        -ItemType Directory `
        -Path (Split-Path -Parent $destination) `
        -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

$PackageFiles = @(
    "apps/dx11_viewer/src/GpuHairViewer.cpp",
    "apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl",
    "apps/dx11_viewer/shaders/HairNovelty01RenderDirtyCache.hlsl",
    "tools/final_stage/hairgpu_novelty01_matrix.py",
    "tools/final_stage/hairgpu_novelty01_readiness_smoke.py",
    "tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
    "tests/final_stage/test_novelty01_complete_contract.py",
    "tests/final_stage/test_novelty01_runtime_contract.py",
    "docs/experiments/NOVELTY01_MAIN_INTEGRATION_CHECKLIST.md"
)

foreach ($relative in $PackageFiles) {
    $source = Join-Path $PackageRoot $relative
    $destination = Join-Path $Repo $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Package file is missing: $relative"
    }
    New-Item `
        -ItemType Directory `
        -Path (Split-Path -Parent $destination) `
        -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

$RequiredMainFoundation = Join-Path `
    $Repo `
    "apps\dx11_viewer\src\GpuHairViewer_parts\02a_required_runtime_foundation.inl"
if (-not (Test-Path -LiteralPath $RequiredMainFoundation -PathType Leaf)) {
    throw "Current Main runtime foundation is missing: $RequiredMainFoundation"
}

Invoke-GitChecked diff --check

$StatusAfter = @(
    git.exe -C $Repo status --short --untracked-files=all
)
[IO.File]::WriteAllLines(
    (Join-Path $Backup "status_after_apply.txt"),
    [string[]]$StatusAfter,
    (New-Object System.Text.UTF8Encoding($false))
)
& git.exe -C $Repo diff --binary "--output=$(Join-Path $Backup 'novelty01_completion.patch')"
if ($LASTEXITCODE -ne 0) {
    throw "Could not save the applied diff."
}

Write-Host ""
Write-Host "============================================================"
Write-Host "NOVELTY01 COMPLETION PACKAGE APPLIED"
Write-Host "============================================================"
Write-Host "Branch      : main"
Write-Host "Main commit : $MainHead"
Write-Host "Novelty ref : $NoveltyRef"
Write-Host "Backup      : $Backup"
Write-Host "No commit or push was performed."
Write-Host "============================================================"
$StatusAfter | ForEach-Object { Write-Host $_ }

if ($RunVerification) {
    $Verification = Join-Path `
        $Repo `
        "tools\final_stage\Run_Novelty01_Readiness_Smoke.ps1"
    & powershell.exe `
        -NoProfile `
        -ExecutionPolicy Bypass `
        -File $Verification `
        -Repo $Repo
    if ($LASTEXITCODE -ne 0) {
        throw "Novelty01 verification failed. ExitCode=$LASTEXITCODE"
    }
}
else {
    Write-Host ""
    Write-Host "Run verification with:"
    Write-Host "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$Repo\tools\final_stage\Run_Novelty01_Readiness_Smoke.ps1`" -Repo `"$Repo`""
}
