$ErrorActionPreference = "Stop"

$Header = "apps/dx11_viewer/src/GpuHairViewer.h"
$Part = "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl"

if (-not (Test-Path $Header)) { throw "Missing $Header" }
if (-not (Test-Path $Part)) { throw "Missing $Part" }

$H = Get-Content -Raw -Encoding UTF8 $Header
$P = Get-Content -Raw -Encoding UTF8 $Part

$Checks = @(
    @("Guide count fixed", $H.Contains("m_builderGuideCount = 2048")),
    @("Followers fixed", $H.Contains("m_builderFollowersPerGuide = 8")),
    @("Particles fixed", $H.Contains("m_builderParticlesPerStrand = 96")),
    @("Preset fixed", $H.Contains('L"DX11 Safe Preview"')),
    @("D3D11 max guard exists", $P.Contains("D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION")),
    @("DispatchGroups uses uint64_t", $P.Contains("const uint64_t groups")),
    @("DispatchGroups clamps", $P.Contains("std::min<uint64_t>"))
)

$Failed = $false
foreach ($Check in $Checks) {
    if ($Check[1]) {
        Write-Host "[OK] $($Check[0])"
    } else {
        Write-Host "[NG] $($Check[0])"
        $Failed = $true
    }
}

if ($Failed) {
    throw "Verification failed."
}

Write-Host "Verification passed."
