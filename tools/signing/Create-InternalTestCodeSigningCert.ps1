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

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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

if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

$storePath = "Cert:\$StoreScope\My"

$existing = @(
    Get-ChildItem -Path $storePath -ErrorAction Stop |
        Where-Object {
            $_.Subject -eq $Subject -and
            $_.HasPrivateKey -and
            (Get-Date) -ge $_.NotBefore -and
            (Get-Date) -le $_.NotAfter
        }
)

if ($existing.Count -gt 0 -and -not $Overwrite) {
    $cert = $existing |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1
}
else {
    $certificateParameters = @{
        Type = 'CodeSigningCert'
        Subject = $Subject
        CertStoreLocation = $storePath
        KeyAlgorithm = 'RSA'
        KeyLength = $KeyLength
        HashAlgorithm = 'SHA256'
        FriendlyName = 'HairSystem Internal Test Signing'
        NotAfter = (Get-Date).AddYears($YearsValid)
    }
    $cert = New-SelfSignedCertificate @certificateParameters
}

if (-not $cert.HasPrivateKey) {
    throw "Created/selected certificate has no private key: $($cert.Thumbprint)"
}

$ekuOids = @(Get-CertificateEkuOidValues -Certificate $cert)
if ($ekuOids -notcontains '1.3.6.1.5.5.7.3.3') {
    throw "Created/selected certificate does not contain the Code Signing EKU."
}

$baseName = 'hairgpu-internal-test-signing'
$cerPath = Join-Path $OutDir "$baseName.cer"
$pfxPath = Join-Path $OutDir "$baseName.pfx"
$jsonPath = Join-Path $OutDir "$baseName.json"

Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null

$pfxWritten = $false
if ($PfxPassword) {
    $secure = ConvertTo-SecureString `
        -String $PfxPassword `
        -AsPlainText `
        -Force
    Export-PfxCertificate `
        -Cert $cert `
        -FilePath $pfxPath `
        -Password $secure `
        -Force |
        Out-Null
    $pfxWritten = $true
}

$info = [ordered]@{
    subject = $cert.Subject
    thumbprint = $cert.Thumbprint
    notBefore = $cert.NotBefore.ToString('o')
    notAfter = $cert.NotAfter.ToString('o')
    hasPrivateKey = $cert.HasPrivateKey
    codeSigningEku = $true
    storeScope = $StoreScope
    storePath = $storePath
    cerPath = (Resolve-Path -LiteralPath $cerPath).Path
    pfxPath = if ($pfxWritten) {
        (Resolve-Path -LiteralPath $pfxPath).Path
    }
    else {
        $null
    }
}

$info |
    ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath $jsonPath -Encoding UTF8

Write-Host ''
Write-Host 'Created / selected certificate:' -ForegroundColor Green
Write-Host "  Subject    : $($cert.Subject)"
Write-Host "  Thumbprint : $($cert.Thumbprint)"
Write-Host "  NotAfter   : $($cert.NotAfter.ToString('o'))"
Write-Host "  CER        : $cerPath"
if ($pfxWritten) {
    Write-Host "  PFX        : $pfxPath"
}
else {
    Write-Host '  PFX        : not exported (pass -PfxPassword to export one)'
}
Write-Host "  JSON       : $jsonPath"
