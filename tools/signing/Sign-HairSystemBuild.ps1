[CmdletBinding()]
param(
    [string]$BuildRoot = '.',
    [string]$Thumbprint,
    [string]$SubjectLike = 'HairSystem Internal Test Signing',
    [ValidateSet('CurrentUser','LocalMachine')]
    [string]$StoreScope = 'CurrentUser',
    [string]$TimestampServer,
    [switch]$IncludePowershellScripts,
    [switch]$WhatIf
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

    throw 'signtool.exe was not found. Install Visual Studio or Windows SDK first.'
}

function Resolve-Certificate {
    param([string]$Thumbprint,[string]$SubjectLike,[string]$StoreScope)
    $store = "Cert:\$StoreScope\My"
    $certs = Get-ChildItem -Path $store
    if ($Thumbprint) {
        $normalized = ($Thumbprint -replace '\s+','').ToUpperInvariant()
        $match = $certs | Where-Object { $_.Thumbprint.ToUpperInvariant() -eq $normalized } |
            Sort-Object NotAfter -Descending | Select-Object -First 1
        if ($match) { return $match }
        throw "Certificate with thumbprint $Thumbprint not found in $store"
    }

    $match = $certs |
        Where-Object { $_.Subject -like "*$SubjectLike*" -or $_.FriendlyName -like "*$SubjectLike*" } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1
    if ($match) { return $match }
    throw "Certificate matching '$SubjectLike' not found in $store"
}

$signtool = Find-SignTool
$cert = Resolve-Certificate -Thumbprint $Thumbprint -SubjectLike $SubjectLike -StoreScope $StoreScope

$resolvedRoot = (Resolve-Path $BuildRoot).Path
$patterns = @('*.exe','*.dll','*.msi','*.appx','*.msix')
$files = foreach ($pattern in $patterns) {
    Get-ChildItem -Path $resolvedRoot -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue
}
$files = $files | Sort-Object FullName -Unique

if (!$files) {
    throw "No signable files found under $resolvedRoot"
}

Write-Host "Using signtool: $signtool"
Write-Host "Using certificate thumbprint: $($cert.Thumbprint)"

foreach ($file in $files) {
    $args = @('sign','/fd','SHA256','/sha1',$cert.Thumbprint,'/s','My')
    if ($StoreScope -eq 'LocalMachine') { $args += '/sm' }
    if ($TimestampServer) {
        $args += @('/tr',$TimestampServer,'/td','SHA256')
    }
    $args += $file.FullName

    if ($WhatIf) {
        Write-Host "[WhatIf] $signtool $($args -join ' ')"
    } else {
        Write-Host "Signing $($file.FullName)" -ForegroundColor Cyan
        & $signtool @args
        if ($LASTEXITCODE -ne 0) {
            throw "signtool failed for $($file.FullName)"
        }
    }
}

if ($IncludePowershellScripts) {
    $psFiles = Get-ChildItem -Path $resolvedRoot -Recurse -File -Filter *.ps1 -ErrorAction SilentlyContinue
    foreach ($psFile in $psFiles) {
        if ($WhatIf) {
            Write-Host "[WhatIf] Set-AuthenticodeSignature $($psFile.FullName)"
        } else {
            Set-AuthenticodeSignature -FilePath $psFile.FullName -Certificate $cert | Out-Null
            Write-Host "Signed script $($psFile.FullName)" -ForegroundColor DarkCyan
        }
    }
}

Write-Host 'Signing complete.' -ForegroundColor Green
