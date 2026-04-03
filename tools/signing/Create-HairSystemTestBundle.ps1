[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildOutputDir,
    [string]$OutDir = "$PSScriptRoot/../../dist",
    [string]$BundleName = 'HairSystem_TestBundle',
    [string]$CertPath = "$PSScriptRoot/output/hairgpu-internal-test-signing.cer",
    [switch]$IncludeGeneratedScaffold
)

$ErrorActionPreference = 'Stop'

$buildDir = (Resolve-Path $BuildOutputDir).Path
if (!(Test-Path $buildDir)) {
    throw "Build output directory not found: $BuildOutputDir"
}

if (!(Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

$stageRoot = Join-Path $OutDir "${BundleName}_stage"
$zipPath = Join-Path $OutDir "$BundleName.zip"

if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
New-Item -ItemType Directory -Path $stageRoot | Out-Null

Copy-Item -Path (Join-Path $buildDir '*') -Destination $stageRoot -Recurse -Force

if (Test-Path $CertPath) {
    Copy-Item -Path $CertPath -Destination (Join-Path $stageRoot (Split-Path $CertPath -Leaf)) -Force
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Copy-Item -Path (Join-Path $repoRoot 'tools\signing\Install-InternalTestCodeSigningCert.ps1') -Destination $stageRoot -Force

if ($IncludeGeneratedScaffold) {
    $scaffold = Join-Path $repoRoot 'generated_scaffold'
    if (Test-Path $scaffold) {
        Copy-Item -Path $scaffold -Destination (Join-Path $stageRoot 'generated_scaffold') -Recurse -Force
    }
}

$readmePath = Join-Path $stageRoot 'README_TEST_BUNDLE.txt'
@'
HairSystem test bundle
======================

1. If the bundle came from a downloaded ZIP, right-click the ZIP and choose Properties -> Unblock before extracting.
2. Run Install-InternalTestCodeSigningCert.ps1 to import the public test certificate.
3. Launch hairgpu_dx11_viewer.exe.

PowerShell example:
  powershell -ExecutionPolicy Bypass -File .\Install-InternalTestCodeSigningCert.ps1 -CertPath .\hairgpu-internal-test-signing.cer -Scope CurrentUser

Note:
- This bundle is intended for internal testing.
- Strictly managed App Control / AppLocker devices can still block it until the administrator allows the signer or app.
'@ | Set-Content -Encoding UTF8 $readmePath

Compress-Archive -Path (Join-Path $stageRoot '*') -DestinationPath $zipPath -CompressionLevel Optimal
Write-Host "Created bundle: $zipPath" -ForegroundColor Green
