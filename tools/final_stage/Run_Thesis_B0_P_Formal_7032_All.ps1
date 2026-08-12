param(
    [string]$Repo = "C:\Users\kyose\source\repos\HairSystem",
    [string]$OutputRoot = "C:\HairSystemFormalResults",
    [string]$ExpectedCommit = "",
    [string]$SessionId = "",
    [string]$Operator = "kyose",
    [string]$SigningSubjectLike = "HairSystem Internal Test Signing",
    [ValidateSet("CurrentUser", "LocalMachine")]
    [string]$SigningStoreScope = "CurrentUser",
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# 70.32 is an orchestration-only revision.
#
# It deliberately does NOT change the 70.31 measurement contract, matrix,
# report policy, FinalGate policy, or fixed-client Window Guard. The only goal
# is to make the complete thesis run reproducible from one repository-owned,
# CI-parsed PowerShell script instead of an ad-hoc interactive paste.
#
# Authoritative formal evidence remains:
#   formalRevision = 70.31
#   Pilot        10
#   Quality      36
#   Performance  36
#   Determinism   4
#   Soak          4
#   TOTAL        90 = 45 B0 + 45 P
#   Smoke        excluded
#   Qualification detached / excluded

$OrchestrationRevision = "70.32"
$FormalRevision = "70.31"
$ExpectedPowerSchemeGuid = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"
$ExpectedGpuRegex = "^(NVIDIA )?GeForce RTX 4070 Ti SUPER$"
$ExpectedVramMinMiB = 15000
$ExpectedVramMaxMiB = 17000
$ExpectedSeeds = "1"
$ExpectedRepetitions = 1

$Repo = [IO.Path]::GetFullPath($Repo)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$StageRunner = Join-Path $Repo "tools\final_stage\Run_Thesis_B0_P_Formal_7031.ps1"
$PowerShellExe = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"

function Write-Section {
    param([Parameter(Mandatory = $true)][string]$Title)
    Write-Host ""
    Write-Host "================================================================"
    Write-Host $Title
    Write-Host "================================================================"
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Write-Section -Title "[RUN] $Label"
    & $FilePath @Arguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$Label failed. ExitCode=$exitCode"
    }
    Write-Host "[PASS] $Label" -ForegroundColor Green
}

function Read-JsonFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required JSON file is missing: $Path"
    }
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Write-JsonUtf8NoBom {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $json = $Value | ConvertTo-Json -Depth 40
    $temporary = "$Path.tmp.$PID"
    [IO.File]::WriteAllText(
        $temporary,
        $json + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding($false))
    )
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Set-ObjectProperty {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        $Value
    )

    if ($Object.PSObject.Properties.Name -contains $Name) {
        $Object.$Name = $Value
    }
    else {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

function Assert-PowerShell51 {
    if (
        $PSVersionTable.PSEdition -ne "Desktop" -or
        $PSVersionTable.PSVersion.Major -ne 5 -or
        $PSVersionTable.PSVersion.Minor -ne 1
    ) {
        throw "70.32 orchestrator requires Windows PowerShell 5.1. Observed=$($PSVersionTable.PSVersion)"
    }
}

function Assert-ScriptParses {
    param([Parameter(Mandatory = $true)][string]$Path)

    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $Path,
        [ref]$tokens,
        [ref]$errors
    )
    if (@($errors).Count -ne 0) {
        $errors | Format-List | Out-String | Write-Host
        throw "PowerShell parser check failed: $Path"
    }
}

function Assert-RepositoryState {
    if (-not (Test-Path -LiteralPath $Repo -PathType Container)) {
        throw "Repository was not found: $Repo"
    }

    $branch = (& git.exe -C $Repo branch --show-current | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Could not resolve current branch."
    }
    if ($branch -ne "main") {
        throw "Formal execution requires main. Observed=$branch"
    }

    $head = (& git.exe -C $Repo rev-parse HEAD | Out-String).Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0 -or $head -notmatch "^[0-9a-f]{40}$") {
        throw "Could not resolve a valid repository HEAD."
    }

    $originMain = (& git.exe -C $Repo rev-parse origin/main | Out-String).Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0) {
        throw "Could not resolve origin/main."
    }

    if ($ExpectedCommit) {
        $expected = $ExpectedCommit.Trim().ToLowerInvariant()
        if ($expected -notmatch "^[0-9a-f]{40}$") {
            throw "ExpectedCommit must be a full 40-character SHA. Observed=$ExpectedCommit"
        }
        if ($head -ne $expected) {
            throw "Local main does not match ExpectedCommit. Expected=$expected Observed=$head"
        }
        if ($originMain -ne $expected) {
            throw "origin/main does not match ExpectedCommit. Expected=$expected Observed=$originMain"
        }
    }

    $dirty = (& git.exe -C $Repo status --porcelain=v1 --untracked-files=all | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "git status failed."
    }
    if ($dirty) {
        Write-Host $dirty
        throw "Formal execution requires a clean worktree."
    }

    return [PSCustomObject]@{
        Branch = $branch
        Head = $head
        OriginMain = $originMain
    }
}

function Get-ActivePowerSchemeGuid {
    $text = (& powercfg.exe /GETACTIVESCHEME | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "powercfg /GETACTIVESCHEME failed."
    }

    $match = [regex]::Match(
        $text,
        "(?i)\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b"
    )
    if (-not $match.Success) {
        throw "Could not parse the active power-scheme GUID. Raw=$text"
    }
    return $match.Value.ToLowerInvariant()
}

function Assert-FormalHostPreflight {
    Write-Section -Title "70.32 FORMAL HOST PREFLIGHT"

    New-Item -Path "HKCU:\System\GameConfigStore" -Force | Out-Null
    New-ItemProperty `
        -Path "HKCU:\System\GameConfigStore" `
        -Name "GameDVR_Enabled" `
        -PropertyType DWord `
        -Value 0 `
        -Force | Out-Null

    New-Item -Path "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\GameDVR" -Force | Out-Null
    New-ItemProperty `
        -Path "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\GameDVR" `
        -Name "AppCaptureEnabled" `
        -PropertyType DWord `
        -Value 0 `
        -Force | Out-Null

    $gameDvr = [int](Get-ItemPropertyValue -Path "HKCU:\System\GameConfigStore" -Name "GameDVR_Enabled")
    $appCapture = [int](Get-ItemPropertyValue -Path "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\GameDVR" -Name "AppCaptureEnabled")
    if ($gameDvr -ne 0 -or $appCapture -ne 0) {
        throw "Windows background capture could not be disabled."
    }

    Invoke-NativeChecked `
        -FilePath "powercfg.exe" `
        -Arguments @("/S", "SCHEME_MIN") `
        -Label "Activate High Performance power scheme"

    $activeGuid = Get-ActivePowerSchemeGuid
    if ($activeGuid -ne $ExpectedPowerSchemeGuid) {
        throw "High Performance GUID is not active. Expected=$ExpectedPowerSchemeGuid Observed=$activeGuid"
    }
    Write-Host "Power scheme GUID: $activeGuid" -ForegroundColor Green

    Add-Type -AssemblyName System.Windows.Forms
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    if ([int]$bounds.Width -ne 1920 -or [int]$bounds.Height -ne 1080) {
        throw "Primary display must be exactly 1920x1080. Observed=$($bounds.Width)x$($bounds.Height)"
    }
    Write-Host "Primary display: 1920x1080" -ForegroundColor Green

    $nvidiaSmi = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
    if ($null -eq $nvidiaSmi) {
        throw "nvidia-smi.exe was not found."
    }

    $gpuLines = @(
        & $nvidiaSmi.Source `
            "--query-gpu=name,memory.total" `
            "--format=csv,noheader,nounits"
    )
    if ($LASTEXITCODE -ne 0 -or $gpuLines.Count -lt 1) {
        throw "nvidia-smi GPU query failed."
    }

    $gpuParts = @([string]$gpuLines[0] -split ",")
    if ($gpuParts.Count -lt 2) {
        throw "Could not parse nvidia-smi output: $($gpuLines[0])"
    }

    $gpuName = $gpuParts[0].Trim()
    $gpuVramMiB = 0
    if (-not [int]::TryParse($gpuParts[1].Trim(), [ref]$gpuVramMiB)) {
        throw "Could not parse GPU VRAM: $($gpuLines[0])"
    }
    if ($gpuName -notmatch $ExpectedGpuRegex) {
        throw "Formal GPU mismatch. Expected=GeForce RTX 4070 Ti SUPER Observed=$gpuName"
    }
    if ($gpuVramMiB -lt $ExpectedVramMinMiB -or $gpuVramMiB -gt $ExpectedVramMaxMiB) {
        throw "Formal GPU VRAM mismatch. Expected=$ExpectedVramMinMiB..$ExpectedVramMaxMiB MiB Observed=$gpuVramMiB MiB"
    }
    Write-Host "GPU: $gpuName / $gpuVramMiB MiB" -ForegroundColor Green

    $physicalCores = 0
    foreach ($cpu in @(Get-CimInstance Win32_Processor)) {
        $physicalCores += [int]$cpu.NumberOfCores
    }
    if ($physicalCores -lt 6) {
        throw "Formal host requires at least six physical CPU cores. Observed=$physicalCores"
    }
    Write-Host "Physical CPU cores: $physicalCores" -ForegroundColor Green

    $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='C:'"
    if ($null -eq $disk) {
        throw "Could not query C: free space."
    }
    $freeGiB = [double]$disk.FreeSpace / 1GB
    if ($freeGiB -lt 20.0) {
        throw ("Formal experiment requires at least 20 GiB free. Observed={0:N2} GiB" -f $freeGiB)
    }
    Write-Host ("Free disk: {0:N2} GiB" -f $freeGiB) -ForegroundColor Green

    Get-Process HairSystem -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    if ($null -ne (Get-Process HairSystem -ErrorAction SilentlyContinue)) {
        throw "A stale HairSystem process is still running."
    }
}

function Invoke-FormalStage {
    param([Parameter(Mandatory = $true)][string]$Stage)

    Write-Section -Title "FORMAL $FormalRevision STAGE: $Stage | ORCHESTRATOR $OrchestrationRevision"
    & $PowerShellExe `
        -NoProfile `
        -ExecutionPolicy Bypass `
        -File $StageRunner `
        -Stage $Stage `
        -Repo $Repo `
        -OutputRoot $OutputRoot `
        -SessionId $SessionId `
        -Operator $Operator `
        -Seeds $ExpectedSeeds `
        -Repetitions $ExpectedRepetitions `
        -SigningSubjectLike $SigningSubjectLike `
        -SigningStoreScope $SigningStoreScope

    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Formal stage failed: $Stage ExitCode=$exitCode"
    }

    $sessionPath = Join-Path $SessionDirectory "formal_session.json"
    $session = Read-JsonFile -Path $sessionPath
    if (@($session.completedStages) -notcontains $Stage) {
        throw "Stage did not enter completedStages: $Stage"
    }
    Write-Host "STAGE VERIFIED: $Stage" -ForegroundColor Green
}

function Approve-HardwareProfile {
    Write-Section -Title "70.32 HARDWARE PROFILE APPROVAL"

    $profilePath = Join-Path $SessionDirectory "environment\hardware_profile.json"
    $profile = Read-JsonFile -Path $profilePath
    if ([string]$profile.status -ne "PENDING_USER_REVIEW") {
        throw "Unexpected hardware-profile state before Asset. Observed=$($profile.status)"
    }

    $activeGuid = Get-ActivePowerSchemeGuid
    if ($activeGuid -ne $ExpectedPowerSchemeGuid) {
        throw "Power scheme changed after Prepare. Expected=$ExpectedPowerSchemeGuid Observed=$activeGuid"
    }

    foreach ($field in @(
        "cpu", "gpu", "gpuDriver", "windowsVersion", "windowsBuild",
        "visualStudioVersion", "msvcVersion", "windowsSdkVersion", "pythonVersion"
    )) {
        if (
            -not ($profile.PSObject.Properties.Name -contains $field) -or
            [string]::IsNullOrWhiteSpace([string]$profile.$field)
        ) {
            throw "Hardware profile required field is empty: $field"
        }
    }

    if ([string]$profile.gpu -notmatch $ExpectedGpuRegex) {
        throw "Hardware-profile GPU mismatch. Observed=$($profile.gpu)"
    }
    if ([double]$profile.gpuVramMiB -lt $ExpectedVramMinMiB -or [double]$profile.gpuVramMiB -gt $ExpectedVramMaxMiB) {
        throw "Hardware-profile GPU VRAM mismatch. Observed=$($profile.gpuVramMiB) MiB"
    }
    if ([double]$profile.ramMiB -le 0.0) {
        throw "Hardware-profile RAM must be positive."
    }
    if ([string]$profile.displayResolution -ne "1920x1080") {
        throw "Hardware-profile displayResolution must be 1920x1080."
    }
    if ($profile.acPowerRequired -ne $true) {
        throw "Hardware-profile acPowerRequired must remain true."
    }

    # Do not validate or preserve the localized power-plan display name.
    # Windows PowerShell 5.1 can receive mojibake from native powercfg output.
    # The GUID is the authoritative value and is stored in ASCII form.
    $profile.powerPlan = "GUID: $ExpectedPowerSchemeGuid (High Performance / SCHEME_MIN)"
    Set-ObjectProperty -Object $profile -Name "powerPlanGuid" -Value $ExpectedPowerSchemeGuid
    Set-ObjectProperty -Object $profile -Name "orchestrationRevision" -Value $OrchestrationRevision
    Set-ObjectProperty -Object $profile -Name "status" -Value "READY"
    Set-ObjectProperty `
        -Object $profile `
        -Name "notes" `
        -Value "70.32 orchestration review: active power scheme verified by GUID; localized power-plan name intentionally ignored; formal measurement policy remains 70.31."

    Write-JsonUtf8NoBom -Value $profile -Path $profilePath

    $verified = Read-JsonFile -Path $profilePath
    if ([string]$verified.status -ne "READY") {
        throw "Hardware profile could not be moved to READY."
    }
    if ([string]$verified.powerPlanGuid -ne $ExpectedPowerSchemeGuid) {
        throw "Canonical power-plan GUID was not persisted to hardware_profile.json."
    }
    Write-Host "Hardware profile: READY" -ForegroundColor Green
    Write-Host "Power plan verified by GUID: $ExpectedPowerSchemeGuid" -ForegroundColor Green
}

function Assert-WindowGuardCount {
    param([Parameter(Mandatory = $true)][int]$ExpectedCount)

    $files = @(
        Get-ChildItem `
            -LiteralPath $SessionDirectory `
            -Recurse `
            -File `
            -Filter "formal_window_guard_7031.json" `
            -ErrorAction Stop
    )

    if ($files.Count -ne $ExpectedCount) {
        throw "Window Guard evidence count mismatch. Expected=$ExpectedCount Observed=$($files.Count)"
    }

    foreach ($file in $files) {
        $guard = Read-JsonFile -Path $file.FullName
        if ([string]$guard.formalRevision -ne $FormalRevision) {
            throw "Window Guard formalRevision mismatch: $($file.FullName)"
        }
        if ([string]$guard.guardRevision -ne "fixed-client-window-guard-v1") {
            throw "Window Guard revision mismatch: $($file.FullName)"
        }
        if ([string]$guard.status -ne "PASS") {
            throw "Window Guard did not PASS: $($file.FullName)"
        }
        if ($guard.viewerExit92RemainsFatal -ne $true) {
            throw "Window Guard does not preserve Viewer ExitCode 92 as fatal: $($file.FullName)"
        }
        if (@($guard.violations).Count -ne 0) {
            throw "Window Guard recorded a client/display violation: $($file.FullName)"
        }
    }

    Write-Host "Window Guard evidence: PASS $ExpectedCount/$ExpectedCount" -ForegroundColor Green
}

function Assert-FinalEvidence {
    Write-Section -Title "70.32 FINAL EVIDENCE AUDIT"

    Assert-WindowGuardCount -ExpectedCount 90

    $reportPath = Join-Path $SessionDirectory "report\thesis_statistical_report.json"
    $report = Read-JsonFile -Path $reportPath
    if ([string]$report.formalRevision -ne $FormalRevision) {
        throw "Report formalRevision mismatch. Observed=$($report.formalRevision)"
    }
    if ([string]$report.samplingPolicy -ne "paired-thesis-90-v1") {
        throw "Report samplingPolicy mismatch. Observed=$($report.samplingPolicy)"
    }
    if ([int]$report.expectedTotalRunCount -ne 90 -or [int]$report.expectedMethodRunCount -ne 45) {
        throw "Report run-budget mismatch."
    }
    if ([string]$report.formalWindowGuard.status -ne "PASS") {
        throw "Report Window Guard audit did not PASS."
    }
    if ([int]$report.formalWindowGuard.recordCount -ne 90 -or [int]$report.formalWindowGuard.passCount -ne 90) {
        throw "Report Window Guard count mismatch."
    }
    if ([int]$report.formalWindowGuard.violationCount -ne 0) {
        throw "Report contains Window Guard violations."
    }

    $gatePath = Join-Path $SessionDirectory "integrity\final_gate.json"
    $gate = Read-JsonFile -Path $gatePath
    if ([string]$gate.status -ne "PASS") {
        throw "FinalGate did not PASS. Observed=$($gate.status)"
    }
    if ([string]$gate.formalRevision -ne $FormalRevision) {
        throw "FinalGate formalRevision mismatch. Observed=$($gate.formalRevision)"
    }
    if ([string]$gate.formalWindowGuard.status -ne "PASS") {
        throw "FinalGate Window Guard audit did not PASS."
    }
    if ([int]$gate.formalWindowGuard.recordCount -ne 90 -or [int]$gate.formalWindowGuard.passCount -ne 90) {
        throw "FinalGate Window Guard count mismatch."
    }

    $sessionPath = Join-Path $SessionDirectory "formal_session.json"
    $session = Read-JsonFile -Path $sessionPath
    if ([string]$session.status -ne "FORMAL_COMPLETE_VALID") {
        throw "Session did not reach FORMAL_COMPLETE_VALID. Observed=$($session.status)"
    }

    foreach ($stage in @(
        "Prepare", "Asset", "Pilot", "Quality", "Performance",
        "Determinism", "Soak", "Report", "VerifyBranches", "FinalGate", "Archive"
    )) {
        if (@($session.completedStages) -notcontains $stage) {
            throw "Completed stage is missing from final session: $stage"
        }
    }
    if (@($session.completedStages) -contains "Smoke") {
        throw "Smoke unexpectedly entered formal completedStages."
    }
    if (@($session.completedStages) -contains "Qualification") {
        throw "Qualification unexpectedly entered formal completedStages."
    }

    $archivePath = Join-Path (Join-Path $OutputRoot "archives") ("$SessionId.zip")
    $sidecarPath = "$archivePath.sha256"
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        throw "Formal archive is missing: $archivePath"
    }
    if (-not (Test-Path -LiteralPath $sidecarPath -PathType Leaf)) {
        throw "Formal archive SHA-256 sidecar is missing: $sidecarPath"
    }

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $sidecar = (Get-Content -LiteralPath $sidecarPath -Raw -Encoding ASCII).Trim().ToLowerInvariant()
    if ($sidecar -notmatch [regex]::Escape($actualHash)) {
        throw "Formal archive SHA-256 sidecar mismatch. Actual=$actualHash Sidecar=$sidecar"
    }

    $repoState = Assert-RepositoryState

    Write-Section -Title "THESIS FORMAL EXPERIMENT COMPLETE"
    Write-Host "Orchestration revision : $OrchestrationRevision"
    Write-Host "Formal evidence revision: $FormalRevision"
    Write-Host "Commit                 : $($repoState.Head)"
    Write-Host "Session ID             : $SessionId"
    Write-Host "Pilot                   : 10"
    Write-Host "Quality                 : 36"
    Write-Host "Performance             : 36"
    Write-Host "Determinism             : 4"
    Write-Host "Soak                    : 4"
    Write-Host "TOTAL                   : 90"
    Write-Host "B0                      : 45"
    Write-Host "P                       : 45"
    Write-Host "Window Guard            : PASS 90/90"
    Write-Host "FinalGate               : PASS"
    Write-Host "Session status          : FORMAL_COMPLETE_VALID"
    Write-Host "Report                  : $reportPath"
    Write-Host "Archive                 : $archivePath"
    Write-Host "Archive SHA-256         : $actualHash"
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

Assert-PowerShell51

if (-not (Test-Path -LiteralPath $StageRunner -PathType Leaf)) {
    throw "70.31 stage runner is missing: $StageRunner"
}

Assert-ScriptParses -Path $PSCommandPath
Assert-ScriptParses -Path $StageRunner

if ($ValidateOnly) {
    Write-Section -Title "70.32 ORCHESTRATOR VALIDATION"
    & $PowerShellExe `
        -NoProfile `
        -ExecutionPolicy Bypass `
        -File $StageRunner `
        -Stage Asset `
        -Repo $Repo `
        -OutputRoot $OutputRoot `
        -Operator $Operator `
        -Seeds $ExpectedSeeds `
        -Repetitions $ExpectedRepetitions `
        -SigningSubjectLike $SigningSubjectLike `
        -SigningStoreScope $SigningStoreScope `
        -ValidateTransformOnly
    if ($LASTEXITCODE -ne 0) {
        throw "70.31 inherited runtime transformation check failed. ExitCode=$LASTEXITCODE"
    }
    Write-Host "70.32 orchestrator parser/runtime validation: PASS" -ForegroundColor Green
    exit 0
}

if ([string]::IsNullOrWhiteSpace($ExpectedCommit)) {
    throw "Real formal execution requires -ExpectedCommit with the exact 40-character Main SHA."
}

if ($null -eq (Get-Command git.exe -ErrorAction SilentlyContinue)) {
    throw "git.exe was not found."
}

Invoke-NativeChecked `
    -FilePath "git.exe" `
    -Arguments @("-C", $Repo, "fetch", "origin", "--prune") `
    -Label "Fetch origin"

Invoke-NativeChecked `
    -FilePath "git.exe" `
    -Arguments @("-C", $Repo, "checkout", "main") `
    -Label "Checkout main"

Invoke-NativeChecked `
    -FilePath "git.exe" `
    -Arguments @("-C", $Repo, "pull", "--ff-only", "origin", "main") `
    -Label "Fast-forward main"

$initialRepoState = Assert-RepositoryState
Write-Host "Repository commit: $($initialRepoState.Head)" -ForegroundColor Green

Assert-FormalHostPreflight

if ([string]::IsNullOrWhiteSpace($SessionId)) {
    $SessionId = "thesis_7032_{0}" -f (Get-Date -Format "yyyyMMdd_HHmmss_fff")
}
if ($SessionId -notmatch "^[A-Za-z0-9_.-]+$") {
    throw "SessionId contains unsupported characters: $SessionId"
}

$SessionDirectory = Join-Path (Join-Path $OutputRoot "sessions") $SessionId
if (Test-Path -LiteralPath $SessionDirectory) {
    throw "Session already exists. Refusing overwrite: $SessionDirectory"
}

Write-Section -Title "NEW THESIS FORMAL SESSION"
Write-Host "Orchestration revision : $OrchestrationRevision"
Write-Host "Formal evidence revision: $FormalRevision"
Write-Host "Session ID             : $SessionId"
Write-Host "Session directory      : $SessionDirectory"
Write-Host "Commit                 : $($initialRepoState.Head)"
Write-Host "Formal run budget      : 90 = 45 B0 + 45 P"
Write-Host "Smoke                  : NOT EXECUTED"
Write-Host "Qualification          : NOT EXECUTED"

try {
    Invoke-FormalStage -Stage "Prepare"
    Approve-HardwareProfile

    Invoke-FormalStage -Stage "Asset"
    Assert-WindowGuardCount -ExpectedCount 0

    Invoke-FormalStage -Stage "Pilot"
    Assert-WindowGuardCount -ExpectedCount 10

    Invoke-FormalStage -Stage "Quality"
    Assert-WindowGuardCount -ExpectedCount 46

    Invoke-FormalStage -Stage "Performance"
    Assert-WindowGuardCount -ExpectedCount 82

    Invoke-FormalStage -Stage "Determinism"
    Assert-WindowGuardCount -ExpectedCount 86

    Invoke-FormalStage -Stage "Soak"
    Assert-WindowGuardCount -ExpectedCount 90

    Invoke-FormalStage -Stage "Report"
    Invoke-FormalStage -Stage "VerifyBranches"
    Invoke-FormalStage -Stage "FinalGate"
    Invoke-FormalStage -Stage "Archive"

    Assert-FinalEvidence
    exit 0
}
catch {
    Write-Section -Title "THESIS FORMAL EXPERIMENT FAILED"
    Write-Host "Orchestration revision : $OrchestrationRevision" -ForegroundColor Yellow
    Write-Host "Formal evidence revision: $FormalRevision" -ForegroundColor Yellow
    Write-Host "Session ID             : $SessionId" -ForegroundColor Yellow
    Write-Host "Session directory      : $SessionDirectory" -ForegroundColor Yellow
    Write-Host "Commit                 : $ExpectedCommit" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "ERROR:" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host ""
    Write-Host "Do not continue to a later stage manually." -ForegroundColor Yellow
    Write-Host "Do not reuse this session after a source-code change." -ForegroundColor Yellow
    exit 1
}
