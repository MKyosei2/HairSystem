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

# HairSystem thesis formal runner 70.32.
#
# 70.32 preserves the proven 70.31 paired 90-run/no-Smoke scientific design.
# It changes only revision-specific implementation bindings and upgrades the
# fixed-client guard to v2 so normal HWND destruction immediately before a
# successful Viewer process exit cannot be misclassified as harness ExitCode 96.
# Native Viewer ExitCode 92 remains fatal and is never converted to success.

$Repo = [IO.Path]::GetFullPath($Repo)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$BaseRunner = Join-Path $Repo "tools\final_stage\Run_Thesis_B0_P_Formal_7031.ps1"

if (-not (Test-Path -LiteralPath $BaseRunner -PathType Leaf)) {
    throw "70.32 base runner is missing: $BaseRunner"
}

$source = Get-Content -LiteralPath $BaseRunner -Raw -Encoding UTF8

if ($source.IndexOf("70.31", [StringComparison]::Ordinal) -lt 0) {
    throw "70.32 expected the 70.31 base revision marker but did not find it."
}
if ($source.IndexOf("7031", [StringComparison]::Ordinal) -lt 0) {
    throw "70.32 expected 7031 implementation bindings but did not find them."
}

# Transform the already-CI-proven 70.31 wrapper into the exact 70.32 runtime.
# Because 70.31 itself transforms 70.30, these replacements make the generated
# runtime bind directly to 7032 matrix/report/gate/contract/test files.
$source = $source.Replace("70.31", "70.32")
$source = $source.Replace("7031", "7032")

if ($source.IndexOf("70.31", [StringComparison]::Ordinal) -ge 0) {
    throw "70.32 transformation left a 70.31 revision marker behind."
}
if ($source.IndexOf("7031", [StringComparison]::Ordinal) -ge 0) {
    throw "70.32 transformation left a 7031 implementation binding behind."
}

foreach ($requiredBinding in @(
    "hairgpu_novelty01_matrix_7032.py",
    "hairgpu_thesis_report_7032.py",
    "hairgpu_thesis_final_gate_7032.py",
    "NOVELTY01_FORMAL_CONTRACT_7032.json",
    "test_thesis_formal_experiment_contract_7032.py",
    'formalRevision = "70.32"'
)) {
    if ($source.IndexOf($requiredBinding, [StringComparison]::Ordinal) -lt 0) {
        throw "70.32 transformed runtime is missing required binding: $requiredBinding"
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
