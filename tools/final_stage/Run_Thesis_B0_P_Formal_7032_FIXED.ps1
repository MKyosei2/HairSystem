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

# HairSystem thesis formal experiment - actual revision 70.32.
#
# Formal evidence:
#   Pilot 10 + Quality 36 + Performance 36 + Determinism 4 + Soak 4 = 90
#   B0 = 45 / P = 45
#   Smoke excluded
#   Qualification excluded
#
# 70.32 keeps all 70.31 scientific conditions and fixes only the false
# ExitCode-96 race caused by normal Win32 HWND destruction immediately before
# successful Viewer process exit. Native Viewer ExitCode 92 remains fatal.

$FormalRevision = "70.32"
$GuardRevision = "fixed-client-window-guard-v2"
$GuardEvidenceName = "formal_window_guard_7032.json"
$ExpectedPowerSchemeGuid = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"
$ExpectedGpuRegex = "^(NVIDIA )?GeForce RTX 4070 Ti SUPER$"
$ExpectedVramMinMiB = 15000
$ExpectedVramMaxMiB = 17000
$Seeds = "1"
$Repetitions = 1

$Repo = [IO.Path]::GetFullPath($Repo)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$StageRunner = Join-Path $Repo "tools\final_stage\Run_Thesis_B0_P_Formal_7032.ps1"
$PowerShellExe = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"

function Write-Section {
    param([Parameter(Mandatory = $true)][string]$Title)
    Write-Host ""
    Write-Host "================================================================"
    Write-Host $Title
    Write-Host "================================================================"
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
    $json = $Value | ConvertTo-Json -Depth 100
    $temporary = "$Path.tmp.$PID"
    [IO.File]::WriteAllText(
        $temporary,
        $json + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding($false))
    )
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Set-JsonProperty {
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
        throw "Windows PowerShell 5.1 is required. Observed=$($PSVersionTable.PSVersion)"
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
        throw "PowerShell 5.1 parser check failed: $Path"
    }
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label
    )
    Write-Section -Title "[RUN] $Label"
    & $FilePath @Arguments
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw "$Label failed. ExitCode=$code"
    }
    Write-Host "[PASS] $Label" -ForegroundColor Green
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
        throw "Could not parse active power-scheme GUID. Raw=$text"
    }
    return $match.Value.ToLowerInvariant()
}

function Assert-RepositoryState {
    if (-not (Test-Path -LiteralPath $Repo -PathType Container)) {
        throw "Repository was not found: $Repo"
    }
    $branch = (& git.exe -C $Repo branch --show-current | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $branch -ne "main") {
        throw "Formal execution requires main. Observed=$branch"
    }
    $head = (& git.exe -C $Repo rev-parse HEAD | Out-String).Trim().ToLowerInvariant()
    $originMain = (& git.exe -C $Repo rev-parse origin/main | Out-String).Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0) {
        throw "Could not resolve origin/main."
    }
    if ($ExpectedCommit) {
        $expected = $ExpectedCommit.Trim().ToLowerInvariant()
        if ($expected -notmatch "^[0-9a-f]{40}$") {
            throw "ExpectedCommit must be a full 40-character SHA. Observed=$ExpectedCommit"
        }
        if ($head -ne $expected -or $originMain -ne $expected) {
            throw "Formal commit mismatch. Expected=$expected HEAD=$head origin/main=$originMain"
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

function Assert-FormalHost {
    Write-Section -Title "FORMAL 70.32 HOST PREFLIGHT"

    Invoke-NativeChecked `
        -FilePath "powercfg.exe" `
        -Arguments @("/S", "SCHEME_MIN") `
        -Label "Activate High Performance power scheme"

    $powerGuid = Get-ActivePowerSchemeGuid
    if ($powerGuid -ne $ExpectedPowerSchemeGuid) {
        throw "High Performance GUID mismatch. Expected=$ExpectedPowerSchemeGuid Observed=$powerGuid"
    }

    Add-Type -AssemblyName System.Windows.Forms
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    if ([int]$bounds.Width -ne 1920 -or [int]$bounds.Height -ne 1080) {
        throw "Primary display must be 1920x1080. Observed=$($bounds.Width)x$($bounds.Height)"
    }

    $nvidiaSmi = Get-Command nvidia-smi.exe -ErrorAction Stop
    $line = (& $nvidiaSmi.Source "--query-gpu=name,memory.total" "--format=csv,noheader,nounits" | Select-Object -First 1)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace([string]$line)) {
        throw "nvidia-smi GPU query failed."
    }
    $parts = @([string]$line -split ",")
    if ($parts.Count -lt 2) {
        throw "Could not parse nvidia-smi output: $line"
    }
    $gpuName = $parts[0].Trim()
    $vram = 0
    if (-not [int]::TryParse($parts[1].Trim(), [ref]$vram)) {
        throw "Could not parse GPU VRAM: $line"
    }
    if ($gpuName -notmatch $ExpectedGpuRegex) {
        throw "Formal GPU mismatch. Observed=$gpuName"
    }
    if ($vram -lt $ExpectedVramMinMiB -or $vram -gt $ExpectedVramMaxMiB) {
        throw "Formal GPU VRAM mismatch. Observed=$vram MiB"
    }

    $cores = 0
    foreach ($cpu in @(Get-CimInstance Win32_Processor)) {
        $cores += [int]$cpu.NumberOfCores
    }
    if ($cores -lt 6) {
        throw "Formal host requires at least 6 physical CPU cores. Observed=$cores"
    }

    $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='C:'"
    if ($null -eq $disk -or ([double]$disk.FreeSpace / 1GB) -lt 20.0) {
        throw "At least 20 GiB free space is required on C:."
    }

    Get-Process HairSystem -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    if ($null -ne (Get-Process HairSystem -ErrorAction SilentlyContinue)) {
        throw "A stale HairSystem process is still running."
    }

    Write-Host "Power scheme: $powerGuid" -ForegroundColor Green
    Write-Host "Display     : 1920x1080" -ForegroundColor Green
    Write-Host "GPU         : $gpuName / $vram MiB" -ForegroundColor Green
    Write-Host "CPU cores   : $cores" -ForegroundColor Green
}

function Invoke-FormalStage {
    param([Parameter(Mandatory = $true)][string]$Stage)

    Write-Section -Title "FORMAL 70.32 STAGE: $Stage"
    & $PowerShellExe `
        -NoProfile `
        -ExecutionPolicy Bypass `
        -File $StageRunner `
        -Stage $Stage `
        -Repo $Repo `
        -OutputRoot $OutputRoot `
        -SessionId $SessionId `
        -Operator $Operator `
        -Seeds $Seeds `
        -Repetitions $Repetitions `
        -SigningSubjectLike $SigningSubjectLike `
        -SigningStoreScope $SigningStoreScope

    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw "FORMAL 70.32 STAGE FAILED: $Stage ExitCode=$code"
    }

    $session = Read-JsonFile -Path (Join-Path $SessionDirectory "formal_session.json")
    if (@($session.completedStages) -notcontains $Stage) {
        throw "Stage did not enter completedStages: $Stage"
    }
    Write-Host "STAGE VERIFIED: $Stage" -ForegroundColor Green
}

function Approve-HardwareProfile {
    Write-Section -Title "FORMAL 70.32 HARDWARE PROFILE APPROVAL"
    $path = Join-Path $SessionDirectory "environment\hardware_profile.json"
    $profile = Read-JsonFile -Path $path
    if ([string]$profile.status -ne "PENDING_USER_REVIEW") {
        throw "Unexpected hardware profile state: $($profile.status)"
    }
    $powerGuid = Get-ActivePowerSchemeGuid
    if ($powerGuid -ne $ExpectedPowerSchemeGuid) {
        throw "Power scheme changed after Prepare. Observed=$powerGuid"
    }
    if ([string]$profile.gpu -notmatch $ExpectedGpuRegex) {
        throw "Hardware-profile GPU mismatch. Observed=$($profile.gpu)"
    }
    if ([string]$profile.displayResolution -ne "1920x1080") {
        throw "Hardware-profile displayResolution must be 1920x1080."
    }
    if ($profile.acPowerRequired -ne $true) {
        throw "Hardware-profile acPowerRequired must remain true."
    }
    Set-JsonProperty -Object $profile -Name "status" -Value "READY"
    Set-JsonProperty -Object $profile -Name "formalRevision" -Value $FormalRevision
    Set-JsonProperty -Object $profile -Name "powerPlanGuid" -Value $ExpectedPowerSchemeGuid
    Set-JsonProperty -Object $profile -Name "powerPlan" -Value "GUID: $ExpectedPowerSchemeGuid (High Performance / SCHEME_MIN)"
    Set-JsonProperty -Object $profile -Name "notes" -Value "70.32 formal host approved by stable power-scheme GUID; localized power-plan display text is non-authoritative."
    Write-JsonUtf8NoBom -Value $profile -Path $path
    $verified = Read-JsonFile -Path $path
    if ([string]$verified.status -ne "READY" -or [string]$verified.powerPlanGuid -ne $ExpectedPowerSchemeGuid) {
        throw "Hardware profile approval did not persist correctly."
    }
    Write-Host "Hardware profile: READY" -ForegroundColor Green
}

function Assert-WindowGuardCount {
    param([Parameter(Mandatory = $true)][int]$ExpectedCount)

    $files = @(
        Get-ChildItem `
            -LiteralPath $SessionDirectory `
            -Recurse `
            -File `
            -Filter $GuardEvidenceName `
            -ErrorAction Stop
    )
    if ($files.Count -ne $ExpectedCount) {
        throw "Window Guard count mismatch. Expected=$ExpectedCount Observed=$($files.Count)"
    }
    foreach ($file in $files) {
        $guard = Read-JsonFile -Path $file.FullName
        if ([string]$guard.formalRevision -ne $FormalRevision) {
            throw "Guard formalRevision mismatch: $($file.FullName)"
        }
        if ([string]$guard.guardRevision -ne $GuardRevision) {
            throw "Guard revision mismatch: $($file.FullName)"
        }
        if ([string]$guard.status -ne "PASS") {
            $reason = ""
            if ($guard.PSObject.Properties.Name -contains "failureReason") {
                $reason = [string]$guard.failureReason
            }
            throw "Window Guard did not PASS: $($file.FullName) Reason=$reason"
        }
        if ($guard.viewerExit92RemainsFatal -ne $true) {
            throw "Viewer ExitCode 92 is not preserved as fatal: $($file.FullName)"
        }
        if (@($guard.violations).Count -ne 0) {
            throw "Window Guard recorded a real client/display violation: $($file.FullName)"
        }
        if ([double]$guard.normalShutdownWindowGraceSeconds -ne 2.0) {
            throw "70.32 shutdown grace metadata mismatch: $($file.FullName)"
        }
    }
    Write-Host "Window Guard evidence: PASS $ExpectedCount/$ExpectedCount" -ForegroundColor Green
}

function Assert-FinalEvidence {
    Assert-WindowGuardCount -ExpectedCount 90

    $reportPath = Join-Path $SessionDirectory "report\thesis_statistical_report.json"
    $report = Read-JsonFile -Path $reportPath
    if ([string]$report.formalRevision -ne $FormalRevision) {
        throw "Report formalRevision mismatch. Observed=$($report.formalRevision)"
    }
    if ([string]$report.formalWindowGuard.status -ne "PASS" -or [int]$report.formalWindowGuard.recordCount -ne 90) {
        throw "Report Window Guard audit did not PASS 90/90."
    }

    $gatePath = Join-Path $SessionDirectory "integrity\final_gate.json"
    $gate = Read-JsonFile -Path $gatePath
    if ([string]$gate.status -ne "PASS") {
        throw "FinalGate did not PASS. Observed=$($gate.status)"
    }
    if ([string]$gate.formalRevision -ne $FormalRevision) {
        throw "FinalGate formalRevision mismatch. Observed=$($gate.formalRevision)"
    }
    if ([string]$gate.formalWindowGuard.status -ne "PASS" -or [int]$gate.formalWindowGuard.recordCount -ne 90) {
        throw "FinalGate Window Guard audit did not PASS 90/90."
    }

    $session = Read-JsonFile -Path (Join-Path $SessionDirectory "formal_session.json")
    if ([string]$session.formalRevision -ne $FormalRevision) {
        throw "Session formalRevision mismatch. Observed=$($session.formalRevision)"
    }
    if ([string]$session.status -ne "FORMAL_COMPLETE_VALID") {
        throw "Session did not reach FORMAL_COMPLETE_VALID. Observed=$($session.status)"
    }
    foreach ($stage in @(
        "Prepare", "Asset", "Pilot", "Quality", "Performance",
        "Determinism", "Soak", "Report", "VerifyBranches", "FinalGate", "Archive"
    )) {
        if (@($session.completedStages) -notcontains $stage) {
            throw "Completed stage missing: $stage"
        }
    }
    if (@($session.completedStages) -contains "Smoke" -or @($session.completedStages) -contains "Qualification") {
        throw "Smoke/Qualification must not enter formal completedStages."
    }

    $archivePath = Join-Path (Join-Path $OutputRoot "archives") "$SessionId.zip"
    $shaPath = "$archivePath.sha256"
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        throw "Archive is missing: $archivePath"
    }
    if (-not (Test-Path -LiteralPath $shaPath -PathType Leaf)) {
        throw "Archive SHA-256 sidecar is missing: $shaPath"
    }
    $actual = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $recorded = (Get-Content -LiteralPath $shaPath -Raw -Encoding ASCII).Trim().ToLowerInvariant()
    if ($recorded -notmatch [regex]::Escape($actual)) {
        throw "Archive SHA-256 mismatch. Actual=$actual Sidecar=$recorded"
    }

    $repoState = Assert-RepositoryState
    Write-Section -Title "THESIS FORMAL 70.32 COMPLETE"
    Write-Host "Commit         : $($repoState.Head)"
    Write-Host "Session ID     : $SessionId"
    Write-Host "Pilot          : 10"
    Write-Host "Quality        : 36"
    Write-Host "Performance    : 36"
    Write-Host "Determinism    : 4"
    Write-Host "Soak           : 4"
    Write-Host "TOTAL          : 90 = 45 B0 + 45 P"
    Write-Host "Window Guard   : PASS 90/90 ($GuardRevision)"
    Write-Host "FinalGate      : PASS"
    Write-Host "Session status : FORMAL_COMPLETE_VALID"
    Write-Host "Archive        : $archivePath"
    Write-Host "Archive SHA256 : $actual"
}

Assert-PowerShell51

if (-not (Test-Path -LiteralPath $StageRunner -PathType Leaf)) {
    throw "Formal 70.32 stage runner is missing: $StageRunner"
}
Assert-ScriptParses -Path $PSCommandPath
Assert-ScriptParses -Path $StageRunner

if ($ValidateOnly) {
    Write-Section -Title "FORMAL 70.32 VALIDATE-ONLY"
    & $PowerShellExe `
        -NoProfile `
        -ExecutionPolicy Bypass `
        -File $StageRunner `
        -Stage Asset `
        -Repo $Repo `
        -OutputRoot $OutputRoot `
        -Operator $Operator `
        -Seeds $Seeds `
        -Repetitions $Repetitions `
        -SigningSubjectLike $SigningSubjectLike `
        -SigningStoreScope $SigningStoreScope `
        -ValidateTransformOnly
    if ($LASTEXITCODE -ne 0) {
        throw "Formal 70.32 runtime transformation validation failed. ExitCode=$LASTEXITCODE"
    }
    Write-Host "Formal 70.32 parser/runtime validation: PASS" -ForegroundColor Green
    exit 0
}

if ([string]::IsNullOrWhiteSpace($ExpectedCommit)) {
    throw "Real formal execution requires -ExpectedCommit with the exact 40-character Main SHA."
}
if ($null -eq (Get-Command git.exe -ErrorAction SilentlyContinue)) {
    throw "git.exe was not found."
}

Invoke-NativeChecked -FilePath "git.exe" -Arguments @("-C", $Repo, "fetch", "origin", "--prune") -Label "Fetch origin"
Invoke-NativeChecked -FilePath "git.exe" -Arguments @("-C", $Repo, "checkout", "main") -Label "Checkout main"
Invoke-NativeChecked -FilePath "git.exe" -Arguments @("-C", $Repo, "pull", "--ff-only", "origin", "main") -Label "Fast-forward main"
$repoState = Assert-RepositoryState
Write-Host "Formal commit: $($repoState.Head)" -ForegroundColor Green

Assert-FormalHost

if ([string]::IsNullOrWhiteSpace($SessionId)) {
    $SessionId = "thesis_7032_{0}" -f (Get-Date -Format "yyyyMMdd_HHmmss_fff")
}
if ($SessionId -notmatch "^[A-Za-z0-9_.-]+$") {
    throw "SessionId contains unsupported characters: $SessionId"
}
$SessionDirectory = Join-Path (Join-Path $OutputRoot "sessions") $SessionId
if (Test-Path -LiteralPath $SessionDirectory) {
    throw "Formal session already exists; evidence overwrite is forbidden: $SessionDirectory"
}

Write-Section -Title "NEW THESIS FORMAL 70.32 SESSION"
Write-Host "Session ID : $SessionId"
Write-Host "Commit     : $($repoState.Head)"
Write-Host "Run budget : 90 = 45 B0 + 45 P"

Invoke-FormalStage -Stage "Prepare"
$prepared = Read-JsonFile -Path (Join-Path $SessionDirectory "formal_session.json")
if ([string]$prepared.formalRevision -ne $FormalRevision) {
    throw "Prepare created the wrong formalRevision. Observed=$($prepared.formalRevision)"
}
if ([int]$prepared.expectedFormalRunCount -ne 90 -or [int]$prepared.expectedMethodRunCount -ne 45) {
    throw "Prepare run-budget mismatch."
}
if ($prepared.formalSmokeIncluded -ne $false) {
    throw "Prepare unexpectedly included Smoke."
}

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
