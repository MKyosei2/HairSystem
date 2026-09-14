param(
    [Parameter(Mandatory=$false)]
    [string]$RepoRoot = ".",

    [Parameter(Mandatory=$false)]
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Apply = Join-Path $Here "scripts\apply_thesis_practical_evaluation.py"
if (-not (Test-Path $Apply)) {
    throw "Missing patcher: $Apply"
}

$resolved = (Resolve-Path $RepoRoot).Path
$argsList = @($Apply, "--repo", $resolved)
if ($Force) {
    $argsList += "--force"
}

python @argsList
if ($LASTEXITCODE -ne 0) {
    throw "Patch application failed. ExitCode=$LASTEXITCODE"
}

Write-Host ""
Write-Host "Overlay applied."
Write-Host "Next:"
Write-Host "  python -m unittest tests.final_stage.test_thesis_edit_evaluation_contract"
