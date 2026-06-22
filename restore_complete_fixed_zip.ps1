$ErrorActionPreference = "Stop"

param(
    [string]$RepoUrl = "https://github.com/MKyosei2/HairSystem.git",
    [string]$Branch = "Novelty",
    [string]$OutputName = "HairSystem_Novelty_FIXED_COMPLETE.zip",
    [string]$WorkRoot = ".",
    [switch]$KeepGit
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ResolvedWorkRoot = Resolve-Path $WorkRoot
$RepoDir = Join-Path $ResolvedWorkRoot "HairSystem_Novelty_FIXED_COMPLETE"

Write-Host "=== HairSystem Novelty full restore ==="
Write-Host "Repo   : $RepoUrl"
Write-Host "Branch : $Branch"
Write-Host "Output : $OutputName"
Write-Host ""

if (Test-Path $RepoDir) {
    Write-Host "Removing existing restore folder: $RepoDir"
    Remove-Item -Recurse -Force $RepoDir
}

Write-Host "Cloning repository..."
git clone --branch $Branch --single-branch $RepoUrl $RepoDir

Push-Location $RepoDir
try {
    Write-Host "Applying D3D11 dispatch overflow fix..."
    Copy-Item -Force (Join-Path $ScriptDir "apply_fix.ps1") ".\apply_fix.ps1"
    powershell -ExecutionPolicy Bypass -File ".\apply_fix.ps1"

    Write-Host "Verifying fix..."
    Copy-Item -Force (Join-Path $ScriptDir "verify_fix.ps1") ".\verify_fix.ps1"
    powershell -ExecutionPolicy Bypass -File ".\verify_fix.ps1"

    if (-not $KeepGit) {
        if (Test-Path ".git") {
            Write-Host "Removing .git from output copy..."
            Remove-Item -Recurse -Force ".git"
        }
    }

    if (Test-Path ".vs") {
        Write-Host "Removing .vs from output copy..."
        Remove-Item -Recurse -Force ".vs"
    }

    $BuildDirs = @("build", "out", ".cache")
    foreach ($Dir in $BuildDirs) {
        if (Test-Path $Dir) {
            Write-Host "Removing $Dir from output copy..."
            Remove-Item -Recurse -Force $Dir
        }
    }
}
finally {
    Pop-Location
}

$ZipPath = Join-Path $ResolvedWorkRoot $OutputName
if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
}

Write-Host "Creating complete fixed ZIP..."
Compress-Archive -Path (Join-Path $RepoDir "*") -DestinationPath $ZipPath -Force

Write-Host ""
Write-Host "DONE"
Write-Host "Generated: $ZipPath"
