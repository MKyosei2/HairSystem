param(
    [ValidateSet(
        "Prepare",
        "Asset",
        "Qualification",
        "Pilot",
        "Quality",
        "Performance",
        "Determinism",
        "Soak",
        "Report",
        "VerifyBranches",
        "FinalGate",
        "Archive"
    )]
    [string]$Stage = "Prepare",
    [string]$Repo = "C:\Users\kyose\source\repos\HairSystem",
    [string]$OutputRoot = "C:\HairSystemFormalResults",
    [string]$SessionId = "",
    [string]$Operator = "kyose",
    [string]$Seeds = "1",
    [ValidateRange(1, 100)]
    [int]$Repetitions = 1,
    [string]$SigningSubjectLike = "HairSystem Internal Test Signing",
    [ValidateSet("CurrentUser","LocalMachine")]
    [string]$SigningStoreScope = "CurrentUser",
    [switch]$ValidateTransformOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# HairSystem thesis formal runner 70.31.
#
# 70.31 deliberately reuses the proven 70.30 90-run/no-Smoke policy and changes
# only the revision-specific implementation bindings. The 70.31 matrix adds a
# fail-closed fixed-client-area window guard. Native Viewer exit code 92 remains
# fatal; it is never converted into success.

$Repo = [IO.Path]::GetFullPath($Repo)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$BaseRunner = Join-Path $Repo "tools\final_stage\Run_Thesis_B0_P_Formal_7030.ps1"

if (-not (Test-Path -LiteralPath $BaseRunner -PathType Leaf)) {
    throw "70.31 base runner is missing: $BaseRunner"
}

$source = Get-Content -LiteralPath $BaseRunner -Raw -Encoding UTF8

if ($source.IndexOf("70.30", [StringComparison]::Ordinal) -lt 0) {
    throw "70.31 expected the 70.30 base revision marker but did not find it."
}
if ($source.IndexOf("7030", [StringComparison]::Ordinal) -lt 0) {
    throw "70.31 expected 7030 implementation bindings but did not find them."
}

$source = $source.Replace("70.30", "70.31")
$source = $source.Replace("7030", "7031")

# Fail closed if any old revision binding survived. This prevents a nominally
# 70.31 session from silently executing the 70.30 matrix/report/final gate.
if ($source.IndexOf("70.30", [StringComparison]::Ordinal) -ge 0) {
    throw "70.31 transformation left a 70.30 revision marker behind."
}
if ($source.IndexOf("7030", [StringComparison]::Ordinal) -ge 0) {
    throw "70.31 transformation left a 7030 implementation binding behind."
}

foreach ($requiredBinding in @(
    "hairgpu_novelty01_matrix_7031.py",
    "hairgpu_thesis_report_7031.py",
    "hairgpu_thesis_final_gate_7031.py",
    "NOVELTY01_FORMAL_CONTRACT_7031.json",
    "test_thesis_formal_experiment_contract_7031.py",
    'formalRevision = "70.31"'
)) {
    if ($source.IndexOf($requiredBinding, [StringComparison]::Ordinal) -lt 0) {
        throw "70.31 transformed runtime is missing required binding: $requiredBinding"
    }
}

$script = [ScriptBlock]::Create($source)

$forward = @{
    Stage = $Stage
    Repo = $Repo
    OutputRoot = $OutputRoot
    SessionId = $SessionId
    Operator = $Operator
    Seeds = $Seeds
    Repetitions = $Repetitions
    SigningSubjectLike = $SigningSubjectLike
    SigningStoreScope = $SigningStoreScope
}
if ($ValidateTransformOnly) {
    $forward["ValidateTransformOnly"] = $true
}

& $script @forward
