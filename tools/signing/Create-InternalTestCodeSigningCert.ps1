[CmdletBinding()]
param(
    [string]$Subject = 'CN=HairSystem Internal Test Signing',
    [ValidateSet('CurrentUser','LocalMachine')]
    [string]$StoreScope = 'CurrentUser',
    [string]$OutDir = "$PSScriptRoot/output",
    [int]$YearsValid = 3,
    [int]$KeyLength = 3072,
    [string]$PfxPassword,
    [switch]$Overwrite
)

$ErrorActionPreference = 'Stop'

if (!(Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

$storePath = "Cert:\$StoreScope\My"

$existing = Get-ChildItem -Path $storePath | Where-Object { $_.Subject -eq $Subject }
if ($existing -and -not $Overwrite) {
    $cert = $existing | Sort-Object NotAfter -Descending | Select-Object -First 1
} else {
    $cert = New-SelfSignedCertificate \
        -Type CodeSigningCert \
        -Subject $Subject \
        -CertStoreLocation $storePath \
        -KeyAlgorithm RSA \
        -KeyLength $KeyLength \
        -HashAlgorithm SHA256 \
        -FriendlyName 'HairSystem Internal Test Signing' \
        -NotAfter (Get-Date).AddYears($YearsValid)
}

$baseName = 'hairgpu-internal-test-signing'
$cerPath = Join-Path $OutDir "$baseName.cer"
$pfxPath = Join-Path $OutDir "$baseName.pfx"
$jsonPath = Join-Path $OutDir "$baseName.json"

Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null

$pfxWritten = $false
if ($PfxPassword) {
    $secure = ConvertTo-SecureString -String $PfxPassword -AsPlainText -Force
    Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $secure -Force | Out-Null
    $pfxWritten = $true
}

$info = [ordered]@{
    subject = $cert.Subject
    thumbprint = $cert.Thumbprint
    notBefore = $cert.NotBefore.ToString('o')
    notAfter = $cert.NotAfter.ToString('o')
    storeScope = $StoreScope
    storePath = $storePath
    cerPath = (Resolve-Path $cerPath).Path
    pfxPath = if ($pfxWritten) { (Resolve-Path $pfxPath).Path } else { $null }
}

$info | ConvertTo-Json | Set-Content -Encoding UTF8 $jsonPath

Write-Host ''
Write-Host 'Created / selected certificate:' -ForegroundColor Green
Write-Host "  Subject    : $($cert.Subject)"
Write-Host "  Thumbprint : $($cert.Thumbprint)"
Write-Host "  CER        : $cerPath"
if ($pfxWritten) {
    Write-Host "  PFX        : $pfxPath"
} else {
    Write-Host '  PFX        : not exported (pass -PfxPassword to export one)'
}
Write-Host "  JSON       : $jsonPath"
