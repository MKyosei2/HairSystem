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

function Get-NativeErrorCode {
    param([Parameter(Mandatory = $true)]$Exception)

    $cursor = $Exception
    while ($null -ne $cursor) {
        if ($cursor.PSObject.Properties.Name -contains 'NativeErrorCode') {
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

function Wait-HairSystemPostSignExecutionReadiness {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$ExpectedThumbprint,
        [ValidateRange(1, 300)][int]$RetryWindowSeconds = 90,
        [ValidateRange(1, 30)][int]$RetryDelaySeconds = 5,
        [ValidateRange(1, 120)][int]$AttemptTimeoutSeconds = 30
    )

    # 70.29: Windows can transiently reject CreateProcess immediately after a
    # newly built image is Authenticode-signed even though that exact image is
    # executable moments later.  Retry ONLY WinError 5 / Access Denied.  The
    # signed bytes and signer are frozen and revalidated before every attempt.
    # WinError 4551 and all non-Access-Denied failures remain hard failures.
    $expectedHash = (
        Get-FileHash -LiteralPath $Executable -Algorithm SHA256
    ).Hash.ToLowerInvariant()

    [void](Assert-SignatureValid `
        -Path $Executable `
        -ExpectedThumbprint $ExpectedThumbprint)

    $deadline = (Get-Date).AddSeconds($RetryWindowSeconds)
    $attempt = 0

    while ($true) {
        $attempt += 1

        $currentHash = (
            Get-FileHash -LiteralPath $Executable -Algorithm SHA256
        ).Hash.ToLowerInvariant()
        if ($currentHash -ne $expectedHash) {
            throw "HairSystem.exe changed during post-sign execution readiness. ExpectedSha256=$expectedHash ObservedSha256=$currentHash"
        }

        [void](Assert-SignatureValid `
            -Path $Executable `
            -ExpectedThumbprint $ExpectedThumbprint)

        try {
            $process = Start-Process `
                -FilePath $Executable `
                -ArgumentList @('--native-self-test') `
                -WorkingDirectory (Split-Path -Parent $Executable) `
                -PassThru `
                -ErrorAction Stop

            $exited = $process.WaitForExit($AttemptTimeoutSeconds * 1000)
            if (-not $exited) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                throw "Post-sign native self-test timed out after $AttemptTimeoutSeconds seconds."
            }

            $process.Refresh()
            if ($process.ExitCode -ne 0) {
                throw "Post-sign native self-test failed. ExitCode=$($process.ExitCode)"
            }

            Write-Host "Post-sign execution readiness PASS on attempt $attempt." -ForegroundColor Green
            return
        }
        catch {
            $nativeCode = Get-NativeErrorCode -Exception $_.Exception
            $message = $_.Exception.Message

            if ($nativeCode -eq 4551 -or $message -match '4551') {
                throw "Windows App Control / Code Integrity explicitly blocked the signed HairSystem.exe (WinError 4551). No retry/bypass is permitted. Original=$message"
            }

            $transientAccessDenied = (
                $nativeCode -eq 5 -or
                $message -match '(?i)access.*denied'
            )
            if (-not $transientAccessDenied) {
                throw
            }

            if ((Get-Date) -ge $deadline) {
                throw "Signed HairSystem.exe remained Access Denied for the full $RetryWindowSeconds-second readiness window. Sha256=$expectedHash Attempts=$attempt LastNativeErrorCode=$nativeCode LastMessage=$message"
            }

            $remainingSeconds = [math]::Max(
                0,
                [int][math]::Ceiling(($deadline - (Get-Date)).TotalSeconds)
            )
            Write-Warning (
                "Post-sign CreateProcess was transiently Access Denied. " +
                "The exact SHA-256 and signer remain frozen; retrying in $RetryDelaySeconds seconds. " +
                "Attempt=$attempt RemainingWindowSeconds=$remainingSeconds NativeErrorCode=$nativeCode"
            )
            Start-Sleep -Seconds $RetryDelaySeconds
        }
    }
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
    foreach ($file in $files) {
        $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
        if ($signature.Status -ne 'Valid') {
            throw "Final signature validation failed: $($file.FullName) Status=$($signature.Status)"
        }
    }

    $hairSystemViewer = @(
        $files | Where-Object { $_.Name -ieq 'HairSystem.exe' }
    ) | Select-Object -First 1

    if ($null -ne $hairSystemViewer) {
        Wait-HairSystemPostSignExecutionReadiness `
            -Executable $hairSystemViewer.FullName `
            -ExpectedThumbprint $cert.Thumbprint `
            -RetryWindowSeconds 90 `
            -RetryDelaySeconds 5 `
            -AttemptTimeoutSeconds 30
    }
}

Write-Host 'Signing complete and signatures verified.' -ForegroundColor Green
