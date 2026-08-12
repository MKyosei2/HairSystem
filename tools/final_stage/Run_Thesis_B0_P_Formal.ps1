param(
    [ValidateSet(
        "Prepare",
        "Asset",
        "Smoke",
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

# ----------------------------------------------------------------------
# Formal child-process text encoding contract.
#
# Windows PowerShell 5.1, Python and localized Windows utilities such as
# powercfg.exe can otherwise disagree about the inherited console code page.
# The thesis runner uses UTF-8 end-to-end so Python's text-mode subprocess
# readers decode exactly the bytes emitted by native child processes.  This is
# an execution-environment fix only: it does not change B0/P physics, workload,
# thresholds, run counts, or evidence semantics.
# ----------------------------------------------------------------------
$FormalUtf8NoBom = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = $FormalUtf8NoBom
$OutputEncoding = $FormalUtf8NoBom
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8:replace"
$env:HAIRGPU_FORMAL_TEXT_ENCODING = "utf-8"

$ChcpPath = Join-Path $env:SystemRoot "System32\chcp.com"
if (Test-Path -LiteralPath $ChcpPath -PathType Leaf) {
    & $ChcpPath 65001 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Could not activate UTF-8 console code page 65001 for formal child-process I/O."
    }
}

$Repo = [IO.Path]::GetFullPath($Repo)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$SessionsRoot = Join-Path $OutputRoot "sessions"
$CurrentSessionPointer = Join-Path $OutputRoot "CURRENT_SESSION.txt"
$ExpectedRemotePattern = "MKyosei2/HairSystem"
$ReducedSamplingPolicy = "paired-thesis-100-v1"
$ExpectedReducedSeeds = "1"
$ExpectedReducedRepetitions = 1

# ----------------------------------------------------------------------
# Host guard for the real ExitCode=92 failure observed during unattended
# Quality execution.  The Viewer keeps its fail-closed resolution check;
# this runner prevents ordinary Windows idle/display power management from
# causing the resize in the first place and verifies 1920x1080 before every
# measurement matrix.
# ----------------------------------------------------------------------

if (-not ("HairGpuFormalExecutionState" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class HairGpuFormalExecutionState
{
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern uint SetThreadExecutionState(uint esFlags);
}
"@
}

function Get-ActivePowerSchemeIdentity {
    $raw = (& powercfg.exe /GETACTIVESCHEME | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Could not query the active Windows power scheme."
    }

    $match = [regex]::Match(
        $raw,
        '(?i)\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b'
    )
    if (-not $match.Success) {
        throw "Could not parse the active Windows power-scheme GUID. Raw=$raw"
    }

    return [PSCustomObject]@{
        Guid = $match.Value.ToLowerInvariant()
        Raw = $raw
    }
}

function Assert-FormalHighPerformancePowerScheme {
    $identity = Get-ActivePowerSchemeIdentity
    $highPerformanceGuid = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"

    if ($identity.Guid -ne $highPerformanceGuid) {
        throw "Formal High Performance power scheme is not active. ExpectedGuid=$highPerformanceGuid ObservedGuid=$($identity.Guid) Raw=$($identity.Raw)"
    }

    return $identity
}

function Enter-FormalHostGuard {
    [uint32]$ES_CONTINUOUS = 2147483648
    [uint32]$ES_SYSTEM_REQUIRED = 0x00000001
    [uint32]$ES_DISPLAY_REQUIRED = 0x00000002
    [uint32]$flags = $ES_CONTINUOUS -bor $ES_SYSTEM_REQUIRED -bor $ES_DISPLAY_REQUIRED

    $result = [HairGpuFormalExecutionState]::SetThreadExecutionState($flags)
    if ($result -eq 0) {
        throw "SetThreadExecutionState failed. Formal display/sleep guard is unavailable."
    }

    # Keep the active power scheme in high-performance mode.  This does not
    # weaken the Viewer resolution gate; it only avoids idle power transitions.
    & powercfg.exe /S SCHEME_MIN | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Could not activate the High Performance power scheme."
    }

    # Validate by GUID, not by the localized display name.  The Python runtime
    # still records the localized name as evidence, but pass/fail of this host
    # guard is language-independent.
    $activeScheme = Assert-FormalHighPerformancePowerScheme
    Write-Host "Formal power scheme GUID: $($activeScheme.Guid)"
}

function Exit-FormalHostGuard {
    [uint32]$ES_CONTINUOUS = 2147483648
    [void][HairGpuFormalExecutionState]::SetThreadExecutionState($ES_CONTINUOUS)
}

function Get-PrimaryDisplayResolution {
    Add-Type -AssemblyName System.Windows.Forms
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    return [PSCustomObject]@{
        Width = [int]$bounds.Width
        Height = [int]$bounds.Height
        Text = "{0}x{1}" -f $bounds.Width, $bounds.Height
    }
}

function Assert-FormalDisplayResolution {
    param([Parameter(Mandatory = $true)]$Session)

    $display = Get-PrimaryDisplayResolution
    if ($display.Width -ne 1920 -or $display.Height -ne 1080) {
        $diagnostic = [ordered]@{
            schema = "hairgpu-formal-display-guard-v1"
            status = "FAIL"
            expected = "1920x1080"
            observed = $display.Text
            stage = $Stage
            capturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
            reason = "Primary display resolution changed before formal matrix launch."
        }
        Write-JsonFile -Value $diagnostic -Path (
            Join-Path $Session.Directory "environment\display_guard_last.json"
        )
        throw "Formal primary display resolution must be 1920x1080. Observed=$($display.Text)"
    }

    $diagnostic = [ordered]@{
        schema = "hairgpu-formal-display-guard-v1"
        status = "PASS"
        expected = "1920x1080"
        observed = $display.Text
        stage = $Stage
        systemSleepGuard = $true
        displaySleepGuard = $true
        powerScheme = (& powercfg.exe /getactivescheme | Out-String).Trim()
        capturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    }
    Write-JsonFile -Value $diagnostic -Path (
        Join-Path $Session.Directory "environment\display_guard_last.json"
    )
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label,
        [string]$WorkingDirectory = ""
    )

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "[RUN] $Label"
    Write-Host "============================================================"

    $previousLocation = Get-Location
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $exitCode = 999
    try {
        if ($WorkingDirectory) {
            Set-Location -LiteralPath $WorkingDirectory
        }
        & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        $exitCode = $LASTEXITCODE
    }
    finally {
        Set-Location -LiteralPath $previousLocation
        $ErrorActionPreference = $oldPreference
    }
    if ($exitCode -ne 0) {
        throw "$Label failed. ExitCode=$exitCode"
    }
}

function Get-PythonCommand {
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($null -ne $py) {
        return [PSCustomObject]@{ FilePath = $py.Source; Prefix = @("-3") }
    }
    $python = Get-Command python.exe -ErrorAction Stop
    return [PSCustomObject]@{ FilePath = $python.Source; Prefix = @() }
}

function Get-MSBuildPath {
    $direct = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($null -ne $direct) {
        return $direct.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "vswhere.exe was not found: $vswhere"
    }
    $found = @(
        & $vswhere `
            -latest `
            -products "*" `
            -requires Microsoft.Component.MSBuild `
            -find "MSBuild\**\Bin\MSBuild.exe"
    )
    if ($LASTEXITCODE -ne 0 -or $found.Count -eq 0) {
        throw "MSBuild.exe was not found."
    }
    return ([string]$found[0]).Trim()
}

function Get-SignToolPath {
    $direct = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($null -ne $direct) {
        return $direct.Source
    }

    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "$env:ProgramFiles\Windows Kits\10\bin"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

    foreach ($root in $roots) {
        $candidate = Get-ChildItem `
            -LiteralPath $root `
            -Recurse `
            -Filter signtool.exe `
            -File `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1

        if ($null -ne $candidate) {
            return $candidate.FullName
        }
    }

    throw "signtool.exe was not found. Install the Windows SDK / Visual Studio signing tools."
}

function Get-CertificateEkuOidValues {
    param(
        [Parameter(Mandatory = $true)]
        $Certificate
    )

    $values = @()

    foreach ($eku in @($Certificate.EnhancedKeyUsageList)) {
        if ($null -eq $eku) {
            continue
        }

        # Windows PowerShell 5.1 normally exposes EnhancedKeyUsageList items
        # as System.Security.Cryptography.Oid objects with a direct Value
        # property.  Other PowerShell/.NET combinations can expose a wrapper
        # whose ObjectId property contains the Oid.  Resolve both shapes
        # without touching a missing property under Set-StrictMode.
        $directValueProperty = $eku.PSObject.Properties["Value"]
        if (
            $null -ne $directValueProperty -and
            -not [string]::IsNullOrWhiteSpace([string]$directValueProperty.Value)
        ) {
            $values += [string]$directValueProperty.Value
            continue
        }

        $objectIdProperty = $eku.PSObject.Properties["ObjectId"]
        if ($null -ne $objectIdProperty -and $null -ne $objectIdProperty.Value) {
            $objectId = $objectIdProperty.Value

            # Windows PowerShell 5.1 can expose
            # EnhancedKeyUsageRepresentation.ObjectId as the OID string itself.
            if (
                $objectId -is [string] -and
                -not [string]::IsNullOrWhiteSpace([string]$objectId)
            ) {
                $values += [string]$objectId
                continue
            }

            $nestedValueProperty = $objectId.PSObject.Properties["Value"]
            if (
                $null -ne $nestedValueProperty -and
                -not [string]::IsNullOrWhiteSpace([string]$nestedValueProperty.Value)
            ) {
                $values += [string]$nestedValueProperty.Value
                continue
            }
        }

        throw "Unsupported EnhancedKeyUsageList item type: $($eku.GetType().FullName)"
    }

    return @($values)
}

function Get-FormalSigningCertificate {
    $store = "Cert:\$SigningStoreScope\My"
    $cert = Get-ChildItem -Path $store -ErrorAction Stop |
        Where-Object {
            (
                $_.Subject -like "*$SigningSubjectLike*" -or
                $_.FriendlyName -like "*$SigningSubjectLike*"
            ) -and
            $_.HasPrivateKey -and
            (Get-Date) -ge $_.NotBefore -and
            (Get-Date) -le $_.NotAfter
        } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1

    if ($null -eq $cert) {
        throw @"
Formal signing certificate was not found in $store.

Required subject/friendly-name:
$SigningSubjectLike

Create/install the existing HairSystem internal test signing certificate before starting a formal session.
Do not disable Windows App Control / Code Integrity.
"@
    }

    $ekuOids = @(Get-CertificateEkuOidValues -Certificate $cert)
    if ($ekuOids -notcontains "1.3.6.1.5.5.7.3.3") {
        throw "Formal signing certificate does not contain the Code Signing EKU. Thumbprint=$($cert.Thumbprint)"
    }

    return $cert
}

function Assert-FormalSigningReady {
    $signScript = Join-Path $Repo "tools\signing\Sign-HairSystemBuild.ps1"
    if (-not (Test-Path -LiteralPath $signScript -PathType Leaf)) {
        throw "Formal signing script is missing: $signScript"
    }

    $signtool = Get-SignToolPath
    $cert = Get-FormalSigningCertificate

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "FORMAL CODE SIGNING PREFLIGHT"
    Write-Host "============================================================"
    Write-Host "SignTool   : $signtool"
    Write-Host "Certificate: $($cert.Subject)"
    Write-Host "Thumbprint : $($cert.Thumbprint)"
    Write-Host "Store      : $SigningStoreScope\My"
    Write-Host "Valid until: $($cert.NotAfter.ToString('o'))"
    Write-Host "Status     : READY"
    Write-Host "============================================================"

    return [PSCustomObject]@{
        SignTool = $signtool
        Certificate = $cert
        SignScript = $signScript
    }
}

function Get-NativeErrorCode {
    param([Parameter(Mandatory = $true)]$Exception)

    $cursor = $Exception
    while ($null -ne $cursor) {
        if ($cursor.PSObject.Properties.Name -contains "NativeErrorCode") {
            try {
                return [int]$cursor.NativeErrorCode
            }
            catch {
            }
        }
        $cursor = $cursor.InnerException
    }
    return $null
}

function Invoke-ViewerExecutionPolicyProbe {
    param(
        [Parameter(Mandatory = $true)][string]$Viewer,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)]$Session,
        [ValidateRange(1, 600)]
        [int]$TimeoutSeconds = 180
    )

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "[RUN] $Label"
    Write-Host "============================================================"

    try {
        $process = Start-Process `
            -FilePath $Viewer `
            -ArgumentList $Arguments `
            -WorkingDirectory (Split-Path -Parent $Viewer) `
            -PassThru `
            -ErrorAction Stop

        $exited = $process.WaitForExit($TimeoutSeconds * 1000)
        if (-not $exited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            throw "$Label timed out after $TimeoutSeconds seconds."
        }

        $process.Refresh()
        if ($process.ExitCode -ne 0) {
            throw "$Label failed. ExitCode=$($process.ExitCode)"
        }

        Write-Host "[PASS] $Label"
    }
    catch {
        $nativeCode = Get-NativeErrorCode -Exception $_.Exception
        $failure = [ordered]@{
            schema = "hairgpu-formal-execution-policy-failure-v1"
            status = "FAIL"
            label = $Label
            viewer = $Viewer
            arguments = $Arguments
            nativeErrorCode = $nativeCode
            exceptionType = $_.Exception.GetType().FullName
            message = $_.Exception.Message
            capturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
        }
        Write-JsonFile `
            -Value $failure `
            -Path (Join-Path $Session.Directory "integrity\formal_signing_failure.json")

        if ($nativeCode -eq 4551 -or $_.Exception.Message -match "4551") {
            throw @"
Windows App Control / Code Integrity still blocked the signed HairSystem.exe (WinError 4551).

The formal runner did NOT disable or bypass the policy.
The configured signing certificate must be permitted by the machine's App Control policy.
Evidence:
$(Join-Path $Session.Directory "integrity\formal_signing_failure.json")
"@
        }

        throw
    }
}

function Assert-ViewerSignatureInvariant {
    param(
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][string]$Viewer
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $Viewer
    if ($signature.Status -ne "Valid") {
        throw "Viewer Authenticode signature is no longer Valid. Status=$($signature.Status)"
    }
    if ($null -eq $signature.SignerCertificate) {
        throw "Viewer signature has no signer certificate."
    }

    $expectedThumbprint = ([string]$Session.Value.viewerSignatureThumbprint).ToUpperInvariant()
    $observedThumbprint = ([string]$signature.SignerCertificate.Thumbprint).ToUpperInvariant()
    if ($expectedThumbprint -ne $observedThumbprint) {
        throw "Viewer signer changed after Asset freeze. Expected=$expectedThumbprint Observed=$observedThumbprint"
    }

    $evidencePath = Join-Path $Session.Directory "integrity\formal_signing.json"
    if (-not (Test-Path -LiteralPath $evidencePath -PathType Leaf)) {
        throw "Formal signing evidence is missing: $evidencePath"
    }
    $evidenceHash = (
        Get-FileHash -LiteralPath $evidencePath -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    if ($evidenceHash -ne ([string]$Session.Value.signingEvidenceSha256).ToLowerInvariant()) {
        throw "Formal signing evidence changed after it was frozen."
    }
}

function Read-JsonFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "JSON file is missing: $Path"
    }
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $temporary = "$Path.tmp.$PID"
    $Value | ConvertTo-Json -Depth 30 |
        Set-Content -LiteralPath $temporary -Encoding UTF8
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

function Get-RepositoryIdentity {
    if (-not (Test-Path -LiteralPath $Repo -PathType Container)) {
        throw "Repository was not found: $Repo"
    }
    $inside = (& git.exe -C $Repo rev-parse --is-inside-work-tree | Out-String).Trim()
    if ($inside -ne "true") {
        throw "Not a Git worktree: $Repo"
    }
    $branch = (& git.exe -C $Repo branch --show-current | Out-String).Trim()
    $head = (& git.exe -C $Repo rev-parse HEAD | Out-String).Trim().ToLowerInvariant()
    $tree = (& git.exe -C $Repo rev-parse "HEAD^{tree}" | Out-String).Trim().ToLowerInvariant()
    $origin = (& git.exe -C $Repo remote get-url origin | Out-String).Trim()
    if ($branch -ne "main") {
        throw "Formal experiment requires the existing main branch. Current=$branch"
    }
    if ($origin -notmatch $ExpectedRemotePattern) {
        throw "Unexpected Git remote: $origin"
    }
    if ($head -notmatch "^[0-9a-f]{40}$") {
        throw "Could not resolve a valid 40-character HEAD SHA."
    }
    return [PSCustomObject]@{
        Branch = $branch
        Head = $head
        Tree = $tree
        Origin = $origin
    }
}

function Assert-CleanWorktree {
    $dirty = (& git.exe -C $Repo status --porcelain --untracked-files=all | Out-String).Trim()
    if ($dirty) {
        Write-Host $dirty
        throw "Formal execution requires a clean worktree. Commit the fixes on main first."
    }
}

function Save-LocalBranchInventory {
    param([Parameter(Mandatory = $true)][string]$Path)
    $rows = @(
        & git.exe -C $Repo for-each-ref `
            "--format=%(refname)`t%(objectname)" `
            refs/heads
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read the local branch inventory."
    }
    $rows | Sort-Object | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-DirectoryDigest {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Directory is missing: $Path"
    }
    $rootPath = [IO.Path]::GetFullPath($Path)
    $records = foreach (
        $file in Get-ChildItem -LiteralPath $rootPath -File -Recurse | Sort-Object FullName
    ) {
        $relative = $file.FullName.Substring($rootPath.Length).TrimStart('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$relative`t$($file.Length)`t$hash"
    }
    $temporary = Join-Path $env:TEMP "hairgpu_tree_$PID.txt"
    try {
        $records | Set-Content -LiteralPath $temporary -Encoding UTF8
        return (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

function Resolve-SessionDirectory {
    if ($SessionId) {
        return Join-Path $SessionsRoot $SessionId
    }
    if (-not (Test-Path -LiteralPath $CurrentSessionPointer -PathType Leaf)) {
        throw "No current session is recorded. Run Stage=Prepare first or pass -SessionId."
    }
    $resolvedId = (Get-Content -LiteralPath $CurrentSessionPointer -Raw).Trim()
    if (-not $resolvedId) {
        throw "CURRENT_SESSION.txt is empty."
    }
    return Join-Path $SessionsRoot $resolvedId
}

function Load-Session {
    $directory = Resolve-SessionDirectory
    $manifest = Join-Path $directory "formal_session.json"
    $value = Read-JsonFile -Path $manifest
    return [PSCustomObject]@{
        Directory = $directory
        ManifestPath = $manifest
        Value = $value
    }
}

function Save-Session {
    param([Parameter(Mandatory = $true)]$Session)
    Set-ObjectProperty -Object $Session.Value -Name "lastUpdatedAt" -Value (
        Get-Date
    ).ToUniversalTime().ToString("o")
    Write-JsonFile -Value $Session.Value -Path $Session.ManifestPath
}

function Add-CompletedStage {
    param(
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $stages = @()
    if ($Session.Value.PSObject.Properties.Name -contains "completedStages") {
        $stages = @($Session.Value.completedStages)
    }
    if ($stages -notcontains $Name) {
        $stages += $Name
    }
    Set-ObjectProperty -Object $Session.Value -Name "completedStages" -Value $stages
    Save-Session -Session $Session
}

function Assert-CompletedStages {
    param(
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][string[]]$Required
    )
    $completed = @($Session.Value.completedStages)
    $missing = @($Required | Where-Object { $completed -notcontains $_ })
    if ($missing.Count -gt 0) {
        throw "Required earlier stages are missing: $($missing -join ', ')"
    }
}

function Invoke-StaticContractChecks {
    $python = Get-PythonCommand
    $compileTargets = @(
        "tools/final_stage/freeze_novelty01_asset.py",
        "tools/final_stage/hairgpu_novelty01_matrix.py",
        "tools/final_stage/hairgpu_thesis_report.py",
        "tools/final_stage/hairgpu_thesis_final_gate.py",
        "tests/final_stage/test_thesis_formal_experiment_contract.py",
        "tests/final_stage/test_novelty01_performance_readback_stability.py"
    )
    foreach ($relative in $compileTargets) {
        $path = Join-Path $Repo $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Formal source file is missing: $path"
        }
        Invoke-NativeChecked `
            -FilePath $python.FilePath `
            -Arguments (@($python.Prefix) + @("-m", "py_compile", $path)) `
            -Label "Python syntax: $relative"
    }

    Invoke-NativeChecked `
        -FilePath $python.FilePath `
        -Arguments (@($python.Prefix) + @(
            "-m", "unittest", "discover",
            "-s", (Join-Path $Repo "tests\final_stage"),
            "-p", "test_thesis_formal_experiment_contract.py",
            "-v"
        )) `
        -Label "Thesis formal source contract tests"

    Invoke-NativeChecked `
        -FilePath $python.FilePath `
        -Arguments (@($python.Prefix) + @(
            "-m", "unittest", "discover",
            "-s", (Join-Path $Repo "tests\final_stage"),
            "-p", "test_novelty01_performance_readback_stability.py",
            "-v"
        )) `
        -Label "P/Performance query-gated readback regression tests"

    $preflight = Join-Path $Repo "tools\final_stage\hairgpu_novelty01_preflight.py"
    $preflightJson = Join-Path $env:TEMP "hairgpu_n01_prepare_preflight_$PID.json"
    try {
        Invoke-NativeChecked `
            -FilePath $python.FilePath `
            -Arguments (@($python.Prefix) + @(
                $preflight,
                "--repo", $Repo,
                "--json", $preflightJson
            )) `
            -Label "Novelty01 static source preflight"
    }
    finally {
        Remove-Item -LiteralPath $preflightJson -Force -ErrorAction SilentlyContinue
    }

    Invoke-NativeChecked `
        -FilePath "git.exe" `
        -Arguments @("-C", $Repo, "diff", "--check") `
        -Label "Git whitespace and conflict-marker check"
}

function Assert-SessionInvariant {
    param([Parameter(Mandatory = $true)]$Session)
    $identity = Get-RepositoryIdentity
    Assert-CleanWorktree
    if ($identity.Head -ne ([string]$Session.Value.formalCommit).ToLowerInvariant()) {
        throw "HEAD changed after Prepare. Expected=$($Session.Value.formalCommit) Observed=$($identity.Head)"
    }
    if ($identity.Tree -ne ([string]$Session.Value.sourceTreeSha).ToLowerInvariant()) {
        throw "Git tree changed after Prepare. Expected=$($Session.Value.sourceTreeSha) Observed=$($identity.Tree)"
    }
    if ($identity.Branch -ne "main") {
        throw "Formal session left main. Observed=$($identity.Branch)"
    }
    return $identity
}

function Assert-NewOutputDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (Test-Path -LiteralPath $Path) {
        throw "Stage output already exists. Refusing to mix or overwrite evidence: $Path"
    }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Capture-Hardware {
    param([Parameter(Mandatory = $true)][string]$EnvironmentDirectory)

    $cpu = @(
        Get-CimInstance Win32_Processor |
            Select-Object Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed
    )
    $gpu = @(
        Get-CimInstance Win32_VideoController |
            Select-Object Name, DriverVersion, AdapterRAM, VideoProcessor, PNPDeviceID
    )
    $computer = Get-CimInstance Win32_ComputerSystem |
        Select-Object Manufacturer, Model, TotalPhysicalMemory
    $os = Get-CimInstance Win32_OperatingSystem |
        Select-Object Caption, Version, BuildNumber, OSArchitecture, TotalVisibleMemorySize
    $powerPlan = (& powercfg.exe /getactivescheme | Out-String).Trim()
    $python = Get-PythonCommand
    $pythonVersion = (
        & $python.FilePath @($python.Prefix + @("--version")) 2>&1 | Out-String
    ).Trim()
    $msbuild = Get-MSBuildPath
    $msbuildVersion = (& $msbuild -version -nologo | Out-String).Trim()
    $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='C:'" |
        Select-Object DeviceID, Size, FreeSpace, FileSystem

    $visualStudioVersion = ""
    $visualStudioPath = ""
    $msvcVersion = ""
    $windowsSdkVersion = ""
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $visualStudioVersion = (
            & $vswhere -latest -products "*" -property catalog_productDisplayVersion |
            Out-String
        ).Trim()
        $visualStudioPath = (
            & $vswhere -latest -products "*" -property installationPath |
            Out-String
        ).Trim()
        $msvcRoot = Join-Path $visualStudioPath "VC\Tools\MSVC"
        if (Test-Path -LiteralPath $msvcRoot -PathType Container) {
            $latestMsvc = Get-ChildItem -LiteralPath $msvcRoot -Directory |
                Sort-Object Name -Descending |
                Select-Object -First 1
            if ($null -ne $latestMsvc) {
                $msvcVersion = $latestMsvc.Name
            }
        }
    }

    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include"
    if (Test-Path -LiteralPath $sdkRoot -PathType Container) {
        $latestSdk = Get-ChildItem -LiteralPath $sdkRoot -Directory |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if ($null -ne $latestSdk) {
            $windowsSdkVersion = $latestSdk.Name
        }
    }

    $primaryCpu = @($cpu)[0]
    $primaryGpu = @($gpu)[0]
    $ramMiB = if ($computer.TotalPhysicalMemory) {
        [math]::Round([double]$computer.TotalPhysicalMemory / 1MB)
    }
    else {
        $null
    }

    $nvidiaSmiGpuMemory = @()
    $nvidiaSmi = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
    if ($null -ne $nvidiaSmi) {
        $nvidiaLines = @(
            & $nvidiaSmi.Source `
                "--query-gpu=name,memory.total" `
                "--format=csv,noheader,nounits" `
                2>$null
        )
        foreach ($line in $nvidiaLines) {
            $parts = @([string]$line -split ",")
            if ($parts.Count -lt 2) {
                continue
            }
            $memoryTotalMiB = 0
            if (-not [int]::TryParse($parts[1].Trim(), [ref]$memoryTotalMiB)) {
                continue
            }
            $nvidiaSmiGpuMemory += [PSCustomObject]@{
                Name = $parts[0].Trim()
                MemoryTotalMiB = $memoryTotalMiB
            }
        }
    }

    $matchedNvidiaGpu = $nvidiaSmiGpuMemory |
        Where-Object { $_.Name -eq [string]$primaryGpu.Name } |
        Select-Object -First 1

    $gpuVramMiB = $null
    $gpuVramSource = "unavailable"
    if (
        $null -ne $matchedNvidiaGpu -and
        [int]$matchedNvidiaGpu.MemoryTotalMiB -gt 0
    ) {
        $gpuVramMiB = [int]$matchedNvidiaGpu.MemoryTotalMiB
        $gpuVramSource = "nvidia-smi"
    }
    elseif ($primaryGpu.AdapterRAM) {
        $gpuVramMiB = [math]::Round([double]$primaryGpu.AdapterRAM / 1MB)
        $gpuVramSource = "Win32_VideoController.AdapterRAM"
    }

    $observed = [ordered]@{
        schema = "hairgpu-observed-hardware-v1"
        capturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
        cpu = $cpu
        gpu = $gpu
        nvidiaSmiGpuMemory = $nvidiaSmiGpuMemory
        resolvedGpuVramMiB = $gpuVramMiB
        gpuVramSource = $gpuVramSource
        computer = $computer
        operatingSystem = $os
        powerPlan = $powerPlan
        pythonVersion = $pythonVersion
        msbuildPath = $msbuild
        msbuildVersion = $msbuildVersion
        visualStudioVersion = $visualStudioVersion
        visualStudioPath = $visualStudioPath
        msvcVersion = $msvcVersion
        windowsSdkVersion = $windowsSdkVersion
        systemDrive = $disk
        displayResolutionRequested = "1920x1080"
    }
    Write-JsonFile -Value $observed -Path (
        Join-Path $EnvironmentDirectory "observed_hardware.json"
    )

    $profilePath = Join-Path $EnvironmentDirectory "hardware_profile.json"
    if (-not (Test-Path -LiteralPath $profilePath -PathType Leaf)) {
        $profile = [ordered]@{
            schema = "hairgpu-formal-hardware-profile-v2"
            status = "PENDING_USER_REVIEW"
            cpu = [string]$primaryCpu.Name
            gpu = [string]$primaryGpu.Name
            gpuVramMiB = $gpuVramMiB
            gpuVramSource = $gpuVramSource
            gpuDriver = [string]$primaryGpu.DriverVersion
            ramMiB = $ramMiB
            windowsVersion = "{0} {1}" -f $os.Caption, $os.Version
            windowsBuild = [string]$os.BuildNumber
            visualStudioVersion = $visualStudioVersion
            msvcVersion = $msvcVersion
            windowsSdkVersion = $windowsSdkVersion
            pythonVersion = $pythonVersion
            powerPlan = $powerPlan
            displayResolution = "1920x1080"
            acPowerRequired = $true
            notes = "Review every value against observed_hardware.json, correct it, then set status to READY."
        }
        Write-JsonFile -Value $profile -Path $profilePath
    }
}

function Assert-HardwareProfileReady {
    param([Parameter(Mandatory = $true)]$Session)

    $profilePath = Join-Path $Session.Directory "environment\hardware_profile.json"
    $observedPath = Join-Path $Session.Directory "environment\observed_hardware.json"
    $profile = Read-JsonFile -Path $profilePath
    $observed = Read-JsonFile -Path $observedPath

    if ($profile.status -ne "READY") {
        throw "Review hardware_profile.json, complete every required field, then set status to READY: $profilePath"
    }

    $requiredTextFields = @(
        "cpu", "gpu", "gpuDriver", "windowsVersion", "windowsBuild",
        "visualStudioVersion", "msvcVersion", "windowsSdkVersion",
        "pythonVersion", "powerPlan"
    )
    foreach ($field in $requiredTextFields) {
        if (
            -not ($profile.PSObject.Properties.Name -contains $field) -or
            -not ([string]$profile.$field).Trim()
        ) {
            throw "Hardware profile required field is empty: $field"
        }
    }

    foreach ($field in @("gpuVramMiB", "ramMiB")) {
        if (
            -not ($profile.PSObject.Properties.Name -contains $field) -or
            [double]$profile.$field -le 0.0
        ) {
            throw "Hardware profile numeric field must be positive: $field"
        }
    }

    $observedCpu = (@($observed.cpu | ForEach-Object { $_.Name }) -join " | ")
    $observedGpu = (@($observed.gpu | ForEach-Object { $_.Name }) -join " | ")

    if ($profile.cpu -and $observedCpu -notlike "*$($profile.cpu)*") {
        throw "CPU profile mismatch. Profile=$($profile.cpu) Observed=$observedCpu"
    }
    if ($profile.gpu -and $observedGpu -notlike "*$($profile.gpu)*") {
        throw "GPU profile mismatch. Profile=$($profile.gpu) Observed=$observedGpu"
    }

    $observedGpuDrivers = (
        @($observed.gpu | ForEach-Object { $_.DriverVersion }) -join " | "
    )
    if ($observedGpuDrivers -notlike "*$($profile.gpuDriver)*") {
        throw "GPU driver profile mismatch. Profile=$($profile.gpuDriver) Observed=$observedGpuDrivers"
    }
    if ($profile.displayResolution -ne "1920x1080") {
        throw "Formal displayResolution must be 1920x1080."
    }
    if ($profile.acPowerRequired -ne $true) {
        throw "acPowerRequired must remain true for formal evidence."
    }

    $profileHash = (
        Get-FileHash -LiteralPath $profilePath -Algorithm SHA256
    ).Hash.ToLowerInvariant()

    $recordedHash = ""
    if ($Session.Value.PSObject.Properties.Name -contains "hardwareProfileSha256") {
        $recordedHash = ([string]$Session.Value.hardwareProfileSha256).ToLowerInvariant()
    }

    if ($recordedHash -and $recordedHash -ne $profileHash) {
        throw "Hardware profile changed after it was frozen. Create a new formal session."
    }
    if (-not $recordedHash) {
        Set-ObjectProperty `
            -Object $Session.Value `
            -Name "hardwareProfileSha256" `
            -Value $profileHash
        Save-Session -Session $Session
    }
}

function Build-FormalRelease {
    param([Parameter(Mandatory = $true)]$Session)

    $signing = Assert-FormalSigningReady
    $msbuild = Get-MSBuildPath
    $solution = Join-Path $Repo "HairSystem.sln"
    $log = Join-Path $Session.Directory "logs\formal_release_build.log"
    $buildRoot = Join-Path $Repo "bin\Release\HairSystem"
    $viewer = Join-Path $buildRoot "HairSystem.exe"

    Get-Process HairSystem -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    Remove-Item `
        -LiteralPath $buildRoot `
        -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item `
        -LiteralPath (Join-Path $Repo "bin-int\Release\HairSystem") `
        -Recurse -Force -ErrorAction SilentlyContinue

    Invoke-NativeChecked `
        -FilePath $msbuild `
        -Arguments @(
            $solution,
            "/m",
            "/t:Rebuild",
            "/nologo",
            "/verbosity:minimal",
            "/p:Configuration=Release",
            "/p:Platform=x64",
            "/p:HairGpuStep0ProfileEnabled=false",
            "/p:HairGpuFormalBuild=true",
            "/fl",
            "/flp:logfile=$log;verbosity=diagnostic"
        ) `
        -Label "Release x64 formal rebuild"

    if (-not (Test-Path -LiteralPath $viewer -PathType Leaf)) {
        throw "Formal viewer was not produced: $viewer"
    }

    # Remove Mark-of-the-Web if it somehow propagated into copied build inputs.
    # This does not bypass App Control; the executable still must be validly
    # signed and must pass the actual execution-policy probe below.
    Get-ChildItem `
        -LiteralPath $buildRoot `
        -Recurse `
        -File `
        -ErrorAction SilentlyContinue |
        ForEach-Object {
            Unblock-File -LiteralPath $_.FullName -ErrorAction SilentlyContinue
        }

    Invoke-NativeChecked `
        -FilePath "powershell.exe" `
        -Arguments @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", $signing.SignScript,
            "-BuildRoot", $buildRoot,
            "-Thumbprint", $signing.Certificate.Thumbprint,
            "-StoreScope", $SigningStoreScope
        ) `
        -Label "Authenticode sign formal Release output"

    $viewerSignature = Get-AuthenticodeSignature -LiteralPath $viewer
    if ($viewerSignature.Status -ne "Valid") {
        throw "Formal Viewer signature is not Valid after signing. Status=$($viewerSignature.Status) Message=$($viewerSignature.StatusMessage)"
    }
    if ($null -eq $viewerSignature.SignerCertificate) {
        throw "Formal Viewer signature has no signer certificate."
    }

    $expectedThumbprint = (
        [string]$signing.Certificate.Thumbprint
    ).ToUpperInvariant()
    $observedThumbprint = (
        [string]$viewerSignature.SignerCertificate.Thumbprint
    ).ToUpperInvariant()
    if ($expectedThumbprint -ne $observedThumbprint) {
        throw "Formal Viewer was signed by an unexpected certificate. Expected=$expectedThumbprint Observed=$observedThumbprint"
    }

    # These are real CreateProcess probes. A valid Authenticode signature alone
    # is not sufficient: the machine's App Control policy must actually permit
    # HairSystem.exe to start.
    Invoke-ViewerExecutionPolicyProbe `
        -Viewer $viewer `
        -Arguments @("--native-self-test") `
        -Label "signed Viewer native self-test / App Control probe" `
        -Session $Session

    Invoke-ViewerExecutionPolicyProbe `
        -Viewer $viewer `
        -Arguments @("--shader-self-test") `
        -Label "signed Viewer shader self-test / App Control probe" `
        -Session $Session

    # Hash only AFTER the final signed bytes and policy probes are complete.
    $viewerHash = (
        Get-FileHash -LiteralPath $viewer -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    $buildLogHash = (
        Get-FileHash -LiteralPath $log -Algorithm SHA256
    ).Hash.ToLowerInvariant()

    $signingEvidence = [ordered]@{
        schema = "hairgpu-formal-signing-evidence-v1"
        status = "PASS"
        signerSubject = [string]$viewerSignature.SignerCertificate.Subject
        signerThumbprint = $observedThumbprint
        signingStoreScope = $SigningStoreScope
        signatureStatus = [string]$viewerSignature.Status
        signatureStatusMessage = [string]$viewerSignature.StatusMessage
        viewerPath = $viewer
        viewerSha256 = $viewerHash
        nativeSelfTest = "PASS"
        shaderSelfTest = "PASS"
        applicationControlExecutionProbe = "PASS"
        hashRecordedAfterSigning = $true
        capturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    }
    $signingEvidencePath = Join-Path $Session.Directory "integrity\formal_signing.json"
    Write-JsonFile -Value $signingEvidence -Path $signingEvidencePath
    $signingEvidenceHash = (
        Get-FileHash -LiteralPath $signingEvidencePath -Algorithm SHA256
    ).Hash.ToLowerInvariant()

    Set-ObjectProperty -Object $Session.Value -Name "viewerPath" -Value $viewer
    Set-ObjectProperty -Object $Session.Value -Name "viewerSha256" -Value $viewerHash
    Set-ObjectProperty -Object $Session.Value -Name "buildLogSha256" -Value $buildLogHash
    Set-ObjectProperty -Object $Session.Value -Name "viewerSigningRequired" -Value $true
    Set-ObjectProperty -Object $Session.Value -Name "viewerSignatureStatus" -Value "Valid"
    Set-ObjectProperty -Object $Session.Value -Name "viewerSignatureThumbprint" -Value $observedThumbprint
    Set-ObjectProperty -Object $Session.Value -Name "viewerExecutionPolicyProbe" -Value "PASS"
    Set-ObjectProperty -Object $Session.Value -Name "signingEvidenceSha256" -Value $signingEvidenceHash
    Save-Session -Session $Session
}

function Assert-FrozenInputs {
    param([Parameter(Mandatory = $true)]$Session)

    $viewer = Join-Path $Repo "bin\Release\HairSystem\HairSystem.exe"
    $assetManifest = Join-Path $Session.Directory "asset\frozen_asset_manifest.json"

    if (-not (Test-Path -LiteralPath $viewer -PathType Leaf)) {
        throw "Viewer is missing. Run Stage=Asset first."
    }
    if (-not (Test-Path -LiteralPath $assetManifest -PathType Leaf)) {
        throw "Frozen asset manifest is missing. Run Stage=Asset first."
    }

    Assert-ViewerSignatureInvariant -Session $Session -Viewer $viewer

    $viewerHash = (
        Get-FileHash -LiteralPath $viewer -Algorithm SHA256
    ).Hash.ToLowerInvariant()
    $manifestHash = (
        Get-FileHash -LiteralPath $assetManifest -Algorithm SHA256
    ).Hash.ToLowerInvariant()

    if ($viewerHash -ne ([string]$Session.Value.viewerSha256).ToLowerInvariant()) {
        throw "Viewer SHA-256 changed after Asset freeze."
    }
    if (
        $manifestHash -ne
        ([string]$Session.Value.assetManifestSha256).ToLowerInvariant()
    ) {
        throw "Frozen asset manifest SHA-256 changed."
    }

    $scenarioRoot = Join-Path $Repo "benchmarks\novelty01\sgapt_scenarios"
    $scenarioHash = Get-DirectoryDigest -Path $scenarioRoot
    if (
        $scenarioHash -ne
        ([string]$Session.Value.canonicalScenarioTreeSha256).ToLowerInvariant()
    ) {
        throw "Canonical scenario tree changed after Asset freeze."
    }

    $python = Get-PythonCommand
    $freeze = Join-Path $Repo "tools\final_stage\freeze_novelty01_asset.py"
    Invoke-NativeChecked `
        -FilePath $python.FilePath `
        -Arguments (@($python.Prefix) + @(
            $freeze,
            "--bundle-dir", (Join-Path $Session.Directory "asset"),
            "--viewer-exe", $viewer,
            "--verify-only"
        )) `
        -Label "Verify frozen asset bytes and manifest"
}


# ----------------------------------------------------------------------
# 70.24 qualification / crash-dump / downstream guards.
# Qualification artifacts live outside sessions\ so the thesis FinalGate never
# counts them as formal evidence. They are intentionally destructive-to-time but
# non-formal: their only purpose is to discover P/Performance, Determinism and
# long-run failures before the 100-run session spends hours collecting evidence.
# ----------------------------------------------------------------------
function Assert-FreeDiskSpace {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [double]$MinimumFreeGiB = 20.0
    )
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($full)
    if ([string]::IsNullOrWhiteSpace($root)) {
        throw "Could not resolve a drive root for disk-space preflight: $full"
    }
    $driveName = $root.TrimEnd('\').TrimEnd(':')
    $drive = Get-PSDrive -Name $driveName -ErrorAction Stop
    $freeGiB = [double]$drive.Free / 1GB
    Write-Host ("Disk free on {0}: {1:N2} GiB" -f $root, $freeGiB)
    if ($freeGiB -lt $MinimumFreeGiB) {
        throw ("Insufficient free disk space. Required>={0:N2} GiB Observed={1:N2} GiB Drive={2}" -f $MinimumFreeGiB, $freeGiB, $root)
    }
}

function Enable-HairSystemCrashDumpCapture {
    param([Parameter(Mandatory = $true)][string]$DumpFolder)

    if ($env:OS -ne "Windows_NT") {
        return $null
    }
    New-Item -ItemType Directory -Path $DumpFolder -Force | Out-Null
    $key = "HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\HairSystem.exe"
    $existed = Test-Path -LiteralPath $key
    $snapshot = [ordered]@{
        Key = $key
        Existed = $existed
        DumpFolderPresent = $false
        DumpFolder = $null
        DumpTypePresent = $false
        DumpType = $null
        DumpCountPresent = $false
        DumpCount = $null
    }
    if ($existed) {
        $old = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        foreach ($name in @("DumpFolder", "DumpType", "DumpCount")) {
            if ($null -ne $old -and $old.PSObject.Properties.Name -contains $name) {
                $snapshot[$name + "Present"] = $true
                $snapshot[$name] = $old.$name
            }
        }
    }
    New-Item -ItemType Directory -Path $key -Force | Out-Null
    New-ItemProperty -LiteralPath $key -Name "DumpFolder" -Value $DumpFolder -PropertyType ExpandString -Force | Out-Null
    New-ItemProperty -LiteralPath $key -Name "DumpType" -Value 2 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -LiteralPath $key -Name "DumpCount" -Value 1 -PropertyType DWord -Force | Out-Null
    Write-Host "WER full crash dump capture: $DumpFolder"
    return [PSCustomObject]$snapshot
}

function Restore-HairSystemCrashDumpCapture {
    param($Snapshot)
    if ($null -eq $Snapshot -or $env:OS -ne "Windows_NT") {
        return
    }
    $key = [string]$Snapshot.Key
    if (-not [bool]$Snapshot.Existed) {
        Remove-Item -LiteralPath $key -Recurse -Force -ErrorAction SilentlyContinue
        return
    }
    foreach ($name in @("DumpFolder", "DumpType", "DumpCount")) {
        $presentName = $name + "Present"
        if ([bool]$Snapshot.$presentName) {
            $type = if ($name -eq "DumpFolder") { "ExpandString" } else { "DWord" }
            New-ItemProperty -LiteralPath $key -Name $name -Value $Snapshot.$name -PropertyType $type -Force | Out-Null
        }
        else {
            Remove-ItemProperty -LiteralPath $key -Name $name -Force -ErrorAction SilentlyContinue
        }
    }
}

function Assert-MatrixSummaryContract {
    param(
        [Parameter(Mandatory = $true)][string]$Output,
        [Parameter(Mandatory = $true)][string]$Profile,
        [Parameter(Mandatory = $true)][int]$ExpectedRuns
    )
    $summaryPath = Join-Path $Output "novelty01_matrix_summary.json"
    if (-not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) {
        throw "Matrix summary is missing: $summaryPath"
    }
    $summary = Read-JsonFile -Path $summaryPath
    if ($summary.passed -ne $true) {
        throw "Matrix summary did not pass: $summaryPath"
    }
    if ([string]$summary.profile -ne $Profile) {
        throw "Matrix profile mismatch. Expected=$Profile Observed=$($summary.profile)"
    }
    if ([int]$summary.runCount -ne $ExpectedRuns) {
        throw "Matrix run count mismatch. Profile=$Profile Expected=$ExpectedRuns Observed=$($summary.runCount)"
    }
    $rows = @($summary.results)
    if ($rows.Count -ne $ExpectedRuns) {
        throw "Matrix results count mismatch. Profile=$Profile Expected=$ExpectedRuns Observed=$($rows.Count)"
    }
    $failed = @($rows | Where-Object { $_.ok -ne $true })
    if ($failed.Count -ne 0) {
        throw "Matrix contains failed rows. Profile=$Profile Failed=$($failed.Count)"
    }
    if (@($summary.scenarioInputParityIssues).Count -ne 0) {
        throw "Scenario-input parity issues were detected in $summaryPath"
    }
    if (@($summary.seedVariationIssues).Count -ne 0) {
        throw "Seed-variation issues were detected in $summaryPath"
    }
    if ($Profile -eq "determinism" -and @($summary.determinismIssues).Count -ne 0) {
        throw "Determinism mismatches were detected in $summaryPath"
    }
    $b0 = @($rows | Where-Object { [string]$_.variant -eq "B0" }).Count
    $proposal = @($rows | Where-Object { [string]$_.variant -eq "P" }).Count
    if ($b0 -ne ($ExpectedRuns / 2) -or $proposal -ne ($ExpectedRuns / 2)) {
        throw "B0/P balance mismatch. Profile=$Profile B0=$b0 P=$proposal ExpectedEach=$($ExpectedRuns / 2)"
    }
}

function Assert-FormalEvidenceReadyForReport {
    param([Parameter(Mandatory = $true)]$Session)

    $expected = [ordered]@{
        smoke = 10
        pilot = 10
        formal = 36
        performance = 36
        determinism = 4
        soak = 4
    }
    $summaries = @(
        Get-ChildItem -LiteralPath $Session.Directory -Recurse -File -Filter "novelty01_matrix_summary.json" -ErrorAction Stop
    )
    if ($summaries.Count -ne 6) {
        throw "Pre-Report gate requires exactly 6 formal matrix summaries. Observed=$($summaries.Count)"
    }
    $total = 0
    $b0Total = 0
    $pTotal = 0
    foreach ($file in $summaries) {
        $summary = Read-JsonFile -Path $file.FullName
        $profile = [string]$summary.profile
        if (-not $expected.Contains($profile)) {
            throw "Unexpected formal matrix profile before Report: $profile ($($file.FullName))"
        }
        $want = [int]$expected[$profile]
        if ($summary.passed -ne $true -or [int]$summary.runCount -ne $want) {
            throw "Formal matrix is incomplete before Report. Profile=$profile Expected=$want Observed=$($summary.runCount) Passed=$($summary.passed)"
        }
        $rows = @($summary.results)
        if ($rows.Count -ne $want -or @($rows | Where-Object { $_.ok -ne $true }).Count -ne 0) {
            throw "Formal matrix result rows are incomplete before Report. Profile=$profile"
        }
        if (@($summary.scenarioInputParityIssues).Count -ne 0 -or
            @($summary.seedVariationIssues).Count -ne 0 -or
            @($summary.determinismIssues).Count -ne 0) {
            throw "Formal matrix contains parity/determinism issues before Report. Profile=$profile"
        }
        $b0Total += @($rows | Where-Object { [string]$_.variant -eq "B0" }).Count
        $pTotal += @($rows | Where-Object { [string]$_.variant -eq "P" }).Count
        $total += $rows.Count
    }
    if ($total -ne 100 -or $b0Total -ne 50 -or $pTotal -ne 50) {
        throw "Pre-Report formal count mismatch. Total=$total B0=$b0Total P=$pTotal Expected=100/50/50"
    }
    Write-Host "Pre-Report evidence gate: PASS (100 total / 50 B0 / 50 P)" -ForegroundColor Green
}

function Invoke-Matrix {
    param(
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][string]$Profile,
        [Parameter(Mandatory = $true)][string]$Scenarios,
        [Parameter(Mandatory = $true)][string]$SeedList,
        [Parameter(Mandatory = $true)][int]$RepeatCount,
        [Parameter(Mandatory = $true)][string]$Output,
        [int]$ExpectedRunCount = 0,
        [switch]$DiagnosticCheckpoints
    )

    Assert-SessionInvariant -Session $Session | Out-Null
    Assert-FrozenInputs -Session $Session
    Enter-FormalHostGuard
    Assert-FormalDisplayResolution -Session $Session
    Assert-NewOutputDirectory -Path $Output

    $python = Get-PythonCommand
    $viewer = Join-Path $Repo "bin\Release\HairSystem\HairSystem.exe"
    $matrix = Join-Path $Repo "tools\final_stage\hairgpu_novelty01_matrix.py"
    $template = Join-Path $Repo "docs\sample_assets\final_runtime_suite_template.json"
    $timeline = Join-Path $Repo "benchmarks\novelty01\sgapt_scenarios"
    $manifest = Join-Path $Session.Directory "asset\frozen_asset_manifest.json"

    foreach ($required in @($viewer, $matrix, $template, $timeline, $manifest)) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "Required formal input is missing: $required"
        }
    }

    $env:HAIRGPU_FROZEN_ASSET_MANIFEST = $manifest
    $env:HAIRGPU_FORMAL_SESSION_ID = [string]$Session.Value.sessionId
    $env:HAIRGPU_FORMAL_COMMIT = [string]$Session.Value.formalCommit

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "THESIS METHOD LEGEND / CURRENT VIEWER TITLE"
    Write-Host "============================================================"
    Write-Host "B0 = PRE-NOVELTY BASELINE (新規性実装前相当)" -ForegroundColor Cyan
    Write-Host "P  = POST-NOVELTY SG-APT (新規性実装後)" -ForegroundColor Yellow
    Write-Host "Each B0/P condition uses the same scenario, seed and repetition."
    Write-Host "The HairSystem window title identifies the case currently running."
    Write-Host "============================================================"

    $diagnosticRoot = Join-Path $OutputRoot "diagnostics\$([string]$Session.Value.sessionId)"
    $dumpFolder = Join-Path $diagnosticRoot "crash_dumps"
    $dumpSnapshot = Enable-HairSystemCrashDumpCapture -DumpFolder $dumpFolder
    $oldCheckpoint = $env:HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_FILE
    if ($DiagnosticCheckpoints) {
        New-Item -ItemType Directory -Path $diagnosticRoot -Force | Out-Null
        $env:HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_FILE = (
            Join-Path $diagnosticRoot "viewer_checkpoints.log"
        )
    }
    else {
        Remove-Item Env:HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_FILE -ErrorAction SilentlyContinue
    }

    try {
        Invoke-NativeChecked `
            -FilePath $python.FilePath `
            -Arguments (@($python.Prefix) + @(
                $matrix,
                "--repo", $Repo,
                "--viewer", $viewer,
                "--template", $template,
                "--timeline-root", $timeline,
                "--output", $Output,
                "--profile", $Profile,
                "--variants", "B0,P",
                "--scenarios", $Scenarios,
                "--seeds", $SeedList,
                "--repetitions", "$RepeatCount",
                "--schedule", "counterbalanced",
                "--require-fxc"
            )) `
            -Label "B0/P $Profile matrix"

        if ($ExpectedRunCount -gt 0) {
            Assert-MatrixSummaryContract `
                -Output $Output `
                -Profile $Profile `
                -ExpectedRuns $ExpectedRunCount
        }
    }
    finally {
        if ([string]::IsNullOrEmpty($oldCheckpoint)) {
            Remove-Item Env:HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_FILE -ErrorAction SilentlyContinue
        }
        else {
            $env:HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_FILE = $oldCheckpoint
        }
        Restore-HairSystemCrashDumpCapture -Snapshot $dumpSnapshot
        Remove-Item Env:HAIRGPU_FROZEN_ASSET_MANIFEST -ErrorAction SilentlyContinue
        Remove-Item Env:HAIRGPU_FORMAL_SESSION_ID -ErrorAction SilentlyContinue
        Remove-Item Env:HAIRGPU_FORMAL_COMMIT -ErrorAction SilentlyContinue
        Exit-FormalHostGuard
    }
}


# ----------------------------------------------------------------------
# 70.27 soak thermal-stability hardening.
#
# Formal thresholds are not weakened. The GPU is brought to a repeatable
# loaded thermal state using a balanced B0/P Pilot pair, and the exact four-run
# Soak matrix is exercised outside sessions\ before thesis evidence starts.
# ----------------------------------------------------------------------
function Get-NvidiaFormalGpuState {
    $nvidia = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
    if ($null -eq $nvidia) {
        throw "nvidia-smi.exe is required for formal GPU thermal conditioning."
    }

    $raw = @(
        & $nvidia.Source `
            "--query-gpu=index,temperature.gpu,clocks.current.graphics,utilization.gpu,pstate,power.draw,power.limit" `
            "--format=csv,noheader,nounits"
    )
    if ($LASTEXITCODE -ne 0 -or $raw.Count -lt 1) {
        throw "Could not query NVIDIA GPU state for formal thermal conditioning."
    }

    $fields = @(([string]$raw[0]).Split(',') | ForEach-Object { $_.Trim() })
    if ($fields.Count -lt 7) {
        throw "Unexpected nvidia-smi thermal-state output: $($raw[0])"
    }

    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    return [PSCustomObject]@{
        index = [int]::Parse($fields[0], $culture)
        temperatureC = [double]::Parse($fields[1], $culture)
        graphicsClockMHz = [double]::Parse($fields[2], $culture)
        utilizationPercent = [double]::Parse($fields[3], $culture)
        pstate = [string]$fields[4]
        powerDrawW = [double]::Parse($fields[5], $culture)
        powerLimitW = [double]::Parse($fields[6], $culture)
    }
}

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

    $hardwareProfiles = Read-JsonFile -Path (
        Join-Path $Repo "benchmarks\novelty01\hardware_profiles.json"
    )
    $executionContract = $hardwareProfiles.profiles.measurement_rtx4070ti_super.executionContract
    [double]$maximumTemperatureC = [double]$executionContract.maximumGpuTemperatureCelsius

    [int]$maximumCycles = 6
    [int]$requiredStableCycles = 3
    [double]$maximumEndpointSpreadC = 3.0
    $states = @()
    $stable = $false
    [double]$stableSpreadC = [double]::PositiveInfinity

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "SOAK THERMAL CONDITIONING (NOT THESIS EVIDENCE)"
    Write-Host "============================================================"
    Write-Host "Label                     : $Label"
    Write-Host "Load                      : counterbalanced B0/P Pilot S13"
    Write-Host "Required stable cycles    : $requiredStableCycles"
    Write-Host "Endpoint temperature span : <= $maximumEndpointSpreadC C"
    Write-Host "Maximum GPU temperature   : <= $maximumTemperatureC C"
    Write-Host "Formal thresholds         : UNCHANGED"
    Write-Host "============================================================"

    for ($cycle = 1; $cycle -le $maximumCycles; ++$cycle) {
        $cycleOutput = Join-Path $Root ("cycle_{0:D2}" -f $cycle)

        # Non-strict, non-evidence load. Both methods are used so conditioning
        # does not favor B0 or P.
        Invoke-Matrix `
            -Session $Session `
            -Profile "pilot" `
            -Scenarios "S13" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output $cycleOutput `
            -ExpectedRunCount 2

        Start-Sleep -Milliseconds 500
        $gpu = Get-NvidiaFormalGpuState
        $state = [PSCustomObject]@{
            cycle = $cycle
            capturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
            temperatureC = [double]$gpu.temperatureC
            graphicsClockMHz = [double]$gpu.graphicsClockMHz
            utilizationPercent = [double]$gpu.utilizationPercent
            pstate = [string]$gpu.pstate
            powerDrawW = [double]$gpu.powerDrawW
            powerLimitW = [double]$gpu.powerLimitW
        }
        $states += $state

        Write-Host ("Thermal cycle {0}: temp={1:N1}C clock={2:N0}MHz util={3:N0}% pstate={4} power={5:N1}/{6:N1}W" -f `
            $cycle, $state.temperatureC, $state.graphicsClockMHz, `
            $state.utilizationPercent, $state.pstate, $state.powerDrawW, $state.powerLimitW)

        if ([double]$state.temperatureC -gt $maximumTemperatureC) {
            $failure = [ordered]@{
                schema = "hairsystem-soak-thermal-conditioning-v1"
                status = "FAIL"
                reason = "maximum_gpu_temperature_exceeded"
                label = $Label
                maximumTemperatureC = $maximumTemperatureC
                states = @($states)
            }
            Write-JsonFile -Value $failure -Path (Join-Path $Root "thermal_conditioning.json")
            throw "GPU exceeded the unchanged formal maximum temperature during conditioning. Observed=$($state.temperatureC)C Limit=$maximumTemperatureC C"
        }

        if ($states.Count -ge $requiredStableCycles) {
            $window = @($states | Select-Object -Last $requiredStableCycles)
            $temperatures = @($window | ForEach-Object { [double]$_.temperatureC })
            [double]$windowMaximum = ($temperatures | Measure-Object -Maximum).Maximum
            [double]$windowMinimum = ($temperatures | Measure-Object -Minimum).Minimum
            $stableSpreadC = $windowMaximum - $windowMinimum
            if ($stableSpreadC -le $maximumEndpointSpreadC) {
                $stable = $true
                break
            }
        }
    }

    $result = [ordered]@{
        schema = "hairsystem-soak-thermal-conditioning-v1"
        status = if ($stable) { "PASS" } else { "FAIL" }
        formalEvidence = $false
        label = $Label
        methodBalance = "counterbalanced B0/P"
        profile = "pilot"
        scenario = "S13"
        maximumCycles = $maximumCycles
        requiredStableCycles = $requiredStableCycles
        maximumEndpointSpreadC = $maximumEndpointSpreadC
        observedStableSpreadC = if ([double]::IsPositiveInfinity($stableSpreadC)) { $null } else { $stableSpreadC }
        maximumTemperatureC = $maximumTemperatureC
        thresholdsChanged = $false
        states = @($states)
        completedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    }
    Write-JsonFile -Value $result -Path (Join-Path $Root "thermal_conditioning.json")

    if (-not $stable) {
        throw "GPU did not reach a repeatable loaded thermal plateau before Soak. Required endpoint spread <= $maximumEndpointSpreadC C across $requiredStableCycles consecutive B0/P load cycles."
    }
    Write-Host ("Soak thermal conditioning: PASS (endpoint spread={0:N1}C)" -f $stableSpreadC) -ForegroundColor Green
}


function Invoke-PreFormalQualificationSuite {
    param([Parameter(Mandatory = $true)]$Session)

    Assert-HardwareProfileReady -Session $Session
    Assert-FreeDiskSpace -Path $OutputRoot -MinimumFreeGiB 20.0

    $root = Join-Path (Join-Path $OutputRoot "qualification") ([string]$Session.Value.sessionId)
    Assert-NewOutputDirectory -Path $root
    New-Item -ItemType Directory -Path $root -Force | Out-Null

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "PRE-FORMAL QUALIFICATION (NOT THESIS EVIDENCE)"
    Write-Host "============================================================"
    Write-Host "1) P/B0 Performance S01 repeated 3x: reproduce the former 0xC0000005 path"
    Write-Host "2) Representative Performance S01/S07/S13/S16/S17/S18"
    Write-Host "3) Determinism S01 repeated 2x"
    Write-Host "4) Ground-truth stress S13 with formal profile"
    Write-Host "5) Loaded B0/P thermal conditioning until a stable endpoint plateau"
    Write-Host "6) Exact full Soak S01/S13 B0/P before thesis evidence"
    Write-Host "Outputs stay outside sessions\\ and cannot be counted by FinalGate."
    Write-Host "============================================================"

    Invoke-Matrix `
        -Session $Session `
        -Profile "performance" `
        -Scenarios "S01" `
        -SeedList "1" `
        -RepeatCount 3 `
        -Output (Join-Path $root "performance_s01_repeat3") `
        -ExpectedRunCount 6 `
        -DiagnosticCheckpoints

    Invoke-Matrix `
        -Session $Session `
        -Profile "performance" `
        -Scenarios "S01,S07,S13,S16,S17,S18" `
        -SeedList "1" `
        -RepeatCount 1 `
        -Output (Join-Path $root "performance_representative") `
        -ExpectedRunCount 12 `
        -DiagnosticCheckpoints

    Invoke-Matrix `
        -Session $Session `
        -Profile "determinism" `
        -Scenarios "S01" `
        -SeedList "1" `
        -RepeatCount 2 `
        -Output (Join-Path $root "determinism_s01_repeat2") `
        -ExpectedRunCount 4 `
        -DiagnosticCheckpoints

    Invoke-Matrix `
        -Session $Session `
        -Profile "formal" `
        -Scenarios "S13" `
        -SeedList "1" `
        -RepeatCount 1 `
        -Output (Join-Path $root "ground_truth_repair_stress") `
        -ExpectedRunCount 2 `
        -DiagnosticCheckpoints

    Invoke-SoakThermalConditioning `
        -Session $Session `
        -Root (Join-Path $root "thermal_conditioning") `
        -Label "preformal_exact_soak"

    Invoke-Matrix `
        -Session $Session `
        -Profile "soak" `
        -Scenarios "S01,S13" `
        -SeedList "1" `
        -RepeatCount 1 `
        -Output (Join-Path $root "soak_full_prequalification") `
        -ExpectedRunCount 4 `
        -DiagnosticCheckpoints

    $summary = [ordered]@{
        schema = "hairsystem-preformal-qualification-v1"
        status = "PASS"
        formalEvidence = $false
        sessionId = [string]$Session.Value.sessionId
        formalCommit = [string]$Session.Value.formalCommit
        completedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
        checks = @(
            "performance_s01_repeat3",
            "performance_representative",
            "determinism_s01_repeat2",
            "ground_truth_repair_stress",
            "thermal_conditioning",
            "soak_full_prequalification"
        )
        crashDumpDirectory = Join-Path (Join-Path (Join-Path $OutputRoot "diagnostics") ([string]$Session.Value.sessionId)) "crash_dumps"
        checkpointLog = Join-Path (Join-Path (Join-Path $OutputRoot "diagnostics") ([string]$Session.Value.sessionId)) "viewer_checkpoints.log"
    }
    Write-JsonFile -Value $summary -Path (Join-Path $root "qualification_result.json")
    Write-Host "Pre-formal qualification: PASS" -ForegroundColor Green
}

function Verify-LocalBranches {
    param([Parameter(Mandatory = $true)]$Session)

    Assert-SessionInvariant -Session $Session | Out-Null
    $before = Join-Path $Session.Directory "integrity\local_branches_before.txt"
    if (-not (Test-Path -LiteralPath $before -PathType Leaf)) {
        throw "Prepare branch inventory is missing: $before"
    }

    $after = Join-Path $Session.Directory "integrity\local_branches_after.txt"
    Save-LocalBranchInventory -Path $after

    $difference = Compare-Object `
        -ReferenceObject (Get-Content -LiteralPath $before) `
        -DifferenceObject (Get-Content -LiteralPath $after)

    if ($difference) {
        $difference | Out-String |
            Set-Content `
                -LiteralPath (
                    Join-Path $Session.Directory "integrity\LOCAL_BRANCH_CHANGE_DETECTED.txt"
                ) `
                -Encoding UTF8
        Set-ObjectProperty `
            -Object $Session.Value `
            -Name "status" `
            -Value "INVALID_LOCAL_BRANCH_CHANGE"
        Save-Session -Session $Session
        throw "Local branch inventory changed. Formal evidence is invalid."
    }

    $result = [ordered]@{
        status = "PASS"
        branchesUnchanged = $true
        beforeSha256 = (
            Get-FileHash -LiteralPath $before -Algorithm SHA256
        ).Hash.ToLowerInvariant()
        afterSha256 = (
            Get-FileHash -LiteralPath $after -Algorithm SHA256
        ).Hash.ToLowerInvariant()
        verifiedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    }

    Write-JsonFile `
        -Value $result `
        -Path (
            Join-Path $Session.Directory "integrity\branch_integrity_result.json"
        )
    Add-CompletedStage -Session $Session -Name "VerifyBranches"
}

# Paired 100-run thesis design is fixed, not user-selectable. Every counted
# condition has a matched B0/P pair; FinalGate requires exactly 50 runs each.
if ($Seeds -ne $ExpectedReducedSeeds) {
    throw "Paired 100-run thesis runner requires -Seeds `"$ExpectedReducedSeeds`". Observed=$Seeds"
}
if ($Repetitions -ne $ExpectedReducedRepetitions) {
    throw "Paired 100-run thesis runner requires -Repetitions $ExpectedReducedRepetitions. Observed=$Repetitions"
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
New-Item -ItemType Directory -Path $SessionsRoot -Force | Out-Null

switch ($Stage) {
    "Prepare" {
        $identity = Get-RepositoryIdentity
        Assert-CleanWorktree
        Assert-FormalSigningReady | Out-Null
        Invoke-StaticContractChecks

        if (-not $SessionId) {
            $SessionId = "{0}_{1}" -f (
                Get-Date -Format "yyyyMMdd_HHmmss"
            ), $identity.Head.Substring(0, 8)
        }
        if ($SessionId -notmatch "^[A-Za-z0-9_.-]+$") {
            throw "SessionId contains unsupported characters: $SessionId"
        }

        $sessionDirectory = Join-Path $SessionsRoot $SessionId
        if (Test-Path -LiteralPath $sessionDirectory) {
            throw "Session already exists. Refusing overwrite: $sessionDirectory"
        }

        foreach ($relative in @(
            "environment", "integrity", "logs", "smoke", "pilot",
            "quality", "performance", "determinism", "soak", "report", "archive"
        )) {
            New-Item `
                -ItemType Directory `
                -Path (Join-Path $sessionDirectory $relative) `
                -Force |
                Out-Null
        }

        $before = Join-Path $sessionDirectory "integrity\local_branches_before.txt"
        Save-LocalBranchInventory -Path $before
        Capture-Hardware -EnvironmentDirectory (
            Join-Path $sessionDirectory "environment"
        )

        $sessionValue = [ordered]@{
            schema = "hairgpu-thesis-formal-session-v2"
            status = "PREPARED"
            sessionId = $SessionId
            repository = $identity.Origin
            repositoryPath = $Repo
            canonicalScenarioRoot = (
                Join-Path $Repo "benchmarks\novelty01\sgapt_scenarios"
            )
            branch = $identity.Branch
            formalCommit = $identity.Head
            sourceTreeSha = $identity.Tree
            createdAtUtc = (Get-Date).ToUniversalTime().ToString("o")
            operator = $Operator
            outputDirectory = $sessionDirectory
            localBranchInventorySha256 = (
                Get-FileHash -LiteralPath $before -Algorithm SHA256
            ).Hash.ToLowerInvariant()
            comparisonMethods = @("B0", "P")
            sameBinary = $true
            sameCommit = $true
            schedule = "counterbalanced"
            seedAlgorithm = "sha256-scenario-physical-input-v2"
            seeds = @(1)
            repetitions = 1
            samplingPolicy = $ReducedSamplingPolicy
            expectedFormalRunCount = 100
            expectedMethodRunCount = 50
            comparisonLabels = [ordered]@{
                B0 = "PRE-NOVELTY BASELINE"
                P = "POST-NOVELTY SG-APT"
            }
            completedStages = @("Prepare")
            branchCreated = $false
            committed = $false
            pushed = $false
        }

        Write-JsonFile `
            -Value $sessionValue `
            -Path (Join-Path $sessionDirectory "formal_session.json")
        Set-Content `
            -LiteralPath $CurrentSessionPointer `
            -Value $SessionId `
            -Encoding UTF8

        Write-Host "Prepared isolated formal session: $sessionDirectory"
        Write-Host "Reduced thesis design: 100 total runs / 50 B0 / 50 P"
        Write-Host "Hardware profile to complete later: $(Join-Path $sessionDirectory 'environment\hardware_profile.json')"
    }

    "Asset" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Prepare")
        Assert-SessionInvariant -Session $session | Out-Null

        $assetManifest = Join-Path $session.Directory "asset\frozen_asset_manifest.json"
        if (Test-Path -LiteralPath $assetManifest) {
            throw "Asset evidence already exists. Create a new session instead of overwriting it."
        }

        Build-FormalRelease -Session $session

        $python = Get-PythonCommand
        $viewer = Join-Path $Repo "bin\Release\HairSystem\HairSystem.exe"
        $preflight = Join-Path $Repo "tools\final_stage\hairgpu_novelty01_preflight.py"
        $freeze = Join-Path $Repo "tools\final_stage\freeze_novelty01_asset.py"
        $template = Join-Path $Repo "docs\sample_assets\novelty01_100k_asset_manifest.json"
        $bundle = Join-Path $session.Directory "asset"

        Invoke-NativeChecked `
            -FilePath $python.FilePath `
            -Arguments (@($python.Prefix) + @(
                $preflight,
                "--repo", $Repo,
                "--require-fxc",
                "--json", (
                    Join-Path $session.Directory "integrity\novelty01_preflight.json"
                )
            )) `
            -Label "Novelty01 source preflight"

        Invoke-NativeChecked `
            -FilePath $python.FilePath `
            -Arguments (@($python.Prefix) + @(
                $freeze,
                "--template-manifest", $template,
                "--bundle-dir", $bundle,
                "--viewer-exe", $viewer,
                "--generate-source",
                "--operator", $Operator,
                "--repo-root", $Repo,
                "--required-branch", "main"
            )) `
            -Label "Generate and freeze canonical 100k asset"

        Invoke-NativeChecked `
            -FilePath $python.FilePath `
            -Arguments (@($python.Prefix) + @(
                $freeze,
                "--bundle-dir", $bundle,
                "--viewer-exe", $viewer,
                "--verify-only"
            )) `
            -Label "Verify canonical 100k asset"

        $frozen = Read-JsonFile -Path $assetManifest
        if ($frozen.freezeStatus -ne "FROZEN") {
            throw "Frozen asset manifest did not reach FROZEN status."
        }
        if (
            [int]$frozen.generation.displayStrandCount -ne 100000 -or
            [int]$frozen.generation.primaryGuideStrandCount -ne 4000 -or
            [int]$frozen.generation.followersPerGuide -ne 24 -or
            [int]$frozen.generation.particlesPerGuide -ne 128 -or
            [int]$frozen.generation.solverIterations -ne 6 -or
            [int]$frozen.generation.groupsPerBlock -ne 32
        ) {
            throw "Frozen asset generation contract mismatch."
        }

        Set-ObjectProperty `
            -Object $session.Value `
            -Name "assetManifestSha256" `
            -Value (
                Get-FileHash -LiteralPath $assetManifest -Algorithm SHA256
            ).Hash.ToLowerInvariant()
        Set-ObjectProperty `
            -Object $session.Value `
            -Name "assetBundleSha256" `
            -Value ([string]$frozen.bundleSha256).ToLowerInvariant()
        Set-ObjectProperty `
            -Object $session.Value `
            -Name "canonicalScenarioTreeSha256" `
            -Value (
                Get-DirectoryDigest -Path (
                    Join-Path $Repo "benchmarks\novelty01\sgapt_scenarios"
                )
            )
        Set-ObjectProperty `
            -Object $session.Value `
            -Name "status" `
            -Value "ASSET_FROZEN"
        Save-Session -Session $session

        # Fail early on the previously crashing P/Performance path and the next
        # unvisited formal stages. Qualification output is deliberately outside
        # the session directory and is never thesis evidence.
        Invoke-PreFormalQualificationSuite -Session $session

        Add-CompletedStage -Session $session -Name "Asset"
    }

    "Smoke" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Asset")
        Invoke-Matrix `
            -Session $session `
            -Profile "smoke" `
            -Scenarios "S01,S06,S11,S17,S18" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output (Join-Path $session.Directory "smoke\B0_vs_P") `
            -ExpectedRunCount 10
        Add-CompletedStage -Session $session -Name "Smoke"
    }

    "Pilot" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Smoke")
        Invoke-Matrix `
            -Session $session `
            -Profile "pilot" `
            -Scenarios "S01,S07,S13,S16,S17" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output (Join-Path $session.Directory "pilot\B0_vs_P") `
            -ExpectedRunCount 10
        Add-CompletedStage -Session $session -Name "Pilot"
    }

    "Quality" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Pilot")
        Assert-HardwareProfileReady -Session $session
        Invoke-Matrix `
            -Session $session `
            -Profile "formal" `
            -Scenarios "all" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output (Join-Path $session.Directory "quality\B0_vs_P") `
            -ExpectedRunCount 36
        Add-CompletedStage -Session $session -Name "Quality"
    }

    "Performance" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Quality")
        Assert-HardwareProfileReady -Session $session
        Invoke-Matrix `
            -Session $session `
            -Profile "performance" `
            -Scenarios "all" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output (Join-Path $session.Directory "performance\B0_vs_P") `
            -ExpectedRunCount 36
        Add-CompletedStage -Session $session -Name "Performance"
    }

    "Determinism" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Performance")
        Assert-HardwareProfileReady -Session $session
        Invoke-Matrix `
            -Session $session `
            -Profile "determinism" `
            -Scenarios "S01" `
            -SeedList "1" `
            -RepeatCount 2 `
            -Output (Join-Path $session.Directory "determinism\B0_vs_P") `
            -ExpectedRunCount 4
        Add-CompletedStage -Session $session -Name "Determinism"
    }

    "Soak" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Determinism")
        Assert-HardwareProfileReady -Session $session
        Assert-FreeDiskSpace -Path $OutputRoot -MinimumFreeGiB 20.0

        $preSoakRoot = Join-Path (
            Join-Path $OutputRoot "pre_soak_conditioning"
        ) ([string]$session.Value.sessionId)
        Invoke-SoakThermalConditioning `
            -Session $session `
            -Root $preSoakRoot `
            -Label "official_soak_entry"

        Invoke-Matrix `
            -Session $session `
            -Profile "soak" `
            -Scenarios "S01,S13" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output (Join-Path $session.Directory "soak\B0_vs_P") `
            -ExpectedRunCount 4
        Add-CompletedStage -Session $session -Name "Soak"
    }

    "Report" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Soak")
        Assert-SessionInvariant -Session $session | Out-Null
        Assert-FrozenInputs -Session $session
        Assert-FormalEvidenceReadyForReport -Session $session

        $reportPath = Join-Path $session.Directory "report\thesis_statistical_report.json"
        if (Test-Path -LiteralPath $reportPath -PathType Leaf) {
            throw "Report already exists. Refusing overwrite: $reportPath"
        }

        $python = Get-PythonCommand
        $reporter = Join-Path $Repo "tools\final_stage\hairgpu_thesis_report.py"
        Invoke-NativeChecked `
            -FilePath $python.FilePath `
            -Arguments (@($python.Prefix) + @(
                $reporter,
                "--session", $session.Directory
            )) `
            -Label "Generate thesis statistical report"
        Add-CompletedStage -Session $session -Name "Report"
    }

    "VerifyBranches" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("Report")
        $branchResult = Join-Path $session.Directory "integrity\branch_integrity_result.json"
        if (Test-Path -LiteralPath $branchResult -PathType Leaf) {
            throw "Local branch integrity result already exists. Refusing overwrite: $branchResult"
        }
        Verify-LocalBranches -Session $session
        Write-Host "Local branch inventory is unchanged."
    }

    "FinalGate" {
        $session = Load-Session
        Assert-CompletedStages `
            -Session $session `
            -Required @("Report", "VerifyBranches")
        Assert-HardwareProfileReady -Session $session
        Assert-SessionInvariant -Session $session | Out-Null
        Assert-FrozenInputs -Session $session

        $gateResult = Join-Path $session.Directory "integrity\final_gate.json"
        if (Test-Path -LiteralPath $gateResult -PathType Leaf) {
            throw "Final gate result already exists. Refusing overwrite: $gateResult"
        }

        $python = Get-PythonCommand
        $gate = Join-Path $Repo "tools\final_stage\hairgpu_thesis_final_gate.py"
        Invoke-NativeChecked `
            -FilePath $python.FilePath `
            -Arguments (@($python.Prefix) + @(
                $gate,
                "--session", $session.Directory
            )) `
            -Label "Thesis final integrity gate"

        Set-ObjectProperty `
            -Object $session.Value `
            -Name "status" `
            -Value "FORMAL_COMPLETE_VALID"
        Save-Session -Session $session
        Add-CompletedStage -Session $session -Name "FinalGate"
    }

    "Archive" {
        $session = Load-Session
        Assert-CompletedStages -Session $session -Required @("FinalGate")
        Assert-FreeDiskSpace -Path $OutputRoot -MinimumFreeGiB 10.0

        $gateResult = Join-Path $session.Directory "integrity\final_gate.json"
        if (-not (Test-Path -LiteralPath $gateResult -PathType Leaf)) {
            throw "Final gate result is missing. Run Stage=FinalGate first."
        }

        $gate = Read-JsonFile -Path $gateResult
        if ($gate.status -ne "PASS") {
            throw "Cannot archive an invalid formal session."
        }

        $archives = Join-Path $OutputRoot "archives"
        New-Item -ItemType Directory -Path $archives -Force | Out-Null
        $destination = Join-Path $archives ("$($session.Value.sessionId).zip")
        if (Test-Path -LiteralPath $destination) {
            throw "Archive already exists. Refusing overwrite: $destination"
        }

        Compress-Archive `
            -LiteralPath $session.Directory `
            -DestinationPath $destination `
            -CompressionLevel Optimal

        $archiveHash = (
            Get-FileHash -LiteralPath $destination -Algorithm SHA256
        ).Hash.ToLowerInvariant()

        Set-Content `
            -LiteralPath "$destination.sha256" `
            -Value "$archiveHash  $([IO.Path]::GetFileName($destination))" `
            -Encoding ASCII

        Set-ObjectProperty `
            -Object $session.Value `
            -Name "archivePath" `
            -Value $destination
        Set-ObjectProperty `
            -Object $session.Value `
            -Name "archiveSha256" `
            -Value $archiveHash
        Save-Session -Session $session
        Add-CompletedStage -Session $session -Name "Archive"

        Write-Host "Archive: $destination"
        Write-Host "SHA-256: $archiveHash"
    }
}
