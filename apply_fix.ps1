$ErrorActionPreference = "Stop"

function Replace-Block {
    param(
        [string]$Path,
        [string]$Old,
        [string]$New
    )

    if (-not (Test-Path $Path)) {
        throw "Missing file: $Path"
    }

    $Text = Get-Content -Raw -Encoding UTF8 $Path

    if ($Text.Contains($New)) {
        Write-Host "Already patched: $Path"
        return
    }

    if (-not $Text.Contains($Old)) {
        throw "Expected original block was not found in: $Path"
    }

    $Text = $Text.Replace($Old, $New)
    Set-Content -Path $Path -Value $Text -Encoding UTF8
    Write-Host "Patched: $Path"
}

Replace-Block `
    -Path "apps/dx11_viewer/src/GpuHairViewer.h" `
    -Old @'
        uint32_t m_builderGuideCount = 6000;
        uint32_t m_builderFollowersPerGuide = 16;
        uint32_t m_builderParticlesPerStrand = 128;
        std::wstring m_activePresetName = L"Rapunzel Preview";
'@ `
    -New @'
        uint32_t m_builderGuideCount = 2048;
        uint32_t m_builderFollowersPerGuide = 8;
        uint32_t m_builderParticlesPerStrand = 96;
        std::wstring m_activePresetName = L"DX11 Safe Preview";
'@

Replace-Block `
    -Path "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl" `
    -Old @'
        UINT DispatchGroups(UINT itemCount, UINT groupSize)
        {
            return (itemCount + groupSize - 1u) / groupSize;
        }
'@ `
    -New @'
        constexpr UINT kMaxD3D11DispatchGroupsPerDimension = D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;

        UINT DispatchGroups(UINT itemCount, UINT groupSize)
        {
            if (itemCount == 0u || groupSize == 0u)
            {
                return 0u;
            }

            const uint64_t groups =
                (static_cast<uint64_t>(itemCount) + static_cast<uint64_t>(groupSize) - 1ull) /
                static_cast<uint64_t>(groupSize);

            // D3D11 Dispatch(x,y,z) accepts at most 65535 thread groups in each dimension.
            // Without this guard, large preview assets such as 13,056,000 particles produce
            // DispatchGroups(13056000, 64) == 204000 and trigger
            // DEVICE_DISPATCH_THREADGROUPCOUNT_OVERFLOW.
            return static_cast<UINT>(std::min<uint64_t>(groups, kMaxD3D11DispatchGroupsPerDimension));
        }
'@

Write-Host "Patch applied."
