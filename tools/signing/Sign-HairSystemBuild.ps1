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

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $roots = @(
        "$env:ProgramFiles(x86)\Windows Kits\10\bin",
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

        if ($candidate) {
            return $candidate.FullName
        }
    }

    throw 'signtool.exe was not found. Install Visual Studio or Windows SDK first.'
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

function Resolve-Certificate {
    param(
        [string]$Thumbprint,
        [string]$SubjectLike,
        [string]$StoreScope
    )

    $store = "Cert:\$StoreScope\My"
    $certs = @(Get-ChildItem -Path $store -ErrorAction Stop)

    if ($Thumbprint) {
        $normalized = ($Thumbprint -replace '\s+','').ToUpperInvariant()
        $match = $certs |
            Where-Object { $_.Thumbprint.ToUpperInvariant() -eq $normalized } |
            Sort-Object NotAfter -Descending |
            Select-Object -First 1

        if (-not $match) {
            throw "Certificate with thumbprint $Thumbprint not found in $store"
        }
    }
    else {
        $match = $certs |
            Where-Object {
                $_.Subject -like "*$SubjectLike*" -or
                $_.FriendlyName -like "*$SubjectLike*"
            } |
            Sort-Object NotAfter -Descending |
            Select-Object -First 1

        if (-not $match) {
            throw "Certificate matching '$SubjectLike' not found in $store"
        }
    }

    if (-not $match.HasPrivateKey) {
        throw "Signing certificate has no private key: $($match.Thumbprint)"
    }

    $now = Get-Date
    if ($now -lt $match.NotBefore -or $now -gt $match.NotAfter) {
        throw "Signing certificate is outside its validity period: $($match.Thumbprint)"
    }

    $ekuOids = @(Get-CertificateEkuOidValues -Certificate $match)
    if ($ekuOids -notcontains '1.3.6.1.5.5.7.3.3') {
        throw "Certificate is not valid for Code Signing EKU: $($match.Thumbprint)"
    }

    return $match
}

function Assert-SignatureValid {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedThumbprint,
        [switch]$AllowOtherValidPublisher
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid') {
        throw "Authenticode signature is not Valid: $Path Status=$($signature.Status) Message=$($signature.StatusMessage)"
    }
    if ($null -eq $signature.SignerCertificate) {
        throw "Valid signature has no signer certificate: $Path"
    }

    $actualThumbprint = (
        [string]$signature.SignerCertificate.Thumbprint
    ).ToUpperInvariant()
    $expected = (
        $ExpectedThumbprint -replace '\s+',''
    ).ToUpperInvariant()

    if (-not $AllowOtherValidPublisher -and $actualThumbprint -ne $expected) {
        throw "Unexpected signer for $Path Expected=$expected Actual=$actualThumbprint"
    }

    return $signature
}

$signtool = Find-SignTool
$cert = Resolve-Certificate `
    -Thumbprint $Thumbprint `
    -SubjectLike $SubjectLike `
    -StoreScope $StoreScope

$resolvedRoot = (Resolve-Path -LiteralPath $BuildRoot).Path
$patterns = @('*.exe','*.dll','*.msi','*.appx','*.msix')
$files = foreach ($pattern in $patterns) {
    Get-ChildItem `
        -LiteralPath $resolvedRoot `
        -Recurse `
        -File `
        -Filter $pattern `
        -ErrorAction SilentlyContinue
}
$files = @($files | Sort-Object FullName -Unique)

if ($files.Count -eq 0) {
    throw "No signable files found under $resolvedRoot"
}

Write-Host "Using signtool: $signtool"
Write-Host "Using certificate subject: $($cert.Subject)"
Write-Host "Using certificate thumbprint: $($cert.Thumbprint)"
Write-Host "Certificate valid until: $($cert.NotAfter.ToString('o'))"

foreach ($file in $files) {
    $existing = Get-AuthenticodeSignature -LiteralPath $file.FullName

    # Preserve a valid third-party/Microsoft signature instead of replacing it.
    if (
        $existing.Status -eq 'Valid' -and
        $null -ne $existing.SignerCertificate -and
        ([string]$existing.SignerCertificate.Thumbprint).ToUpperInvariant() -ne
            ([string]$cert.Thumbprint).ToUpperInvariant()
    ) {
        Write-Host "Preserving existing valid signature: $($file.FullName)" -ForegroundColor DarkCyan
        continue
    }

    $args = @(
        'sign',
        '/fd', 'SHA256',
        '/sha1', $cert.Thumbprint,
        '/s', 'My'
    )
    if ($StoreScope -eq 'LocalMachine') {
        $args += '/sm'
    }
    if ($TimestampServer) {
        $args += @('/tr', $TimestampServer, '/td', 'SHA256')
    }
    $args += $file.FullName

    if ($WhatIf) {
        Write-Host "[WhatIf] $signtool $($args -join ' ')"
        continue
    }

    Write-Host "Signing $($file.FullName)" -ForegroundColor Cyan
    & $signtool @args
    if ($LASTEXITCODE -ne 0) {
        throw "signtool failed for $($file.FullName)"
    }

    [void](Assert-SignatureValid `
        -Path $file.FullName `
        -ExpectedThumbprint $cert.Thumbprint)
}

if ($IncludePowershellScripts) {
    $psFiles = @(
        Get-ChildItem `
            -LiteralPath $resolvedRoot `
            -Recurse `
            -File `
            -Filter *.ps1 `
            -ErrorAction SilentlyContinue
    )
    foreach ($psFile in $psFiles) {
        if ($WhatIf) {
            Write-Host "[WhatIf] Set-AuthenticodeSignature $($psFile.FullName)"
            continue
        }

        $signed = Set-AuthenticodeSignature `
            -FilePath $psFile.FullName `
            -Certificate $cert

        if ($signed.Status -ne 'Valid') {
            throw "PowerShell script signature is not Valid: $($psFile.FullName) Status=$($signed.Status)"
        }
        Write-Host "Signed script $($psFile.FullName)" -ForegroundColor DarkCyan
    }
}

if (-not $WhatIf) {
    # Final pass: every executable image must be validly signed. Existing valid
    # third-party signatures are accepted; locally unsigned output is not.
    foreach ($file in $files) {
        $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
        if ($signature.Status -ne 'Valid') {
            throw "Final signature validation failed: $($file.FullName) Status=$($signature.Status)"
        }
    }
}

Write-Host 'Signing complete and signatures verified.' -ForegroundColor Green
