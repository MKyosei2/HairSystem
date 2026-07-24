#!/usr/bin/env python3
from __future__ import annotations

import base64
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / ".artifact_chunks"
ZIP_NAME = "HairSystem_prenovelty_visual_stability_fix_only.zip"
TARGETS = [
    Path("apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl"),
    Path("apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"),
    Path("apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl"),
    Path("apps/dx11_viewer/src/GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl"),
    Path("apps/dx11_viewer/src/GpuHairViewer_parts/07_draw_path_and_gpu_timing.inl"),
    Path("apps/dx11_viewer/shaders/HairStrandInstanced.hlsl"),
]


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one {label} match, observed {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def validate_cpp_balance(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count("{") != text.count("}"):
        raise RuntimeError(f"{path}: brace count changed unexpectedly")


def apply_fixes() -> None:
    setup = ROOT / TARGETS[0]
    replace_once(
        setup,
        'if (name.rfind(L"sample_hybrid_cache_v1", 0u) == 0u ||\n'
        '                        name.rfind(L"sample_hybrid_cache_v2", 0u) == 0u)',
        'if (name.rfind(L"sample_hybrid_cache_v1", 0u) == 0u ||\n'
        '                        name.rfind(L"sample_hybrid_cache_v2", 0u) == 0u ||\n'
        '                        name.rfind(L"sample_hybrid_cache_v3", 0u) == 0u)',
        "legacy cache rejection",
    )
    replace_once(
        setup,
        'L"sample_hybrid_cache_v3_runtime_verified_g" + std::to_wstring(m_builderGuideCount)',
        'L"sample_hybrid_cache_v4_visual_centered_g" + std::to_wstring(m_builderGuideCount)',
        "interactive cache schema",
    )
    replace_once(
        setup,
        '        m_widthScale = 0.82f;\n'
        '        m_renderMode = RenderMode::StrandRibbon;\n'
        '        // Stable startup is a reviewable simulation view, never an implicit edit\n'
        '        // session inherited from the inline Edit Stack.\n'
        '        m_activePresetName = L"Stable Benchmark Wig";\n'
        '        m_workspaceMode = WorkspaceMode::Sim;\n'
        '        m_interactionMode = InteractionMode::View;\n'
        '        m_groomTool = GroomTool::Select;',
        '        // The 100k-strand preview uses narrower authored ribbons than the old\n'
        '        // emergency visibility preset. Keeping the fixed workload intact while\n'
        '        // reducing width prevents the fringe from becoming a solid plate.\n'
        '        m_widthScale = 0.66f;\n'
        '        m_renderMode = RenderMode::StrandRibbon;\n'
        '        // Stable startup is a reviewable simulation view, never an implicit edit\n'
        '        // session inherited from the inline Edit Stack. The editor panel starts\n'
        '        // collapsed so the canonical subject is centered in the full viewport.\n'
        '        m_activePresetName = L"Stable Benchmark Wig";\n'
        '        m_workspaceMode = WorkspaceMode::Sim;\n'
        '        m_interactionMode = InteractionMode::View;\n'
        '        m_showInspectorPane = false;\n'
        '        m_showBottomTray = false;\n'
        '        m_showAdvancedInspector = false;\n'
        '        m_fastGroomViewport = false;\n'
        '        m_groomTool = GroomTool::Select;',
        "stable startup presentation",
    )
    replace_once(
        setup,
        '        m_scalpCoverageStrength = 0.92f;\n'
        '        m_scalpTintDarkness = 0.48f;\n'
        '        m_scalpHairBlend = 0.62f;',
        '        // A slightly darker, better blended scalp closes sub-pixel root gaps\n'
        '        // without changing strand count, topology, simulation, or formal data.\n'
        '        m_scalpCoverageStrength = 1.04f;\n'
        '        m_scalpTintDarkness = 0.58f;\n'
        '        m_scalpHairBlend = 0.76f;',
        "scalp visual coverage",
    )

    runtime = ROOT / TARGETS[1]
    replace_once(
        runtime,
        'runtimeProof << "buildId=HAIRGPU_BUILD_V9_20260724_FORCE_APPLIED\\n";',
        'runtimeProof << "buildId=HAIRGPU_BUILD_V10_20260724_VISUAL_STABILITY\\n";',
        "runtime build identity",
    )
    replace_once(
        runtime,
        'runtimeProof << "assetCacheSchema=v3_runtime_verified\\n";',
        'runtimeProof << "assetCacheSchema=v4_visual_centered\\n";',
        "runtime cache identity",
    )
    replace_once(
        runtime,
        '        const uint32_t localDebugCaptureFrames = localDebugCaptureEnabled\n'
        '            ? std::max<uint32_t>(EnvironmentValueToUInt(\n'
        '                GetEnvironmentVariableString(L"HAIRGPU_LOCAL_DEBUG_CAPTURE_FRAMES"),\n'
        '                1800u), 1u)\n'
        '            : 0u;\n'
        '        m_localDebugCaptureActive = localDebugCaptureEnabled;',
        '        const uint32_t localDebugCaptureFrames = localDebugCaptureEnabled\n'
        '            ? std::max<uint32_t>(EnvironmentValueToUInt(\n'
        '                GetEnvironmentVariableString(L"HAIRGPU_LOCAL_DEBUG_CAPTURE_FRAMES"),\n'
        '                1800u), 1u)\n'
        '            : 0u;\n'
        '        // Normal interactive title statistics begin only after resources, the\n'
        '        // first presents, and driver lazy initialization have settled. Formal\n'
        '        // and local-debug evidence keep their existing explicit warm-up rules.\n'
        '        const uint32_t interactiveCadenceWarmupFrames =\n'
        '            (!IsExperimentModeEnabled() && !localDebugCaptureEnabled) ? 120u : 0u;\n'
        '        uint32_t interactiveCadenceWarmupRemaining = interactiveCadenceWarmupFrames;\n'
        '        m_localDebugCaptureActive = localDebugCaptureEnabled;',
        "interactive cadence warmup declaration",
    )
    replace_once(
        runtime,
        '            m_sessionWallSeconds += static_cast<double>(wallFrameSeconds);\n'
        '            ++m_sessionCompletedFrameCount;\n'
        '            m_sessionEffectiveFps =\n'
        '                m_sessionWallSeconds > 1.0e-9\n'
        '                ? static_cast<float>(\n'
        '                    static_cast<double>(m_sessionCompletedFrameCount) /\n'
        '                    m_sessionWallSeconds)\n'
        '                : 0.0f;\n'
        '            m_recentFrameIntervalsMilliseconds[m_recentFrameIntervalWriteIndex] =\n'
        '                m_lastFrameIntervalMilliseconds;\n'
        '            m_recentFrameIntervalWriteIndex =\n'
        '                (m_recentFrameIntervalWriteIndex + 1u) % kRealtimeFrameHistoryCapacity;\n'
        '            m_recentFrameIntervalCount = std::min(\n'
        '                m_recentFrameIntervalCount + 1u,\n'
        '                kRealtimeFrameHistoryCapacity);\n'
        '            if (m_lastFrameIntervalMilliseconds >= 100.0f)\n'
        '            {\n'
        '                ++m_observedStallCount100Milliseconds;\n'
        '            }\n'
        '            m_statsUpdateAccumulator += wallFrameSeconds;',
        '            if (interactiveCadenceWarmupRemaining > 0u)\n'
        '            {\n'
        '                --interactiveCadenceWarmupRemaining;\n'
        '                m_sessionWallSeconds = 0.0;\n'
        '                m_sessionCompletedFrameCount = 0ull;\n'
        '                m_sessionEffectiveFps = 0.0f;\n'
        '                m_recentFrameIntervalsMilliseconds.fill(0.0f);\n'
        '                m_recentFrameIntervalWriteIndex = 0u;\n'
        '                m_recentFrameIntervalCount = 0u;\n'
        '                m_recentEffectiveFps = 0.0f;\n'
        '                m_recentOnePercentLowFps = 0.0f;\n'
        '                m_recentFrameP95Milliseconds = 0.0f;\n'
        '                m_recentFrameWorstMilliseconds = 0.0f;\n'
        '                m_observedStallCount100Milliseconds = 0u;\n'
        '            }\n'
        '            else\n'
        '            {\n'
        '                m_sessionWallSeconds += static_cast<double>(wallFrameSeconds);\n'
        '                ++m_sessionCompletedFrameCount;\n'
        '                m_sessionEffectiveFps =\n'
        '                    m_sessionWallSeconds > 1.0e-9\n'
        '                    ? static_cast<float>(\n'
        '                        static_cast<double>(m_sessionCompletedFrameCount) /\n'
        '                        m_sessionWallSeconds)\n'
        '                    : 0.0f;\n'
        '                m_recentFrameIntervalsMilliseconds[m_recentFrameIntervalWriteIndex] =\n'
        '                    m_lastFrameIntervalMilliseconds;\n'
        '                m_recentFrameIntervalWriteIndex =\n'
        '                    (m_recentFrameIntervalWriteIndex + 1u) % kRealtimeFrameHistoryCapacity;\n'
        '                m_recentFrameIntervalCount = std::min(\n'
        '                    m_recentFrameIntervalCount + 1u,\n'
        '                    kRealtimeFrameHistoryCapacity);\n'
        '                if (m_lastFrameIntervalMilliseconds >= 100.0f)\n'
        '                {\n'
        '                    ++m_observedStallCount100Milliseconds;\n'
        '                }\n'
        '            }\n'
        '            m_statsUpdateAccumulator += wallFrameSeconds;',
        "interactive cadence acceptance",
    )

    camera = ROOT / TARGETS[2]
    replace_once(
        camera,
        '(m_assetBoundsRadius / std::tan(limitingHalfFov)) * 1.08f;',
        '(m_assetBoundsRadius / std::tan(limitingHalfFov)) * 0.98f;',
        "camera framing margin",
    )
    replace_once(
        camera,
        'm_cameraDistance = Clamp(desiredDistance, 0.92f, 1.12f);',
        'm_cameraDistance = Clamp(desiredDistance, 0.84f, 1.00f);',
        "canonical camera distance",
    )

    title = ROOT / TARGETS[3]
    replace_once(
        title,
        'ss << L"[HAIRGPU V9 FORCE-APPLIED] Hair GPU Viewer"',
        'ss << L"[HAIRGPU V10 VISUAL-STABLE] Hair GPU Viewer"',
        "window title identity",
    )

    layout = ROOT / TARGETS[4]
    replace_once(
        layout,
        '        RECT client = {};\n'
        '        GetClientRect(m_hwnd, &client);\n'
        '        const int panelX = (std::max)(static_cast<int>(client.right) - kUiPanelWidth, 0);\n'
        '        const int panelHeight = (std::max)(static_cast<int>(client.bottom), 1);\n'
        '        MoveWindow(m_uiPanel, panelX, 0, kUiPanelWidth, panelHeight, TRUE);',
        '        RECT client = {};\n'
        '        GetClientRect(m_hwnd, &client);\n'
        '        // Sim/View starts as an uncluttered full-width review viewport. The\n'
        '        // controls are moved outside the client area while the panel is hidden,\n'
        '        // so direct child controls cannot leave a blank 500-pixel reservation.\n'
        '        const bool showUiPanel =\n'
        '            IsGroomWorkspace() || m_showInspectorPane || m_showBottomTray;\n'
        '        ShowWindow(m_uiPanel, showUiPanel ? SW_SHOW : SW_HIDE);\n'
        '        const int panelX = showUiPanel\n'
        '            ? (std::max)(static_cast<int>(client.right) - kUiPanelWidth, 0)\n'
        '            : static_cast<int>(client.right);\n'
        '        const int panelHeight = (std::max)(static_cast<int>(client.bottom), 1);\n'
        '        MoveWindow(m_uiPanel, panelX, 0, kUiPanelWidth, panelHeight, TRUE);',
        "full-width viewport panel handling",
    )

    shader = ROOT / TARGETS[5]
    replace_once(
        shader,
        '// HAIRGPU_STRAND_RIBBON_VISIBILITY_V17_SEPARATE_RENDER_AND_EDIT',
        '// HAIRGPU_STRAND_RIBBON_VISIBILITY_V18_NATURAL_100K_WIDTH',
        "shader version marker",
    )
    replace_once(
        shader,
        '    // Approximately two pixels at a 60-degree vertical FOV, independent of\n'
        '    // camera distance. This avoids point/dash rendering after auto framing.\n'
        '    const float distanceScaledHalfWidth = cameraDistance * 0.00125f;\n'
        '    const float authoredHalfWidth = authoredWidth * (isGuide ? 0.22f : 0.50f);\n'
        '    float halfWidth = max(authoredHalfWidth, distanceScaledHalfWidth);\n'
        '    halfWidth = min(halfWidth, cameraDistance * 0.0030f);\n'
        '\n'
        '    const float rootMask = 1.0f - smoothstep(0.0f, 0.10f, u);\n'
        '    const float tipTaper = lerp(1.0f, 0.34f, smoothstep(0.70f, 1.0f, u));\n'
        '    halfWidth *= lerp(1.12f, 1.0f, 1.0f - rootMask) * tipTaper;',
        '    // Keep approximately one visible pixel at a 60-degree vertical FOV.\n'
        '    // The previous two-pixel floor on all 100,000 ribbons merged neighboring\n'
        '    // followers into opaque sheets, especially across the fringe and crown.\n'
        '    const float distanceScaledHalfWidth = cameraDistance * 0.00072f;\n'
        '    const float authoredHalfWidth = authoredWidth * (isGuide ? 0.20f : 0.34f);\n'
        '    float halfWidth = max(authoredHalfWidth, distanceScaledHalfWidth);\n'
        '    halfWidth = min(halfWidth, cameraDistance * 0.0019f);\n'
        '    const float strandWidthVariation =\n'
        '        lerp(0.78f, 1.04f, Hash01(strandIndex * 2246822519u + 3266489917u));\n'
        '\n'
        '    const float rootMask = 1.0f - smoothstep(0.0f, 0.08f, u);\n'
        '    const float tipTaper = lerp(1.0f, 0.24f, smoothstep(0.68f, 1.0f, u));\n'
        '    halfWidth *= strandWidthVariation *\n'
        '        lerp(1.06f, 1.0f, 1.0f - rootMask) * tipTaper;',
        "natural ribbon width",
    )

    for relative in TARGETS[:-1]:
        validate_cpp_balance(ROOT / relative)

    required_tokens = {
        TARGETS[0]: ["sample_hybrid_cache_v4_visual_centered", "m_showInspectorPane = false"],
        TARGETS[1]: ["interactiveCadenceWarmupRemaining", "HAIRGPU_BUILD_V10_20260724_VISUAL_STABILITY"],
        TARGETS[2]: ["Clamp(desiredDistance, 0.84f, 1.00f)"],
        TARGETS[3]: ["HAIRGPU V10 VISUAL-STABLE"],
        TARGETS[4]: ["const bool showUiPanel"],
        TARGETS[5]: ["HAIRGPU_STRAND_RIBBON_VISIBILITY_V18_NATURAL_100K_WIDTH", "strandWidthVariation"],
    }
    for relative, tokens in required_tokens.items():
        text = (ROOT / relative).read_text(encoding="utf-8")
        for token in tokens:
            if token not in text:
                raise RuntimeError(f"{relative}: validation token missing: {token}")


def build_archive() -> None:
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    OUTPUT.mkdir(parents=True)
    archive_path = OUTPUT / ZIP_NAME
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for relative in TARGETS:
            archive.write(ROOT / relative, relative.as_posix())

    payload = archive_path.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    encoded = base64.b64encode(payload).decode("ascii")
    chunk_size = 12000
    chunks = [encoded[i : i + chunk_size] for i in range(0, len(encoded), chunk_size)]
    archive_path.unlink()
    for index, chunk in enumerate(chunks):
        (OUTPUT / f"part_{index:03d}.b64").write_text(chunk, encoding="ascii")
    manifest = {
        "schemaVersion": "hairgpu-chatgpt-artifact-chunks-v1",
        "zipName": ZIP_NAME,
        "zipSha256": digest,
        "zipSizeBytes": len(payload),
        "base64Length": len(encoded),
        "chunkSize": chunk_size,
        "chunkCount": len(chunks),
        "files": [path.as_posix() for path in TARGETS],
    }
    (OUTPUT / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    apply_fixes()
    build_archive()
