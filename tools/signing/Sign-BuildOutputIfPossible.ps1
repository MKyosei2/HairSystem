[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildOutputDir,
    [string]$SubjectLike = 'HairSystem Internal Test Signing',
    [ValidateSet('CurrentUser','LocalMachine')]
    [string]$StoreScope = 'CurrentUser'
)

$ErrorActionPreference = 'Stop'

function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $roots = @(
        "$env:ProgramFiles(x86)\Windows Kits\10\bin",
        "$env:ProgramFiles\Windows Kits\10\bin"
    ) | Where-Object { $_ -and (Test-Path $_) }

    foreach ($root in $roots) {
        $candidate = Get-ChildItem -Path $root -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }

    return $null
}

try {
    if (!(Test-Path $BuildOutputDir)) {
        Write-Host "Build output not found. Skipping auto-sign." -ForegroundColor DarkYellow
        exit 0
    }

    $resolvedRoot = (Resolve-Path $BuildOutputDir).Path
    $cert = Get-ChildItem -Path "Cert:\$StoreScope\My" -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -like "*$SubjectLike*" -or $_.FriendlyName -like "*$SubjectLike*" } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1

    if (-not $cert) {
        Write-Host "No local test signing certificate found. Auto-sign skipped." -ForegroundColor DarkYellow
        exit 0
    }

    $signtool = Find-SignTool
    if (-not $signtool) {
        Write-Host "signtool.exe not found. Auto-sign skipped." -ForegroundColor DarkYellow
        exit 0
    }

    $files = Get-ChildItem -Path $resolvedRoot -Recurse -File -Include *.exe,*.dll -ErrorAction SilentlyContinue |
        Sort-Object FullName -Unique

    if (-not $files) {
        Write-Host "No signable files found. Auto-sign skipped." -ForegroundColor DarkYellow
        exit 0
    }

    foreach ($file in $files) {
        Write-Host "Auto-signing $($file.FullName)" -ForegroundColor Cyan
        & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /s My $file.FullName | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "signtool failed for $($file.FullName). Continuing without failing the build." -ForegroundColor DarkYellow
        }
    }
}
catch {
    Write-Host "Auto-sign skipped: $($_.Exception.Message)" -ForegroundColor DarkYellow
}

exit 0
