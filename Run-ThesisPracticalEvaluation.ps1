param(
    [Parameter(Mandatory=$true)]
    [string]$RepoRoot,

    [Parameter(Mandatory=$true)]
    [string]$ViewerExe,

    [Parameter(Mandatory=$false)]
    [string]$Template = "",

    [Parameter(Mandatory=$false)]
    [string]$Output = "",

    [Parameter(Mandatory=$false)]
    [ValidateSet("smoke","pilot","formal","performance","soak","determinism","gtx1660")]
    [string]$Profile = "pilot",

    [Parameter(Mandatory=$false)]
    [string]$Cases = "T01,T02,T03,T04,T05,T06,T07",

    [Parameter(Mandatory=$false)]
    [string]$Seeds = "1,2"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path $RepoRoot).Path
$ViewerExe = (Resolve-Path $ViewerExe).Path

if ([string]::IsNullOrWhiteSpace($Template)) {
    $Template = Join-Path $RepoRoot "docs\sample_assets\final_runtime_suite_template.json"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $Output = Join-Path $RepoRoot "thesis_practical_results\$stamp"
}

$script = Join-Path $RepoRoot "tools\final_stage\hairgpu_thesis_practical_matrix.py"
if (-not (Test-Path $script)) { throw "Missing: $script" }
if (-not (Test-Path $Template)) { throw "Missing: $Template" }

New-Item -ItemType Directory -Force -Path $Output | Out-Null

python $script `
    --repo $RepoRoot `
    --viewer $ViewerExe `
    --template $Template `
    --output $Output `
    --profile $Profile `
    --cases $Cases `
    --seeds $Seeds

if ($LASTEXITCODE -ne 0) {
    throw "Thesis practical matrix failed. ExitCode=$LASTEXITCODE"
}

$reportScript = Join-Path $RepoRoot "tools\final_stage\hairgpu_thesis_practical_report.py"
$reportOutput = Join-Path $Output "report"
python $reportScript --input $Output --output $reportOutput
if ($LASTEXITCODE -ne 0) {
    throw "Evidence report is incomplete. Inspect $reportOutput"
}

Write-Host ""
Write-Host "Completed."
Write-Host "Raw results : $Output"
Write-Host "Report      : $reportOutput"
