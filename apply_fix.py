from pathlib import Path

ROOT = Path.cwd()

def replace_block(path: Path, old: str, new: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"Missing file: {path}")
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"Already patched: {path}")
        return
    if old not in text:
        raise RuntimeError(f"Expected original block was not found in: {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"Patched: {path}")

replace_block(
    ROOT / "apps/dx11_viewer/src/GpuHairViewer.h",
    """        uint32_t m_builderGuideCount = 6000;
        uint32_t m_builderFollowersPerGuide = 16;
        uint32_t m_builderParticlesPerStrand = 128;
        std::wstring m_activePresetName = L"Rapunzel Preview";""",
    """        uint32_t m_builderGuideCount = 2048;
        uint32_t m_builderFollowersPerGuide = 8;
        uint32_t m_builderParticlesPerStrand = 96;
        std::wstring m_activePresetName = L"DX11 Safe Preview";""",
)

replace_block(
    ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl",
    """        UINT DispatchGroups(UINT itemCount, UINT groupSize)
        {
            return (itemCount + groupSize - 1u) / groupSize;
        }""",
    """        constexpr UINT kMaxD3D11DispatchGroupsPerDimension = D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;

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
        }""",
)

print("Patch applied.")
