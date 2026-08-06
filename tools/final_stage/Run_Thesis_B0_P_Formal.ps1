param(
    [ValidateSet("Prepare", "Asset", "Smoke", "Quality", "Performance", "VerifyBranches")]
    [string]$Stage = "Prepare",
    [string]$Repo = "C:\Users\kyose\source\repos\HairSystem",
    [string]$OutputRoot = "C:\HairSystemFormalResults",
    [string]$Operator = "kyose",
    [string]$Seeds = "1,2,3,4,5",
    [int]$Repetitions = 3
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "[RUN] $Label"
    Write-Host "============================================================"

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

function Ensure-OutputTree {
    $folders = @(
        $OutputRoot,
        (Join-Path $OutputRoot "assets"),
        (Join-Path $OutputRoot "assets\novelty01_100k"),
        (Join-Path $OutputRoot "environment"),
        (Join-Path $OutputRoot "integrity"),
        (Join-Path $OutputRoot "logs"),
        (Join-Path $OutputRoot "smoke"),
        (Join-Path $OutputRoot "pilot"),
        (Join-Path $OutputRoot "formal_quality"),
        (Join-Path $OutputRoot "formal_performance"),
        (Join-Path $OutputRoot "determinism"),
        (Join-Path $OutputRoot "long_run"),
        (Join-Path $OutputRoot "comparison"),
        (Join-Path $OutputRoot "temporary")
    )
    foreach ($folder in $folders) {
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
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
    $head = (& git.exe -C $Repo rev-parse HEAD | Out-String).Trim()
    $origin = (& git.exe -C $Repo remote get-url origin | Out-String).Trim()
    if ($branch -ne "main") {
        throw "Formal experiment requires the existing main branch. Current=$branch"
    }
    if ($origin -notmatch "MKyosei2/HairSystem") {
        throw "Unexpected Git remote: $origin"
    }
    return [PSCustomObject]@{
        Branch = $branch
        Head = $head
        Origin = $origin
    }
}

function Assert-CleanWorktree {
    $dirty = (& git.exe -C $Repo status --porcelain --untracked-files=all | Out-String).Trim()
    if ($dirty) {
        Write-Host $dirty
        throw "Formal execution requires a clean worktree. Commit the applied fixes on main first."
    }
}

function Save-RemoteBranchInventory {
    param([Parameter(Mandatory = $true)][string]$Path)
    @(& git.exe -C $Repo ls-remote --heads origin) |
        Sort-Object |
        Set-Content -LiteralPath $Path -Encoding UTF8
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read remote branch inventory."
    }
}

function Assert-HardwareProfileReady {
    $path = Join-Path $OutputRoot "environment\hardware_profile.json"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Hardware profile is missing: $path"
    }
    $profile = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($profile.status -ne "READY") {
        throw "Set hardware_profile.json status to READY after entering the fixed PC details."
    }
}

function Build-FormalRelease {
    $msbuild = Get-MSBuildPath
    $solution = Join-Path $Repo "HairSystem.sln"
    $log = Join-Path $OutputRoot "logs\formal_release_build.log"

    Get-Process HairSystem -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    Remove-Item -LiteralPath (Join-Path $Repo "bin\Release\HairSystem") `
        -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $Repo "bin-int\Release\HairSystem") `
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
}

function Invoke-Matrix {
    param(
        [Parameter(Mandatory = $true)][string]$Profile,
        [Parameter(Mandatory = $true)][string]$Scenarios,
        [Parameter(Mandatory = $true)][string]$SeedList,
        [Parameter(Mandatory = $true)][int]$RepeatCount,
        [Parameter(Mandatory = $true)][string]$Output
    )

    $python = Get-PythonCommand
    $viewer = Join-Path $Repo "bin\Release\HairSystem\HairSystem.exe"
    $matrix = Join-Path $Repo "tools\final_stage\hairgpu_novelty01_matrix.py"
    $template = Join-Path $Repo "docs\sample_assets\final_runtime_suite_template.json"
    $timeline = Join-Path $Repo "benchmarks\novelty01\sgapt_scenarios"
    $manifest = Join-Path $OutputRoot "assets\novelty01_100k\frozen_asset_manifest.json"

    foreach ($required in @($viewer, $matrix, $template, $timeline, $manifest)) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "Required formal input is missing: $required"
        }
    }

    $env:HAIRGPU_FROZEN_ASSET_MANIFEST = $manifest
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
    }
    finally {
        Remove-Item Env:HAIRGPU_FROZEN_ASSET_MANIFEST -ErrorAction SilentlyContinue
    }
}

$Repo = [IO.Path]::GetFullPath($Repo)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
Ensure-OutputTree
$identity = Get-RepositoryIdentity

switch ($Stage) {
    "Prepare" {
        $before = Join-Path $OutputRoot "integrity\remote_branches_before.txt"
        Save-RemoteBranchInventory -Path $before
        $branchHash = (Get-FileHash -LiteralPath $before -Algorithm SHA256).Hash

        @(
            "repository=$($identity.Origin)",
            "branch=$($identity.Branch)",
            "head=$($identity.Head)",
            "capturedAt=$((Get-Date).ToString('o'))",
            "remoteBranchInventorySha256=$branchHash"
        ) | Set-Content `
            -LiteralPath (Join-Path $OutputRoot "integrity\repository_before.txt") `
            -Encoding UTF8

        $hardwarePath = Join-Path $OutputRoot "environment\hardware_profile.json"
        if (-not (Test-Path -LiteralPath $hardwarePath -PathType Leaf)) {
            [ordered]@{
                schema = "hairgpu-formal-hardware-profile-v1"
                status = "PENDING_USER_ENTRY"
                cpu = ""
                gpu = ""
                gpuVramMiB = $null
                gpuDriver = ""
                ramMiB = $null
                windowsVersion = ""
                windowsBuild = ""
                visualStudioVersion = ""
                msvcVersion = ""
                windowsSdkVersion = ""
                pythonVersion = ""
                powerPlan = ""
                displayResolution = "1920x1080"
                acPowerRequired = $true
                notes = ""
            } | ConvertTo-Json -Depth 5 |
                Set-Content -LiteralPath $hardwarePath -Encoding UTF8
        }
        Write-Host "Preparation complete: $OutputRoot"
    }

    "Asset" {
        Assert-CleanWorktree
        Build-FormalRelease

        $python = Get-PythonCommand
        $viewer = Join-Path $Repo "bin\Release\HairSystem\HairSystem.exe"
        $preflight = Join-Path $Repo "tools\final_stage\hairgpu_novelty01_preflight.py"
        $freeze = Join-Path $Repo "tools\final_stage\freeze_novelty01_asset.py"
        $template = Join-Path $Repo "docs\sample_assets\novelty01_100k_asset_manifest.json"
        $bundle = Join-Path $OutputRoot "assets\novelty01_100k"

        Invoke-NativeChecked `
            -FilePath $python.FilePath `
            -Arguments (@($python.Prefix) + @(
                $preflight,
                "--repo", $Repo,
                "--require-fxc",
                "--json", (Join-Path $OutputRoot "integrity\novelty01_preflight.json")
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
                "--required-branch", "main",
                "--force"
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
    }

    "Smoke" {
        Assert-CleanWorktree
        Invoke-Matrix `
            -Profile "smoke" `
            -Scenarios "S01,S06,S11,S17,S18" `
            -SeedList "1" `
            -RepeatCount 1 `
            -Output (Join-Path $OutputRoot "smoke\B0_vs_P")
    }

    "Quality" {
        Assert-CleanWorktree
        Assert-HardwareProfileReady
        Invoke-Matrix `
            -Profile "formal" `
            -Scenarios "all" `
            -SeedList $Seeds `
            -RepeatCount $Repetitions `
            -Output (Join-Path $OutputRoot "formal_quality\B0_vs_P")
    }

    "Performance" {
        Assert-CleanWorktree
        Assert-HardwareProfileReady
        Invoke-Matrix `
            -Profile "performance" `
            -Scenarios "all" `
            -SeedList $Seeds `
            -RepeatCount $Repetitions `
            -Output (Join-Path $OutputRoot "formal_performance\B0_vs_P")
    }

    "VerifyBranches" {
        $before = Join-Path $OutputRoot "integrity\remote_branches_before.txt"
        if (-not (Test-Path -LiteralPath $before -PathType Leaf)) {
            throw "Run Prepare first. Missing: $before"
        }
        $after = Join-Path $OutputRoot "integrity\remote_branches_after.txt"
        Save-RemoteBranchInventory -Path $after
        $difference = Compare-Object `
            -ReferenceObject (Get-Content -LiteralPath $before) `
            -DifferenceObject (Get-Content -LiteralPath $after)
        if ($difference) {
            $difference | Out-String |
                Set-Content `
                    -LiteralPath (Join-Path $OutputRoot "integrity\REMOTE_BRANCH_CHANGE_DETECTED.txt") `
                    -Encoding UTF8
            throw "Remote branch inventory changed. Formal evidence is invalid."
        }

        @(
            "status=PASS",
            "branchesUnchanged=true",
            "beforeSha256=$((Get-FileHash -LiteralPath $before -Algorithm SHA256).Hash)",
            "afterSha256=$((Get-FileHash -LiteralPath $after -Algorithm SHA256).Hash)",
            "verifiedAt=$((Get-Date).ToString('o'))"
        ) | Set-Content `
            -LiteralPath (Join-Path $OutputRoot "integrity\remote_branch_integrity_result.txt") `
            -Encoding UTF8
        Write-Host "Remote branch inventory is unchanged."
    }
}
