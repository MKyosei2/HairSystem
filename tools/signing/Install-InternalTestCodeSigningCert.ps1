[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CertPath,
    [ValidateSet('CurrentUser','LocalMachine')]
    [string]$Scope = 'CurrentUser',
    [switch]$SkipRoot,
    [switch]$SkipTrustedPublisher
)

$ErrorActionPreference = 'Stop'

if (!(Test-Path $CertPath)) {
    throw "Certificate file not found: $CertPath"
}

$resolved = (Resolve-Path $CertPath).Path
Unblock-File -Path $resolved -ErrorAction SilentlyContinue

if (-not $SkipRoot) {
    $rootStore = "Cert:\$Scope\Root"
    Import-Certificate -FilePath $resolved -CertStoreLocation $rootStore | Out-Null
    Write-Host "Imported into $rootStore" -ForegroundColor Green
}

if (-not $SkipTrustedPublisher) {
    $pubStore = "Cert:\$Scope\TrustedPublisher"
    Import-Certificate -FilePath $resolved -CertStoreLocation $pubStore | Out-Null
    Write-Host "Imported into $pubStore" -ForegroundColor Green
}

Write-Host 'Done.' -ForegroundColor Green
