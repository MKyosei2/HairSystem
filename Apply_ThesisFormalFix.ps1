param(
    [string]$Repo = "C:\Users\kyose\source\repos\HairSystem",
    [string]$BackupRoot = "C:\HairSystemFormalResults\source_backups"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$PackageRoot = $PSScriptRoot
$RequiredAncestor = "0da4220c594ce0880b55f096f235aee89f987ebf"
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$BackupDirectory = Join-Path $BackupRoot "thesis_formal_fix_$Stamp"

$ReplacementFiles = @(
    "tools/final_stage/freeze_novelty01_asset.py",
    "tools/final_stage/hairgpu_novelty01_matrix.py",
    "tools/final_stage/Run_Thesis_B0_P_Formal.ps1",
    "docs/sample_assets/novelty01_100k_asset_manifest.json",
    "tests/final_stage/test_thesis_formal_experiment_contract.py"
)

$PatchedFiles = @(
    "apps/dx11_viewer/src/AppMain.cpp",
    "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json"
)

function Get-PythonCommand {
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($null -ne $py) {
        return [PSCustomObject]@{ FilePath = $py.Source; Prefix = @("-3") }
    }
    $python = Get-Command python.exe -ErrorAction Stop
    return [PSCustomObject]@{ FilePath = $python.Source; Prefix = @() }
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label
    )
    Write-Host "[RUN] $Label"
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }
    if ($code -ne 0) {
        throw "$Label failed. ExitCode=$code"
    }
}

function Backup-File {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $source = Join-Path $Repo $RelativePath
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        $destination = Join-Path $BackupDirectory $RelativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
        return $true
    }
    return $false
}

function Restore-File {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][bool]$Existed
    )
    $destination = Join-Path $Repo $RelativePath
    $backup = Join-Path $BackupDirectory $RelativePath
    if ($Existed) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $backup -Destination $destination -Force
    }
    elseif (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Force
    }
}

$Repo = [IO.Path]::GetFullPath($Repo)
if (-not (Test-Path -LiteralPath $Repo -PathType Container)) {
    throw "Repository was not found: $Repo"
}

$inside = (& git.exe -C $Repo rev-parse --is-inside-work-tree | Out-String).Trim()
if ($inside -ne "true") {
    throw "Not a Git worktree: $Repo"
}
$branch = (& git.exe -C $Repo branch --show-current | Out-String).Trim()
$head = (& git.exe -C $Repo rev-parse HEAD | Out-String).Trim()
$origin = (& git.exe -C $Repo remote get-url origin | Out-String).Trim()
if ($branch -ne "main") {
    throw "Apply this package only to the existing main branch. Current=$branch"
}
if ($origin -notmatch "MKyosei2/HairSystem") {
    throw "Unexpected repository remote: $origin"
}
& git.exe -C $Repo merge-base --is-ancestor $RequiredAncestor HEAD
if ($LASTEXITCODE -ne 0) {
    throw "HEAD is not descended from the supported Main baseline $RequiredAncestor. HEAD=$head"
}
$dirty = (& git.exe -C $Repo status --porcelain --untracked-files=all | Out-String).Trim()
if ($dirty) {
    Write-Host $dirty
    throw "The worktree must be clean before applying the package."
}

$Patch = Join-Path $PackageRoot "patches\thesis_formal_main.patch"
if (-not (Test-Path -LiteralPath $Patch -PathType Leaf)) {
    throw "Patch file is missing: $Patch"
}
foreach ($relative in $ReplacementFiles) {
    $source = Join-Path $PackageRoot $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Replacement file is missing: $source"
    }
}

New-Item -ItemType Directory -Path $BackupDirectory -Force | Out-Null
$Existence = @{}
foreach ($relative in @($PatchedFiles + $ReplacementFiles)) {
    $Existence[$relative] = Backup-File -RelativePath $relative
}

try {
    Invoke-NativeChecked `
        -FilePath "git.exe" `
        -Arguments @("-C", $Repo, "apply", "--check", "--whitespace=nowarn", $Patch) `
        -Label "Validate source patch"

    Invoke-NativeChecked `
        -FilePath "git.exe" `
        -Arguments @("-C", $Repo, "apply", "--whitespace=nowarn", $Patch) `
        -Label "Apply source patch"

    foreach ($relative in $ReplacementFiles) {
        $source = Join-Path $PackageRoot $relative
        $destination = Join-Path $Repo $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }

    $Python = Get-PythonCommand
    foreach ($relative in @(
        "tools/final_stage/freeze_novelty01_asset.py",
        "tools/final_stage/hairgpu_novelty01_matrix.py",
        "tests/final_stage/test_thesis_formal_experiment_contract.py"
    )) {
        Invoke-NativeChecked `
            -FilePath $Python.FilePath `
            -Arguments (@($Python.Prefix) + @("-m", "py_compile", (Join-Path $Repo $relative))) `
            -Label "Python syntax: $relative"
    }

    Invoke-NativeChecked `
        -FilePath $Python.FilePath `
        -Arguments (@($Python.Prefix) + @(
            "-m", "unittest", "discover",
            "-s", (Join-Path $Repo "tests\final_stage"),
            "-p", "test_thesis_formal_experiment_contract.py",
            "-v"
        )) `
        -Label "Thesis formal experiment contract tests"

    $Changed = @(& git.exe -C $Repo status --short --untracked-files=all)
    $Changed | Set-Content -LiteralPath (Join-Path $BackupDirectory "applied_files.txt") -Encoding UTF8

    [ordered]@{
        status = "PASS"
        repository = $origin
        branch = $branch
        sourceHead = $head
        requiredAncestor = $RequiredAncestor
        appliedAt = (Get-Date).ToString("o")
        backupDirectory = $BackupDirectory
        changedFiles = $Changed
        branchCreated = $false
        pushed = $false
        committed = $false
    } | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath (Join-Path $BackupDirectory "apply_result.json") -Encoding UTF8

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "THESIS FORMAL FIX APPLIED: PASS"
    Write-Host "============================================================"
    Write-Host "Repository: $Repo"
    Write-Host "Branch    : $branch"
    Write-Host "HEAD      : $head"
    Write-Host "Backup    : $BackupDirectory"
    Write-Host "No branch was created, no commit was made, and nothing was pushed."
    Write-Host "Review the diff and commit these files directly to main before Formal execution."
}
catch {
    Write-Host "Apply failed. Restoring backed-up files..."
    foreach ($relative in @($PatchedFiles + $ReplacementFiles)) {
        Restore-File -RelativePath $relative -Existed ([bool]$Existence[$relative])
    }
    throw
}
