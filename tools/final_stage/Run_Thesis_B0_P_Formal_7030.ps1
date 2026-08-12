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
    [string]$SigningStoreScope = "CurrentUser"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# HairSystem thesis formal runner 70.30.
#
# The proven 70.29 build/sign/freeze implementation remains the execution
# owner. 70.30 applies a fail-closed, exactly-once source transformation in
# memory so only the thesis experiment policy changes.
#
# Formal evidence:
#   Pilot        10
#   Quality      36
#   Performance  36
#   Determinism   4
#   Soak          4
#   TOTAL        90 = 45 B0 + 45 P
#
# Smoke is not formal evidence. Qualification is optional diagnostic work and
# writes outside sessions\, so it is never counted by Report/FinalGate.

$Repo = [IO.Path]::GetFullPath($Repo)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$LegacyRunner = Join-Path $Repo "tools\final_stage\Run_Thesis_B0_P_Formal.ps1"
$Contract7030 = Join-Path $Repo "docs\experiments\NOVELTY01_FORMAL_CONTRACT_7030.json"
$Matrix7030 = Join-Path $Repo "tools\final_stage\hairgpu_novelty01_matrix_7030.py"
$Report7030 = Join-Path $Repo "tools\final_stage\hairgpu_thesis_report_7030.py"
$FinalGate7030 = Join-Path $Repo "tools\final_stage\hairgpu_thesis_final_gate_7030.py"
$Test7030 = Join-Path $Repo "tests\final_stage\test_thesis_formal_experiment_contract_7030.py"

foreach ($required in @(
    $LegacyRunner,
    $Contract7030,
    $Matrix7030,
    $Report7030,
    $FinalGate7030,
    $Test7030
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "70.30 required source is missing: $required"
    }
}

function Replace-RegexOnce {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Replacement,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $regex = [System.Text.RegularExpressions.Regex]::new(
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Singleline
    )
    $matches = $regex.Matches($Text)
    if ($matches.Count -ne 1) {
        throw "70.30 source transform '$Label' expected exactly one match but found $($matches.Count). The 70.29 base changed; revalidate instead of guessing."
    }
    $evaluator = [System.Text.RegularExpressions.MatchEvaluator]{
        param($match)
        return $Replacement
    }
    return $regex.Replace($Text, $evaluator, 1)
}

function Invoke-7030PythonContractChecks {
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    $filePath = $null
    $prefix = @()
    if ($null -ne $py) {
        $filePath = $py.Source
        $prefix = @("-3")
    }
    else {
        $python = Get-Command python.exe -ErrorAction Stop
        $filePath = $python.Source
    }

    foreach ($target in @($Matrix7030, $Report7030, $FinalGate7030, $Test7030)) {
        & $filePath @($prefix + @("-m", "py_compile", $target))
        if ($LASTEXITCODE -ne 0) {
            throw "70.30 Python syntax check failed: $target"
        }
    }

    & $filePath @($prefix + @(
        "-m", "unittest", "discover",
        "-s", (Join-Path $Repo "tests\final_stage"),
        "-p", "test_thesis_formal_experiment_contract_7030.py",
        "-v"
    ))
    if ($LASTEXITCODE -ne 0) {
        throw "70.30 thesis contract tests failed."
    }
}

if ($Stage -eq "Prepare") {
    Invoke-7030PythonContractChecks
}

$source = Get-Content -LiteralPath $LegacyRunner -Raw -Encoding UTF8

$stageReplacement = @'
"Asset",
        "Qualification",
        "Pilot",
'@
$source = Replace-RegexOnce -Text $source `
    -Pattern '"Asset",\s*"Smoke",\s*"Pilot",' `
    -Replacement $stageReplacement.Trim() `
    -Label "stage-set"

$source = Replace-RegexOnce -Text $source `
    -Pattern '\$ReducedSamplingPolicy\s*=\s*"paired-thesis-100-v1"' `
    -Replacement '$ReducedSamplingPolicy = "paired-thesis-90-v1"' `
    -Label "sampling-policy"
$source = Replace-RegexOnce -Text $source `
    -Pattern '\$matrix\s*=\s*Join-Path \$Repo "tools\\final_stage\\hairgpu_novelty01_matrix\.py"' `
    -Replacement '$matrix = Join-Path $Repo "tools\final_stage\hairgpu_novelty01_matrix_7030.py"' `
    -Label "matrix-runtime-path"
$source = Replace-RegexOnce -Text $source `
    -Pattern '\$reporter\s*=\s*Join-Path \$Repo "tools\\final_stage\\hairgpu_thesis_report\.py"' `
    -Replacement '$reporter = Join-Path $Repo "tools\final_stage\hairgpu_thesis_report_7030.py"' `
    -Label "report-runtime-path"
$source = Replace-RegexOnce -Text $source `
    -Pattern '\$gate\s*=\s*Join-Path \$Repo "tools\\final_stage\\hairgpu_thesis_final_gate\.py"' `
    -Replacement '$gate = Join-Path $Repo "tools\final_stage\hairgpu_thesis_final_gate_7030.py"' `
    -Label "final-gate-runtime-path"

# Asset now ends after build/sign/freeze. Qualification is detached.
$source = Replace-RegexOnce -Text $source `
    -Pattern '(?m)^[ \t]*Invoke-PreFormalQualificationSuite -Session \$session[ \t]*\r?$' `
    -Replacement '' `
    -Label "detach-qualification-from-asset"

$qualificationCase = @'
    "Qualification" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Asset")
        Assert-SessionInvariant -Session $session | Out-Null
        Invoke-PreFormalQualificationSuite -Session $session
        Add-CompletedStage -Session $session -Name "Qualification"
    }

    "Pilot" {
'@
$source = Replace-RegexOnce -Text $source `
    -Pattern '(?ms)^[ \t]*"Smoke"\s*\{.*?^[ \t]*"Pilot"\s*\{' `
    -Replacement $qualificationCase `
    -Label "smoke-to-qualification"

$source = Replace-RegexOnce -Text $source `
    -Pattern 'Assert-CompletedStages -Session \$session -Required @\("Smoke"\)' `
    -Replacement 'Assert-CompletedStages -Session $session -Required @("Asset")' `
    -Label "pilot-dependency"

$source = Replace-RegexOnce -Text $source `
    -Pattern '"environment", "integrity", "logs", "smoke", "pilot",' `
    -Replacement '"environment", "integrity", "logs", "pilot",' `
    -Label "remove-smoke-directory"

$sessionMetadata = @'
samplingPolicy = $ReducedSamplingPolicy
            formalRevision = "70.30"
            formalSmokeIncluded = $false
            formalContractPath = "docs/experiments/NOVELTY01_FORMAL_CONTRACT_7030.json"
            fatalIssuePolicy = "nan-inf-crash-input-mismatch-corruption-v1"
            expectedFormalRunCount = 90
            expectedMethodRunCount = 45
'@
$source = Replace-RegexOnce -Text $source `
    -Pattern 'samplingPolicy = \$ReducedSamplingPolicy\s*expectedFormalRunCount = 100\s*expectedMethodRunCount = 50' `
    -Replacement $sessionMetadata.Trim() `
    -Label "session-90-run-metadata"

$source = Replace-RegexOnce -Text $source `
    -Pattern 'Reduced thesis design: 100 total runs / 50 B0 / 50 P' `
    -Replacement 'Reduced thesis design 70.30: 90 total runs / 45 B0 / 45 P; Smoke excluded' `
    -Label "prepare-message"

# Pre-Report gate is five formal matrices and 90/45/45 runs.
$source = Replace-RegexOnce -Text $source `
    -Pattern '(?m)^[ \t]*smoke = 10[ \t]*\r?$' `
    -Replacement '' `
    -Label "report-remove-smoke-profile"
$source = Replace-RegexOnce -Text $source `
    -Pattern '\$summaries\.Count -ne 6' `
    -Replacement '$summaries.Count -ne 5' `
    -Label "report-summary-count-condition"
$source = Replace-RegexOnce -Text $source `
    -Pattern 'requires exactly 6 formal matrix summaries' `
    -Replacement 'requires exactly 5 formal matrix summaries' `
    -Label "report-summary-count-message"
$source = Replace-RegexOnce -Text $source `
    -Pattern '\$total -ne 100 -or \$b0Total -ne 50 -or \$pTotal -ne 50' `
    -Replacement '$total -ne 90 -or $b0Total -ne 45 -or $pTotal -ne 45' `
    -Label "report-total-count-condition"
$source = Replace-RegexOnce -Text $source `
    -Pattern 'Expected=100/50/50' `
    -Replacement 'Expected=90/45/45' `
    -Label "report-total-count-message"
$source = Replace-RegexOnce -Text $source `
    -Pattern 'Pre-Report evidence gate: PASS \(100 total / 50 B0 / 50 P\)' `
    -Replacement 'Pre-Report evidence gate: PASS (90 total / 45 B0 / 45 P)' `
    -Label "report-pass-message"

# Thermal conditioning is diagnostic-only and explicitly rejects P8/idle.
$thermal7030 = @'
function Invoke-SoakThermalConditioning {
    param(
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Assert-SessionInvariant -Session $Session | Out-Null
    Assert-FrozenInputs -Session $Session
    Assert-HardwareProfileReady -Session $Session
    Assert-NewOutputDirectory -Path $Root
    New-Item -ItemType Directory -Path $Root -Force | Out-Null

    [int]$maximumCycles = 3
    [int]$requiredStableLoadedCycles = 2
    [double]$maximumLoadedSpreadC = 3.0
    [double]$minimumLoadedUtilizationPercent = 20.0
    [double]$minimumLoadedClockMHz = 1000.0
    $states = @()
    $loadedStates = @()
    $stable = $false
    [double]$stableSpreadC = [double]::PositiveInfinity

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "70.30 THERMAL CONDITIONING (DIAGNOSTIC, NOT THESIS EVIDENCE)"
    Write-Host "============================================================"
    Write-Host "P8/idle snapshots can never satisfy the loaded plateau test."
    Write-Host "A non-loaded result is recorded and does NOT abort formal evidence."
    Write-Host "============================================================"

    for ($cycle = 1; $cycle -le $maximumCycles; ++$cycle) {
        $cycleOutput = Join-Path $Root ("cycle_{0:D2}" -f $cycle)
        Invoke-Matrix `
            -Session $Session `
            -Profile "pilot" `
            -Scenarios "S13" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output $cycleOutput `
            -ExpectedRunCount 2

        Start-Sleep -Milliseconds 250
        $gpu = Get-NvidiaFormalGpuState
        $isLoaded = (
            [string]$gpu.pstate -ne "P8" -and
            [double]$gpu.utilizationPercent -ge $minimumLoadedUtilizationPercent -and
            [double]$gpu.graphicsClockMHz -ge $minimumLoadedClockMHz
        )
        $state = [PSCustomObject]@{
            cycle = $cycle
            capturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
            temperatureC = [double]$gpu.temperatureC
            graphicsClockMHz = [double]$gpu.graphicsClockMHz
            utilizationPercent = [double]$gpu.utilizationPercent
            pstate = [string]$gpu.pstate
            powerDrawW = [double]$gpu.powerDrawW
            powerLimitW = [double]$gpu.powerLimitW
            loaded = [bool]$isLoaded
        }
        $states += $state
        if ($isLoaded) {
            $loadedStates += $state
        }

        Write-Host ("Thermal cycle {0}: temp={1:N1}C clock={2:N0}MHz util={3:N0}% pstate={4} loaded={5}" -f `
            $cycle, $state.temperatureC, $state.graphicsClockMHz, `
            $state.utilizationPercent, $state.pstate, $state.loaded)

        if ($loadedStates.Count -ge $requiredStableLoadedCycles) {
            $window = @($loadedStates | Select-Object -Last $requiredStableLoadedCycles)
            $temperatures = @($window | ForEach-Object { [double]$_.temperatureC })
            [double]$windowMaximum = ($temperatures | Measure-Object -Maximum).Maximum
            [double]$windowMinimum = ($temperatures | Measure-Object -Minimum).Minimum
            $stableSpreadC = $windowMaximum - $windowMinimum
            if ($stableSpreadC -le $maximumLoadedSpreadC) {
                $stable = $true
                break
            }
        }
    }

    $status = if ($stable) {
        "PASS_LOADED_PLATEAU"
    }
    elseif ($loadedStates.Count -eq 0) {
        "DIAGNOSTIC_ONLY_NOT_LOADED"
    }
    else {
        "DIAGNOSTIC_ONLY_UNSTABLE"
    }

    $result = [ordered]@{
        schema = "hairsystem-soak-thermal-conditioning-v2-7030"
        status = $status
        formalEvidence = $false
        nonFatal = $true
        label = $Label
        p8IdleRejected = $true
        minimumLoadedUtilizationPercent = $minimumLoadedUtilizationPercent
        minimumLoadedClockMHz = $minimumLoadedClockMHz
        maximumCycles = $maximumCycles
        requiredStableLoadedCycles = $requiredStableLoadedCycles
        maximumLoadedSpreadC = $maximumLoadedSpreadC
        observedStableSpreadC = if ([double]::IsPositiveInfinity($stableSpreadC)) { $null } else { $stableSpreadC }
        loadedStateCount = $loadedStates.Count
        states = @($states)
        completedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    }
    Write-JsonFile -Value $result -Path (Join-Path $Root "thermal_conditioning.json")

    if ($stable) {
        Write-Host ("70.30 loaded thermal plateau observed: spread={0:N1}C" -f $stableSpreadC) -ForegroundColor Green
    }
    else {
        Write-Warning "70.30 thermal conditioning did not observe a stable loaded plateau. This is recorded as a non-fatal experimental condition; formal Soak continues."
    }
}

function Invoke-PreFormalQualificationSuite {
'@
$source = Replace-RegexOnce -Text $source `
    -Pattern '(?ms)function Invoke-SoakThermalConditioning \{.*?^function Invoke-PreFormalQualificationSuite \{' `
    -Replacement $thermal7030 `
    -Label "thermal-conditioning-7030"

$source = $source.Replace(
    "Paired 100-run thesis design is fixed, not user-selectable.",
    "Paired 90-run thesis design is fixed, not user-selectable."
)
$source = $source.Replace(
    "Paired 100-run thesis runner requires",
    "Paired 90-run thesis runner requires"
)

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

& $script @forward
