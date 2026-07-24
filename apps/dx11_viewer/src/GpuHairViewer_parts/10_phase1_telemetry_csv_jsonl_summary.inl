// GpuHairViewer.cpp ordered part 10: phase1_telemetry_csv_jsonl_summary
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.
// HAIRGPU_PRENOVELTY_TELEMETRY_CONTRACT_V4:
// preserve measured-zero versus unavailable values, require delayed GPU timing,
// and reject synthetic metrics or same-frame GPU readbacks in formal evidence.

std::swap(stack.layers[static_cast<size_t>(selectedIndex)], stack.layers[static_cast<size_t>(targetIndex)]);
const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
LoadQuickEditFormFromInlineText(targetIndex);
if (applyAfterMove)
{
    return ApplyInlineEditStackFromUi(true);
}
return true;
    }

    bool GpuHairViewer::ToggleSelectedInlineLayerEnabled(bool applyAfterToggle)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            return false;
        }

        const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
        hairgpu::EditLayer& layer = stack.layers[static_cast<size_t>(selectedIndex)];
        layer.enabled = !layer.enabled;
        m_quickEditEnabled = layer.enabled;
        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        LoadQuickEditFormFromInlineText(selectedIndex);
        if (applyAfterToggle)
        {
            return ApplyInlineEditStackFromUi(true);
        }
        return true;
    }

    bool GpuHairViewer::DuplicateSelectedInlineLayer(bool applyAfterDuplicate)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            return false;
        }

        const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
        hairgpu::EditLayer duplicate = stack.layers[static_cast<size_t>(selectedIndex)];
        duplicate.name += "_copy";
        stack.layers.insert(stack.layers.begin() + selectedIndex + 1, duplicate);
        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        LoadQuickEditFormFromInlineText(selectedIndex + 1);
        if (applyAfterDuplicate)
        {
            return ApplyInlineEditStackFromUi(true);
        }
        return true;
    }

    bool GpuHairViewer::DeleteSelectedInlineLayer(bool applyAfterDelete)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            return false;
        }

        const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
        stack.layers.erase(stack.layers.begin() + selectedIndex);
        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        const int nextSelection = stack.layers.empty() ? -1 : std::min<int>(selectedIndex, static_cast<int>(stack.layers.size()) - 1);
        LoadQuickEditFormFromInlineText(nextSelection);
        if (applyAfterDelete)
        {
            return ApplyInlineEditStackFromUi(true);
        }
        return true;
    }

    bool GpuHairViewer::BrowseAndSetMaskPath(bool partingMask)
    {
        wchar_t fileName[MAX_PATH] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"Mask Files (*.bmp;*.pgm;*.ppm;*.csv)\0*.bmp;*.pgm;*.ppm;*.csv\0Bitmap (*.bmp)\0*.bmp\0Portable Graymap (*.pgm)\0*.pgm\0Portable Pixmap (*.ppm)\0*.ppm\0CSV (*.csv)\0*.csv\0All Files\0*.*\0";
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&ofn))
        {
            return false;
        }
        if (partingMask)
        {
            m_partingMaskPath = fileName;
        }
        else
        {
            m_regionMaskPath = fileName;
        }
        RefreshAssetStatsText();
        RefreshMaskPreviewControls();
        return true;
    }

    void GpuHairViewer::ApplySampleMaskOverrides()
    {
        std::string parting = WideToUtf8(m_partingMaskPath);
        std::string region = WideToUtf8(m_regionMaskPath);
        hairgpu::SetSampleMaskOverridePaths(parting.empty() ? nullptr : parting.c_str(), region.empty() ? nullptr : region.c_str());
        hairgpu::SetSampleMaskSourceMode(static_cast<hairgpu::SampleMaskSourceMode>(m_maskSourceMode));
    }

    void GpuHairViewer::ReleaseMaskPreviewBitmaps()
    {
        if (m_partingPreviewBitmap) { DeleteObject(m_partingPreviewBitmap); m_partingPreviewBitmap = nullptr; }
        if (m_regionPreviewBitmap) { DeleteObject(m_regionPreviewBitmap); m_regionPreviewBitmap = nullptr; }
    }

    void GpuHairViewer::RefreshMaskPreviewControls()
    {
        ReleaseMaskPreviewBitmaps();
        std::wstring partingLabel = L"Parting preview: ";
        std::wstring regionLabel = L"Region preview: ";
        if (m_maskSourceMode == MaskSourceMode::Procedural)
        {
            partingLabel += L"procedural";
            regionLabel += L"procedural";
        }
        else
        {
            partingLabel += m_partingMaskPath.empty() ? (m_maskSourceMode == MaskSourceMode::Auto ? L"auto" : L"none") : std::filesystem::path(m_partingMaskPath).filename().wstring();
            regionLabel += m_regionMaskPath.empty() ? (m_maskSourceMode == MaskSourceMode::Auto ? L"auto" : L"none") : std::filesystem::path(m_regionMaskPath).filename().wstring();
        }
        if (m_labelPartingMask) SetWindowTextW(m_labelPartingMask, partingLabel.c_str());
        if (m_labelRegionMask) SetWindowTextW(m_labelRegionMask, regionLabel.c_str());

        if (m_maskSourceMode != MaskSourceMode::Procedural)
        {
            PreviewMaskImage partImg, regionImg;
            std::wstring effectiveParting = m_partingMaskPath;
            std::wstring effectiveRegion = m_regionMaskPath;
            if (m_maskSourceMode == MaskSourceMode::Auto)
            {
                if (effectiveParting.empty()) effectiveParting = ResolveViewerDefaultMaskPath(L"scalp_parting_mask");
                if (effectiveRegion.empty()) effectiveRegion = ResolveViewerDefaultMaskPath(L"scalp_region_mask");
            }
            if (!effectiveParting.empty() && LoadPreviewMaskAny(effectiveParting, 1u, partImg))
            {
                m_partingPreviewBitmap = CreatePreviewBitmapFromMask(partImg, true);
                if (m_partingMaskPath.empty()) partingLabel = L"Parting preview: auto file";
            }
            if (!effectiveRegion.empty() && LoadPreviewMaskAny(effectiveRegion, 3u, regionImg))
            {
                m_regionPreviewBitmap = CreatePreviewBitmapFromMask(regionImg, false);
                if (m_regionMaskPath.empty()) regionLabel = L"Region preview: auto file";
            }
        }
        SendMessageW(m_previewPartingMask, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(m_partingPreviewBitmap));
        SendMessageW(m_previewRegionMask, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(m_regionPreviewBitmap));
        UpdateScalpOverlayVertexBuffer();
    }

    void GpuHairViewer::ShowMaskPreviewPopup(bool partingMask)
    {
        HBITMAP bitmap = partingMask ? m_partingPreviewBitmap : m_regionPreviewBitmap;
        if (!bitmap)
        {
            MessageBoxW(m_hwnd, L"No preview bitmap is available for this mask.", L"Mask Preview", MB_OK | MB_ICONINFORMATION);
            return;
        }
        const wchar_t* title = partingMask ? L"Parting Mask Preview" : L"Region Mask Preview";
        HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
            CW_USEDEFAULT, CW_USEDEFAULT, 420, 420, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (popup)
        {
            SendMessageW(popup, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmap));
            ShowWindow(popup, SW_SHOW);
            UpdateWindow(popup);
        }
    }

    void GpuHairViewer::EvaluateScalpCoverageTint(
        float u,
        float v,
        float& r,
        float& g,
        float& b) const
    {
        auto wrapSigned = [](float value)
        {
            while (value > 0.5f)
            {
                value -= 1.0f;
            }
            while (value < -0.5f)
            {
                value += 1.0f;
            }
            return value;
        };

        // The scalp mesh is a hair-cap underlayer in the normal preview. Match it
        // to the root colour so tiny raster/depth gaps do not appear as a large
        // skin-coloured bald patch. This changes rendering only, not simulation.
        const float dx = wrapSigned(u - 0.50f);
        const float crownDy = v - 0.085f;
        const float crown = hairgpu::Clamp(
            1.0f -
                std::sqrt(
                    dx * dx * 2.0f +
                    crownDy * crownDy * 1.35f) /
                    0.30f,
            0.0f,
            1.0f);

        const float partWidth =
            std::max(m_scalpPartLineWidth, 1.0e-4f);
        const float partLine =
            std::exp(
                -(dx * dx) /
                std::max(partWidth * partWidth, 1.0e-5f)) *
            std::exp(
                -(crownDy * crownDy) / 0.028f);

        float frontDu = std::fabs(u - 0.25f);
        frontDu = std::min(frontDu, 1.0f - frontDu);
        const float frontHairline =
            std::exp(
                -((v - 0.27f) * (v - 0.27f)) /
                0.020f) *
            hairgpu::Clamp(
                1.0f - frontDu / 0.11f,
                0.0f,
                1.0f);

        const float faceU =
            hairgpu::Clamp(1.0f - frontDu / 0.18f, 0.0f, 1.0f);
        const float faceTop =
            hairgpu::Clamp((v - 0.17f) / 0.13f, 0.0f, 1.0f);
        const float faceBottom =
            hairgpu::Clamp((0.88f - v) / 0.16f, 0.0f, 1.0f);
        const float mannequinFace = faceU * faceTop * faceBottom;

        const float rawCoverage =
            0.76f +
            0.30f * crown +
            0.16f * frontHairline -
            0.035f * partLine;
        // Keep the crown as a dark hair-cap underlayer, but render the front/lower
        // head as a visible mannequin. A uniformly dark full head looked like a
        // giant hole whenever the camera orbited above or around the face opening.
        const float coverage = hairgpu::Clamp(
            rawCoverage * m_scalpCoverageStrength * (1.0f - 0.94f * mannequinFace),
            0.0f,
            1.0f);

        const float darkness = hairgpu::Clamp(
            m_scalpTintDarkness,
            0.0f,
            1.0f);
        const float hairBlend = hairgpu::Clamp(
            m_scalpHairBlend,
            0.0f,
            1.0f);

        const float scalpR = 0.56f * (1.0f - darkness) + 0.34f * darkness;
        const float scalpG = 0.40f * (1.0f - darkness) + 0.24f * darkness;
        const float scalpB = 0.30f * (1.0f - darkness) + 0.17f * darkness;

        const float hairR = 0.24f * (1.0f - hairBlend) + 0.17f * hairBlend;
        const float hairG = 0.16f * (1.0f - hairBlend) + 0.105f * hairBlend;
        const float hairB = 0.090f * (1.0f - hairBlend) + 0.055f * hairBlend;

        const float partLight = hairgpu::Clamp(
            0.02f + 0.08f * partLine,
            0.0f,
            0.10f);

        r = scalpR * (1.0f - coverage) +
            hairR * coverage +
            partLight * 0.035f;
        g = scalpG * (1.0f - coverage) +
            hairG * coverage +
            partLight * 0.025f;
        b = scalpB * (1.0f - coverage) +
            hairB * coverage +
            partLight * 0.015f;

        r = hairgpu::Clamp(r, 0.0f, 1.0f);
        g = hairgpu::Clamp(g, 0.0f, 1.0f);
        b = hairgpu::Clamp(b, 0.0f, 1.0f);
    }

    void GpuHairViewer::UpdateScalpOverlayVertexBuffer()
    {
        if (!m_scalpVertexBuffer || m_asset.scalpMesh.positions.empty())
        {
            return;
        }

        PreviewMaskImage partImg;
        PreviewMaskImage regionImg;
        std::wstring effectiveParting = m_partingMaskPath;
        std::wstring effectiveRegion = m_regionMaskPath;
        if (m_maskSourceMode == MaskSourceMode::Auto)
        {
            if (effectiveParting.empty()) effectiveParting = ResolveViewerDefaultMaskPath(L"scalp_parting_mask");
            if (effectiveRegion.empty()) effectiveRegion = ResolveViewerDefaultMaskPath(L"scalp_region_mask");
        }
        if (!effectiveParting.empty()) LoadPreviewMaskAny(effectiveParting, 1u, partImg);
        if (!effectiveRegion.empty()) LoadPreviewMaskAny(effectiveRegion, 3u, regionImg);

        std::vector<Vec2> guideRootUvs;
        std::vector<float> guideRootInfluence;
        std::vector<float> guideRootRisk;
        std::vector<uint8_t> guideRootActive;
        bool usingSelectedLayerPreview = false;
        const bool needsGuideOverlayData = IsEditInfluenceOverlayMode() || IsActiveSetRiskOverlayMode();
        if (needsGuideOverlayData)
        {
            if (IsEditInfluenceOverlayMode())
            {
                uint32_t previewGuideCount = 0u;
                float previewMaxInfluence = 0.0f;
                usingSelectedLayerPreview = TryBuildSelectedInlineLayerPreview(guideRootInfluence, previewGuideCount, previewMaxInfluence);
                if (!usingSelectedLayerPreview)
                {
                    guideRootInfluence = m_runtimeEditState.guideInfluence;
                }
            }

            uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
            if (IsEditInfluenceOverlayMode() && !guideRootInfluence.empty())
            {
                guideCount = std::min<uint32_t>(guideCount, static_cast<uint32_t>(guideRootInfluence.size()));
            }
            if (guideCount > 0u)
            {
                guideRootUvs.reserve(guideCount);
                if (!guideRootInfluence.empty())
                {
                    guideRootInfluence.resize(guideCount);
                }
                if (IsActiveSetRiskOverlayMode())
                {
                    guideRootRisk.assign(guideCount, 0.0f);
                    guideRootActive.assign(guideCount, 0u);
                    for (const hairgpu::ActiveSetClusterScore& score : m_activeSetPlan.scores)
                    {
                        if (score.clusterIndex >= m_activeSetClusters.size())
                        {
                            continue;
                        }
                        const hairgpu::ActiveSetCluster& cluster = m_activeSetClusters[score.clusterIndex];
                        const float risk = GetActiveSetOverlayScoreValue(score);
                        for (uint32_t guideIndex : cluster.guideStrandIndices)
                        {
                            if (guideIndex >= guideCount)
                            {
                                continue;
                            }
                            guideRootRisk[guideIndex] = std::max(guideRootRisk[guideIndex], risk);
                            if (score.selected)
                            {
                                guideRootActive[guideIndex] = 1u;
                            }
                        }
                    }
                }

                for (uint32_t guideIndex = 0; guideIndex < guideCount; ++guideIndex)
                {
                    Vec2 uv = { 0.5f, 0.5f };
                    if (guideIndex < m_asset.rootBindings.size())
                    {
                        uv = m_asset.rootBindings[guideIndex].rootUv;
                    }
                    else if (guideIndex < m_asset.strands.size() && m_asset.strands[guideIndex].pointCount > 0u)
                    {
                        const Vec3 root = m_asset.restPoints[m_asset.strands[guideIndex].pointOffset];
                        const Vec3 previewUv = ComputeScalpPreviewUv(root);
                        uv = { previewUv.x, previewUv.y };
                    }
                    guideRootUvs.push_back(uv);
                }
            }
        }

        std::vector<ScalpVertex> scalpVertices;
        scalpVertices.reserve(m_asset.scalpMesh.positions.size());
        for (size_t i = 0; i < m_asset.scalpMesh.positions.size(); ++i)
        {
            const Vec3 p = m_asset.scalpMesh.positions[i];
            const Vec3 n = (i < m_asset.scalpMesh.normals.size()) ? m_asset.scalpMesh.normals[i] : Vec3(0.0f, 1.0f, 0.0f);
            float r = 0.76f, g = 0.62f, b = 0.52f;
            const Vec3 uvw = ComputeScalpPreviewUv(p);
            EvaluateScalpCoverageTint(uvw.x, uvw.y, r, g, b);
            if (m_maskOverlayMode == MaskOverlayMode::Parting)
            {
                const float maskValue = (partImg.width > 0) ? SamplePreviewMaskScalar(partImg, uvw.x, uvw.y, 0) : ProceduralPartingPreview(uvw.x, uvw.y);
                r = 0.28f + 0.72f * maskValue;
                g = 0.20f + 0.38f * (1.0f - maskValue);
                b = 0.55f * (1.0f - maskValue);
            }
            else if (m_maskOverlayMode == MaskOverlayMode::Region)
            {
                Vec3 region = (regionImg.width > 0) ? Vec3(SamplePreviewMaskScalar(regionImg, uvw.x, uvw.y, 0), SamplePreviewMaskScalar(regionImg, uvw.x, uvw.y, 1), SamplePreviewMaskScalar(regionImg, uvw.x, uvw.y, 2)) : ProceduralRegionPreview(uvw.x, uvw.y);
                r = 0.20f + 0.80f * region.x;
                g = 0.20f + 0.80f * region.y;
                b = 0.20f + 0.80f * region.z;
            }
            else if (IsEditInfluenceOverlayMode() && !guideRootUvs.empty())
            {
                float bestDistSq = std::numeric_limits<float>::max();
                float influence = 0.0f;
                float nearestSelectedDistSq = std::numeric_limits<float>::max();
                float nearestOutsideDistSq = std::numeric_limits<float>::max();
                for (size_t guideIndex = 0; guideIndex < guideRootUvs.size(); ++guideIndex)
                {
                    const float du = uvw.x - guideRootUvs[guideIndex].x;
                    const float dv = uvw.y - guideRootUvs[guideIndex].y;
                    const float distSq = du * du + dv * dv;
                    const float guideInfluence = hairgpu::Clamp(guideRootInfluence[guideIndex], 0.0f, 1.0f);
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        influence = guideInfluence;
                    }
                    if (guideInfluence >= 0.01f)
                    {
                        nearestSelectedDistSq = std::min(nearestSelectedDistSq, distSq);
                    }
                    else
                    {
                        nearestOutsideDistSq = std::min(nearestOutsideDistSq, distSq);
                    }
                }
                influence = Clamp(influence, 0.0f, 1.0f);
                const float rawR = 0.18f + 0.82f * influence;
                const float rawG = 0.28f + 0.50f * (1.0f - influence);
                const float rawB = 0.92f - 0.74f * influence;
                float afterR = rawR;
                float afterG = rawG;
                float afterB = rawB;
                if (usingSelectedLayerPreview && nearestSelectedDistSq < std::numeric_limits<float>::max() && nearestOutsideDistSq < std::numeric_limits<float>::max())
                {
                    const float ds = std::sqrt(nearestSelectedDistSq);
                    const float dof = std::sqrt(nearestOutsideDistSq);
                    const float denom = std::max(std::max(ds, dof), 1.0e-4f);
                    const float boundary = Clamp(1.0f - std::abs(ds - dof) / denom, 0.0f, 1.0f);
                    afterR = Clamp(afterR + 0.32f * boundary, 0.0f, 1.0f);
                    afterG = Clamp(afterG + 0.20f * boundary, 0.0f, 1.0f);
                    afterB = Clamp(afterB * (1.0f - 0.42f * boundary), 0.0f, 1.0f);
                }

                if (IsBoundaryBeforeAfterOverlayMode())
                {
                    const bool showAfter = uvw.x >= 0.5f;
                    r = showAfter ? afterR : rawR;
                    g = showAfter ? afterG : rawG;
                    b = showAfter ? afterB : rawB;

                    const float divider = Clamp(1.0f - std::abs(uvw.x - 0.5f) / 0.018f, 0.0f, 1.0f);
                    if (divider > 0.0f)
                    {
                        r = Clamp(r * (1.0f - 0.65f * divider) + 0.95f * divider, 0.0f, 1.0f);
                        g = Clamp(g * (1.0f - 0.65f * divider) + 0.95f * divider, 0.0f, 1.0f);
                        b = Clamp(b * (1.0f - 0.65f * divider) + 0.95f * divider, 0.0f, 1.0f);
                    }
                }
                else
                {
                    r = afterR;
                    g = afterG;
                    b = afterB;
                }
            }
            else if (IsActiveSetRiskOverlayMode() && !guideRootUvs.empty() && guideRootRisk.size() == guideRootUvs.size())
            {
                float bestDistSq = std::numeric_limits<float>::max();
                float risk = 0.0f;
                bool nearestActive = false;
                float nearestActiveDistSq = std::numeric_limits<float>::max();
                float nearestInactiveDistSq = std::numeric_limits<float>::max();
                for (size_t guideIndex = 0; guideIndex < guideRootUvs.size(); ++guideIndex)
                {
                    const float du = uvw.x - guideRootUvs[guideIndex].x;
                    const float dv = uvw.y - guideRootUvs[guideIndex].y;
                    const float distSq = du * du + dv * dv;
                    const float guideRisk = hairgpu::Clamp(guideRootRisk[guideIndex], 0.0f, 1.0f);
                    const bool guideActive = guideIndex < guideRootActive.size() && guideRootActive[guideIndex] != 0u;
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        risk = guideRisk;
                        nearestActive = guideActive;
                    }
                    if (guideActive)
                    {
                        nearestActiveDistSq = std::min(nearestActiveDistSq, distSq);
                    }
                    else
                    {
                        nearestInactiveDistSq = std::min(nearestInactiveDistSq, distSq);
                    }
                }
                risk = Clamp(risk, 0.0f, 1.0f);
                r = 0.16f + 0.84f * risk;
                g = 0.24f + 0.56f * (1.0f - risk);
                b = 0.96f - 0.82f * risk;
                if (m_maskOverlayMode == MaskOverlayMode::ActiveSetMotion)
                {
                    g = Clamp(g + 0.06f * risk, 0.0f, 1.0f);
                    b = Clamp(b + 0.04f * (1.0f - risk), 0.0f, 1.0f);
                }
                else if (m_maskOverlayMode == MaskOverlayMode::ActiveSetContact)
                {
                    r = Clamp(r + 0.12f * risk, 0.0f, 1.0f);
                    b = Clamp(b * (1.0f - 0.10f * risk), 0.0f, 1.0f);
                }
                else if (m_maskOverlayMode == MaskOverlayMode::ActiveSetUncertainty)
                {
                    r = Clamp(r * (1.0f - 0.10f * risk) + 0.12f * risk, 0.0f, 1.0f);
                    g = Clamp(g + 0.10f * (1.0f - risk), 0.0f, 1.0f);
                    b = Clamp(b + 0.08f, 0.0f, 1.0f);
                }
                else if (m_maskOverlayMode == MaskOverlayMode::ActiveSetEdit)
                {
                    r = Clamp(r + 0.08f * risk, 0.0f, 1.0f);
                    g = Clamp(g + 0.18f * risk, 0.0f, 1.0f);
                    b = Clamp(b * (1.0f - 0.18f * risk), 0.0f, 1.0f);
                }
                if (nearestActive)
                {
                    r = Clamp(r + 0.10f, 0.0f, 1.0f);
                    g = Clamp(g + 0.22f, 0.0f, 1.0f);
                    b = Clamp(b * 0.62f, 0.0f, 1.0f);
                }
                if (nearestActiveDistSq < std::numeric_limits<float>::max() && nearestInactiveDistSq < std::numeric_limits<float>::max())
                {
                    const float ds = std::sqrt(nearestActiveDistSq);
                    const float dof = std::sqrt(nearestInactiveDistSq);
                    const float denom = std::max(std::max(ds, dof), 1.0e-4f);
                    const float boundary = Clamp(1.0f - std::abs(ds - dof) / denom, 0.0f, 1.0f);
                    r = Clamp(r * (1.0f - 0.38f * boundary) + 0.98f * boundary, 0.0f, 1.0f);
                    g = Clamp(g * (1.0f - 0.22f * boundary) + 0.95f * boundary, 0.0f, 1.0f);
                    b = Clamp(b + 0.10f * boundary, 0.0f, 1.0f);
                }
            }
            ScalpVertex v = {};
            v.scalpIndex = static_cast<uint32_t>(i);
            v.nx = n.x;
            v.ny = n.y;
            v.nz = n.z;
            v.r = r;
            v.g = g;
            v.b = b;
            scalpVertices.push_back(v);
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_scalpVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, scalpVertices.data(), scalpVertices.size() * sizeof(ScalpVertex));
            m_context->Unmap(m_scalpVertexBuffer.Get(), 0);
        }
    }

    bool GpuHairViewer::ExportCurrentFrameCsv(const std::wstring& filePath)
    {
        std::vector<GpuParticle> particles;
        if (!ReadBackCurrentParticlePositions(particles))
        {
            return false;
        }

        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out)
        {
            return false;
        }

        out << "strandIndex,pointIndex,x,y,z,width,isGuide,parentGuide,followWeight,clumpWeight,noiseWeight\n";
        for (size_t strandIndex = 0; strandIndex < m_asset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = m_asset.strands[strandIndex];
            const bool isGuide = strandIndex < m_asset.guideStrandCount;
            const FollowData follow = (strandIndex < m_asset.followData.size()) ? m_asset.followData[strandIndex] : FollowData{};
            for (uint32_t i = 0; i < strand.pointCount; ++i)
            {
                const uint32_t particleIndex = strand.pointOffset + i;
                const GpuParticle& p = particles[particleIndex];
                const float width = (particleIndex < m_asset.pointWidths.size()) ? m_asset.pointWidths[particleIndex] : 0.0f;
                out << strandIndex << "," << i << "," << p.x << "," << p.y << "," << p.z << "," << width << ","
                    << (isGuide ? 1 : 0) << "," << follow.parentGuide << "," << follow.followWeight << "," << follow.clumpWeight << "," << follow.noiseWeight << "\n";
            }
        }

        return true;
    }


    namespace
    {
        struct FormalJsonValue
        {
            enum class Type { Null, Boolean, Number, String, Object, Array };
            Type type = Type::Null;
            bool booleanValue = false;
            double numberValue = 0.0;
            std::string stringValue;
            std::vector<std::pair<std::string, FormalJsonValue>> objectValues;
            std::vector<FormalJsonValue> arrayValues;

            const FormalJsonValue* Find(const char* key) const
            {
                if (type != Type::Object || key == nullptr) return nullptr;
                for (const auto& item : objectValues)
                    if (item.first == key) return &item.second;
                return nullptr;
            }
            const FormalJsonValue* FindAny(std::initializer_list<const char*> keys) const
            {
                for (const char* key : keys)
                    if (const FormalJsonValue* value = Find(key)) return value;
                return nullptr;
            }
            std::string String(std::initializer_list<const char*> keys, const std::string& fallback = {}) const
            {
                const FormalJsonValue* value = FindAny(keys);
                return value && value->type == Type::String ? value->stringValue : fallback;
            }
            bool Boolean(std::initializer_list<const char*> keys, bool fallback) const
            {
                const FormalJsonValue* value = FindAny(keys);
                if (!value) return fallback;
                if (value->type == Type::Boolean) return value->booleanValue;
                if (value->type == Type::Number) return value->numberValue != 0.0;
                if (value->type == Type::String)
                    return value->stringValue == "true" || value->stringValue == "1" || value->stringValue == "yes";
                return fallback;
            }
            uint32_t UInt(std::initializer_list<const char*> keys, uint32_t fallback) const
            {
                const FormalJsonValue* value = FindAny(keys);
                if (!value) return fallback;
                if (value->type == Type::Number && value->numberValue >= 0.0)
                    return static_cast<uint32_t>(value->numberValue);
                if (value->type == Type::String)
                {
                    try { return static_cast<uint32_t>(std::stoul(value->stringValue)); }
                    catch (...) { return fallback; }
                }
                return fallback;
            }
            float Float(std::initializer_list<const char*> keys, float fallback) const
            {
                const FormalJsonValue* value = FindAny(keys);
                if (!value) return fallback;
                if (value->type == Type::Number) return static_cast<float>(value->numberValue);
                if (value->type == Type::String)
                {
                    try { return std::stof(value->stringValue); }
                    catch (...) { return fallback; }
                }
                return fallback;
            }
        };

        class FormalJsonParser
        {
        public:
            explicit FormalJsonParser(const std::string& text) : m_text(text) {}
            bool Parse(FormalJsonValue& outValue, std::string& outError)
            {
                m_position = 0u;
                m_error.clear();
                SkipWhitespace();
                if (!ParseValue(outValue))
                {
                    outError = m_error.empty() ? "invalid JSON" : m_error;
                    return false;
                }
                SkipWhitespace();
                if (m_position != m_text.size())
                {
                    outError = "trailing data after JSON document";
                    return false;
                }
                return true;
            }
        private:
            const std::string& m_text;
            size_t m_position = 0u;
            std::string m_error;

            void SkipWhitespace()
            {
                while (m_position < m_text.size())
                {
                    const unsigned char c = static_cast<unsigned char>(m_text[m_position]);
                    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
                    ++m_position;
                }
            }
            bool Fail(const char* message)
            {
                std::ostringstream stream;
                stream << message << " at byte " << m_position;
                m_error = stream.str();
                return false;
            }
            bool Consume(char expected)
            {
                SkipWhitespace();
                if (m_position >= m_text.size() || m_text[m_position] != expected) return false;
                ++m_position;
                return true;
            }
            bool ParseValue(FormalJsonValue& out)
            {
                SkipWhitespace();
                if (m_position >= m_text.size()) return Fail("unexpected end of JSON");
                const char c = m_text[m_position];
                if (c == '{') return ParseObject(out);
                if (c == '[') return ParseArray(out);
                if (c == '"')
                {
                    out.type = FormalJsonValue::Type::String;
                    return ParseString(out.stringValue);
                }
                if (c == 't' && MatchLiteral("true")) { out.type = FormalJsonValue::Type::Boolean; out.booleanValue = true; return true; }
                if (c == 'f' && MatchLiteral("false")) { out.type = FormalJsonValue::Type::Boolean; out.booleanValue = false; return true; }
                if (c == 'n' && MatchLiteral("null")) { out.type = FormalJsonValue::Type::Null; return true; }
                if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
                return Fail("unexpected JSON token");
            }
            bool MatchLiteral(const char* literal)
            {
                const size_t length = std::strlen(literal);
                if (m_text.compare(m_position, length, literal) != 0) return false;
                m_position += length;
                return true;
            }
            bool ParseString(std::string& out)
            {
                if (!Consume('"')) return Fail("expected string");
                out.clear();
                while (m_position < m_text.size())
                {
                    const char c = m_text[m_position++];
                    if (c == '"') return true;
                    if (static_cast<unsigned char>(c) < 0x20u) return Fail("control character in string");
                    if (c != '\\') { out.push_back(c); continue; }
                    if (m_position >= m_text.size()) return Fail("unterminated escape");
                    const char escaped = m_text[m_position++];
                    switch (escaped)
                    {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                        // Paths and experiment identifiers are emitted as UTF-8
                        // rather than escaped UTF-16. Rejecting \u here avoids
                        // silently corrupting hashes or paths.
                        return Fail("unicode escapes are not accepted in formal manifests; emit UTF-8");
                    default: return Fail("invalid string escape");
                    }
                }
                return Fail("unterminated string");
            }
            bool ParseNumber(FormalJsonValue& out)
            {
                const size_t start = m_position;
                if (m_text[m_position] == '-') ++m_position;
                if (m_position >= m_text.size()) return Fail("invalid number");
                if (m_text[m_position] == '0') ++m_position;
                else
                {
                    if (m_text[m_position] < '1' || m_text[m_position] > '9') return Fail("invalid number");
                    while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9') ++m_position;
                }
                if (m_position < m_text.size() && m_text[m_position] == '.')
                {
                    ++m_position;
                    if (m_position >= m_text.size() || m_text[m_position] < '0' || m_text[m_position] > '9') return Fail("invalid fraction");
                    while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9') ++m_position;
                }
                if (m_position < m_text.size() && (m_text[m_position] == 'e' || m_text[m_position] == 'E'))
                {
                    ++m_position;
                    if (m_position < m_text.size() && (m_text[m_position] == '+' || m_text[m_position] == '-')) ++m_position;
                    if (m_position >= m_text.size() || m_text[m_position] < '0' || m_text[m_position] > '9') return Fail("invalid exponent");
                    while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9') ++m_position;
                }
                try { out.numberValue = std::stod(m_text.substr(start, m_position - start)); }
                catch (...) { return Fail("number conversion failed"); }
                if (!std::isfinite(out.numberValue)) return Fail("non-finite number");
                out.type = FormalJsonValue::Type::Number;
                return true;
            }
            bool ParseObject(FormalJsonValue& out)
            {
                if (!Consume('{')) return Fail("expected object");
                out.type = FormalJsonValue::Type::Object;
                out.objectValues.clear();
                SkipWhitespace();
                if (Consume('}')) return true;
                while (true)
                {
                    std::string key;
                    if (!ParseString(key)) return false;
                    for (const auto& existing : out.objectValues)
                        if (existing.first == key) return Fail("duplicate object key");
                    if (!Consume(':')) return Fail("expected colon");
                    FormalJsonValue value;
                    if (!ParseValue(value)) return false;
                    out.objectValues.emplace_back(std::move(key), std::move(value));
                    SkipWhitespace();
                    if (Consume('}')) return true;
                    if (!Consume(',')) return Fail("expected comma");
                }
            }
            bool ParseArray(FormalJsonValue& out)
            {
                if (!Consume('[')) return Fail("expected array");
                out.type = FormalJsonValue::Type::Array;
                out.arrayValues.clear();
                SkipWhitespace();
                if (Consume(']')) return true;
                while (true)
                {
                    FormalJsonValue value;
                    if (!ParseValue(value)) return false;
                    out.arrayValues.emplace_back(std::move(value));
                    SkipWhitespace();
                    if (Consume(']')) return true;
                    if (!Consume(',')) return Fail("expected comma");
                }
            }
        };

        std::wstring FormalUtf8ToWide(const std::string& raw)
        {
            if (raw.empty()) return {};
            const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
            if (required <= 0) return {};
            std::wstring wide(static_cast<size_t>(required), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), wide.data(), required) != required)
                return {};
            return wide;
        }

    }

    GpuHairViewer::ExperimentMethodId GpuHairViewer::ParseExperimentMethodId(const std::wstring& value)
    {
        std::wstring normalized = value;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });

        if (normalized == L"b0" || normalized == L"b0_fulldynamic") return ExperimentMethodId::B0_FullDynamic;
        if (normalized == L"b1" || normalized == L"b1_strandsleep") return ExperimentMethodId::B1_StrandSleep;
        if (normalized == L"b2" || normalized == L"b2_blocksleep") return ExperimentMethodId::B2_BlockSleep;
        if (normalized == L"b3" || normalized == L"b3_segmentthresholdsleep") return ExperimentMethodId::B3_SegmentThresholdSleep;
        if (normalized == L"b4" || normalized == L"b4_segmenthysteresissleep") return ExperimentMethodId::B4_SegmentHysteresisSleep;
        if (normalized == L"b5" || normalized == L"b5_segmentcache") return ExperimentMethodId::B5_SegmentCache;
        if (normalized == L"b6" || normalized == L"b6_segmentgpucompaction") return ExperimentMethodId::B6_SegmentGpuCompaction;
        if (normalized == L"b7" || normalized == L"b7_activesetonly") return ExperimentMethodId::B7_ActiveSetOnly;
        if (normalized == L"b8" || normalized == L"b8_externalstatearray") return ExperimentMethodId::B8_ExternalStateArray;
        if (normalized == L"p" || normalized == L"p_integratedsegmentgroup") return ExperimentMethodId::P_IntegratedSegmentGroup;
        return ExperimentMethodId::Unknown;
    }

    bool GpuHairViewer::ExperimentMethodFlagsEqual(const ExperimentMethodFlags& lhs, const ExperimentMethodFlags& rhs)
    {
        return lhs.enableNoNoveltyBaseline == rhs.enableNoNoveltyBaseline &&
            lhs.enableMicrotype == rhs.enableMicrotype &&
            lhs.enableBudgetedPhysics == rhs.enableBudgetedPhysics &&
            lhs.enableCertificateRender == rhs.enableCertificateRender &&
            lhs.enablePerStrandEdit == rhs.enablePerStrandEdit &&
            lhs.enableCBMA == rhs.enableCBMA &&
            lhs.enablePhysicsIslands == rhs.enablePhysicsIslands &&
            lhs.enableRodRepair == rhs.enableRodRepair &&
            lhs.enableGpuResident == rhs.enableGpuResident &&
            lhs.enableSegmentGroupSleep == rhs.enableSegmentGroupSleep &&
            lhs.enableSegmentGroupIntegratedState == rhs.enableSegmentGroupIntegratedState &&
            lhs.enableExternalStateArray == rhs.enableExternalStateArray &&
            lhs.enableSharedPointOwnership == rhs.enableSharedPointOwnership &&
            lhs.enableDirectionalResponsibility == rhs.enableDirectionalResponsibility &&
            lhs.enableStaticCertificate == rhs.enableStaticCertificate &&
            lhs.enablePredictiveWake == rhs.enablePredictiveWake &&
            lhs.enableVelocityRecovery == rhs.enableVelocityRecovery &&
            lhs.enableRepair == rhs.enableRepair &&
            lhs.enableGpuCompaction == rhs.enableGpuCompaction &&
            lhs.enableActiveSet == rhs.enableActiveSet &&
            lhs.enableStrandSleep == rhs.enableStrandSleep &&
            lhs.enableBlockSleep == rhs.enableBlockSleep &&
            lhs.enableSleepHysteresis == rhs.enableSleepHysteresis &&
            lhs.enableStaticCache == rhs.enableStaticCache &&
            lhs.stateStorage == rhs.stateStorage;
    }

    GpuHairViewer::ExperimentScenarioId GpuHairViewer::ParseExperimentScenarioId(const std::wstring& value)
    {
        static const std::array<std::pair<const wchar_t*, ExperimentScenarioId>, 18> values = {{
            { L"S01_Idle", ExperimentScenarioId::S01_Idle }, { L"S02_Breathing", ExperimentScenarioId::S02_Breathing },
            { L"S03_Walk", ExperimentScenarioId::S03_Walk }, { L"S04_SuddenStop", ExperimentScenarioId::S04_SuddenStop },
            { L"S05_HeadRotation", ExperimentScenarioId::S05_HeadRotation }, { L"S06_Teleport", ExperimentScenarioId::S06_Teleport },
            { L"S07_ColliderApproach", ExperimentScenarioId::S07_ColliderApproach },
            { L"S08_StaticDynamicBoundaryContact", ExperimentScenarioId::S08_StaticDynamicBoundaryContact },
            { L"S09_StrongWind", ExperimentScenarioId::S09_StrongWind }, { L"S10_WindStop", ExperimentScenarioId::S10_WindStop },
            { L"S11_LocalEdit", ExperimentScenarioId::S11_LocalEdit }, { L"S12_FalseStaticTrap", ExperimentScenarioId::S12_FalseStaticTrap },
            { L"S13_RepairStorm", ExperimentScenarioId::S13_RepairStorm }, { L"S14_AllDynamic", ExperimentScenarioId::S14_AllDynamic },
            { L"S15_AllStatic", ExperimentScenarioId::S15_AllStatic }, { L"S16_StateThrashing", ExperimentScenarioId::S16_StateThrashing },
            { L"S17_CloseCamera100k", ExperimentScenarioId::S17_CloseCamera100k }, { L"S18_FarCamera100k", ExperimentScenarioId::S18_FarCamera100k }
        }};
        for (const auto& entry : values) if (value == entry.first) return entry.second;
        return ExperimentScenarioId::Unknown;
    }

    GpuHairViewer::ScenarioEventType GpuHairViewer::ParseScenarioEventType(const std::string& value)
    {
        if (value == "set_wind") return ScenarioEventType::SetWind;
        if (value == "set_root_translation") return ScenarioEventType::SetRootTranslation;
        if (value == "set_root_rotation") return ScenarioEventType::SetRootRotation;
        if (value == "teleport_root") return ScenarioEventType::TeleportRoot;
        if (value == "set_collider_transform") return ScenarioEventType::SetColliderTransform;
        if (value == "inject_local_edit") return ScenarioEventType::InjectLocalEdit;
        if (value == "inject_repair_storm") return ScenarioEventType::InjectRepairStorm;
        if (value == "force_all_dynamic") return ScenarioEventType::ForceAllDynamic;
        if (value == "force_all_static") return ScenarioEventType::ForceAllStatic;
        if (value == "set_camera_distance") return ScenarioEventType::SetCameraDistance;
        return ScenarioEventType::Unknown;
    }

    // HAIRGPU_EXPERIMENT_RUNTIME_PATCH_V2
    bool GpuHairViewer::LoadExperimentCaseFromEnvironment()
    {
        m_experimentCase = {};
        const std::wstring caseJsonPath = GetEnvironmentVariableString(L"HAIRGPU_CASE_JSON");
        if (caseJsonPath.empty())
        {
            return true; // normal interactive launch
        }

        std::ifstream input(std::filesystem::path(caseJsonPath), std::ios::binary);
        if (!input) return false;
        const std::string caseJson{
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
        FormalJsonValue root;
        std::string parseError;
        if (!FormalJsonParser(caseJson).Parse(root, parseError) || root.type != FormalJsonValue::Type::Object)
        {
            m_experimentFailureMessage = L"Experiment case JSON parse failed: " + FormalUtf8ToWide(parseError);
            return false;
        }

        const FormalJsonValue* settings = root.FindAny({ "method_settings", "methodSettings" });
        if (!settings || settings->type != FormalJsonValue::Type::Object)
        {
            m_experimentFailureMessage = L"Formal case is missing a method_settings object.";
            return false;
        }
        const FormalJsonValue* assertions = root.FindAny({ "assertions", "scenario_assertions" });
        if (!assertions || assertions->type != FormalJsonValue::Type::Object)
        {
            m_experimentFailureMessage = L"Formal case is missing a scenario assertions object.";
            return false;
        }

        auto envText = [](const wchar_t* name, const std::wstring& fallback) -> std::wstring
        {
            const std::wstring value = GetEnvironmentVariableString(name);
            return value.empty() ? fallback : value;
        };
        auto envUInt = [](const wchar_t* name, uint32_t fallback) -> uint32_t
        {
            const std::wstring value = GetEnvironmentVariableString(name);
            if (value.empty()) return fallback;
            try { return static_cast<uint32_t>(std::stoul(value)); }
            catch (...) { return fallback; }
        };
        auto envFloat = [](const wchar_t* name, float fallback) -> float
        {
            const std::wstring value = GetEnvironmentVariableString(name);
            if (value.empty()) return fallback;
            try { return std::stof(value); }
            catch (...) { return fallback; }
        };
        auto envBool = [](const wchar_t* name, bool fallback) -> bool
        {
            std::wstring value = GetEnvironmentVariableString(name);
            if (value.empty()) return fallback;
            std::transform(value.begin(), value.end(), value.begin(), ::towlower);
            return value == L"1" || value == L"true" || value == L"yes" || value == L"on";
        };
        auto settingBool = [&](std::initializer_list<const char*> keys, const wchar_t* envName, bool fallback) -> bool
        {
            return envBool(envName, settings->Boolean(keys, fallback));
        };

        m_experimentCase.enabled = true;
        m_experimentCase.caseJsonPath = caseJsonPath;
        m_experimentCase.caseId = envText(L"HAIRGPU_EXPERIMENT_CASE_ID", FormalUtf8ToWide(root.String({ "case_id", "caseId" })));
        m_experimentCase.variant = envText(L"HAIRGPU_VARIANT", FormalUtf8ToWide(root.String({ "method", "variant" })));
        m_experimentCase.scenario = envText(L"HAIRGPU_SCENARIO", FormalUtf8ToWide(root.String({ "scenario" })));
        m_experimentCase.methodId = ParseExperimentMethodId(m_experimentCase.variant);
        m_experimentCase.scenarioId = ParseExperimentScenarioId(m_experimentCase.scenario);
        m_experimentCase.measurementModeName = envText(L"HAIRGPU_MEASUREMENT_MODE", FormalUtf8ToWide(root.String({ "measurement_mode", "measurementMode" }, "performance")));
        if (m_experimentCase.measurementModeName == L"quality") m_experimentCase.measurementMode = MeasurementMode::Quality;
        else if (m_experimentCase.measurementModeName == L"determinism") m_experimentCase.measurementMode = MeasurementMode::Determinism;
        else if (m_experimentCase.measurementModeName == L"robustness") m_experimentCase.measurementMode = MeasurementMode::Robustness;
        else m_experimentCase.measurementMode = MeasurementMode::Performance;

        m_experimentCase.seed = envUInt(L"HAIRGPU_SEED", root.UInt({ "seed" }, 0u));
        m_experimentCase.repetitionIndex = envUInt(L"HAIRGPU_REPEAT_INDEX", root.UInt({ "repeat_index", "repetition_index" }, 0u));
        m_experimentCase.warmupFrames = envUInt(L"HAIRGPU_WARMUP_FRAMES", root.UInt({ "warmup_frames", "warmupFrames" }, 600u));
        m_experimentCase.measurementFrames = envUInt(L"HAIRGPU_MEASUREMENT_FRAMES", root.UInt({ "measurement_frames", "measurementFrames" }, 6000u));
        m_experimentCase.requestedWidth = envUInt(L"HAIRGPU_RENDER_WIDTH", root.UInt({ "width", "render_width" }, 1920u));
        m_experimentCase.requestedHeight = envUInt(L"HAIRGPU_RENDER_HEIGHT", root.UInt({ "height", "render_height" }, 1080u));
        m_experimentCase.requestedGuideCount = envUInt(L"HAIRGPU_GUIDE_COUNT", root.UInt({ "guide_count", "guide_strand_count" }, 4000u));
        m_experimentCase.requestedDisplayStrandCount = envUInt(L"HAIRGPU_DISPLAY_STRAND_COUNT", root.UInt({ "display_strand_count" }, 100000u));
        m_experimentCase.requestedParticlesPerGuide = envUInt(L"HAIRGPU_PARTICLES_PER_GUIDE", root.UInt({ "particles_per_guide", "particles_per_strand" }, 128u));
        m_experimentCase.requestedSolverIterations = envUInt(L"HAIRGPU_SOLVER_ITERATIONS", root.UInt({ "solver_iterations" }, 12u));

        // Formal 100k contract: all guides must own the same follower count.
        // The canonical layout is 4,000 guides + 96,000 followers
        // (24 followers per guide) = exactly 100,000 displayed strands.
        if (m_experimentCase.requestedDisplayStrandCount != 100000u)
        {
            m_experimentFailureMessage = L"Formal evaluation requires exactly 100,000 displayed strands.";
            return false;
        }
        if (m_experimentCase.requestedGuideCount == 0u ||
            m_experimentCase.requestedDisplayStrandCount < m_experimentCase.requestedGuideCount)
        {
            m_experimentFailureMessage = L"Display strand count must be at least the positive guide count.";
            return false;
        }
        if ((m_experimentCase.requestedDisplayStrandCount % m_experimentCase.requestedGuideCount) != 0u)
        {
            m_experimentFailureMessage = L"Display strand count must be exactly divisible by guide count.";
            return false;
        }
        const uint32_t requestedFollowersPerGuide =
            m_experimentCase.requestedDisplayStrandCount / m_experimentCase.requestedGuideCount - 1u;
        if (requestedFollowersPerGuide > 64u)
        {
            m_experimentFailureMessage = L"Followers per guide exceeds the Viewer-supported limit of 64.";
            return false;
        }
        m_experimentCase.fixedTimeStepSeconds = envFloat(L"HAIRGPU_FIXED_TIMESTEP_SECONDS", root.Float({ "fixed_timestep_seconds" }, 1.0f / 60.0f));
        m_experimentCase.fixedTimeStepEnabled = true;
        m_experimentCase.vsyncEnabled = envBool(L"HAIRGPU_VSYNC", root.Boolean({ "vsync" }, false));
        m_experimentCase.presentSyncInterval = envUInt(L"HAIRGPU_PRESENT_SYNC_INTERVAL", root.UInt({ "present_sync_interval" }, 0u));
        m_experimentCase.gpuTimestampDelayFrames = envUInt(L"HAIRGPU_GPU_TIMESTAMP_DELAY_FRAMES", root.UInt({ "gpu_timestamp_delay_frames" }, 6u));
        m_experimentCase.autoExitAfterCapture = envBool(L"HAIRGPU_AUTO_EXIT_AFTER_CAPTURE", root.Boolean({ "auto_exit_after_capture" }, true));
        m_experimentCase.strictValidationEnabled = envBool(L"HAIRGPU_STRICT_VALIDATION", root.Boolean({ "strict_validation" }, true));
        m_experimentCase.hardwareAdapterRequired = root.Boolean({ "hardware_adapter_required", "real_measurement_required" }, true);

        m_experimentCase.scenarioTimelinePath = envText(L"HAIRGPU_SCENARIO_TIMELINE", FormalUtf8ToWide(root.String({ "scenarioTimelinePath", "scenario_timeline" })));
        m_experimentCase.assetManifestPath = envText(L"HAIRGPU_ASSET_MANIFEST", FormalUtf8ToWide(root.String({ "assetManifestPath", "asset_manifest" })));
        m_experimentCase.settingsHash = envText(L"HAIRGPU_SETTINGS_HASH", FormalUtf8ToWide(root.String({ "settings_hash", "settingsSha256" })));
        m_experimentCase.executableSha256 = envText(L"HAIRGPU_EXECUTABLE_SHA256", FormalUtf8ToWide(root.String({ "executableSha256" })));
        m_experimentCase.commitSha = envText(L"HAIRGPU_COMMIT_SHA", FormalUtf8ToWide(root.String({ "commitSha", "commit_sha" })));
        m_experimentCase.buildConfiguration = envText(L"HAIRGPU_BUILD_CONFIGURATION", FormalUtf8ToWide(root.String({ "build_configuration" }, "Release")));
        m_experimentCase.platform = envText(L"HAIRGPU_PLATFORM", FormalUtf8ToWide(root.String({ "platform" }, "x64")));
        m_experimentCase.hardwareProfile = envText(L"HAIRGPU_HARDWARE_PROFILE", FormalUtf8ToWide(root.String({ "hardware_profile" })));
        m_experimentCase.scenarioTimelineSha256 = envText(L"HAIRGPU_SCENARIO_TIMELINE_SHA256", FormalUtf8ToWide(root.String({ "scenarioTimelineSha256" })));
        m_experimentCase.assetManifestSha256 = envText(L"HAIRGPU_ASSET_MANIFEST_SHA256", FormalUtf8ToWide(root.String({ "assetManifestSha256" })));
        m_experimentCase.assetSha256 = envText(L"HAIRGPU_ASSET_SHA256", FormalUtf8ToWide(root.String({ "assetSha256" })));
        m_experimentCase.shaderBundleSha256 = envText(L"HAIRGPU_SHADER_BUNDLE_SHA256", FormalUtf8ToWide(root.String({ "shaderBundleSha256" })));
        m_experimentCase.requestedMethodSettingsSha256 = envText(
            L"HAIRGPU_REQUESTED_METHOD_SETTINGS_SHA256",
            FormalUtf8ToWide(root.String({ "requestedMethodSettingsSha256", "requested_method_settings_sha256", "method_settings_sha256" })));
        m_experimentCase.appliedMethodSettingsSha256 = envText(
            L"HAIRGPU_APPLIED_METHOD_SETTINGS_SHA256",
            FormalUtf8ToWide(root.String({ "appliedMethodSettingsSha256", "applied_method_settings_sha256", "method_settings_sha256" })));
        if (m_experimentCase.appliedMethodSettingsSha256.empty())
            m_experimentCase.appliedMethodSettingsSha256 = m_experimentCase.requestedMethodSettingsSha256;
        m_experimentCase.assetFrozen = envBool(L"HAIRGPU_ASSET_FROZEN", root.Boolean({ "assetFrozen" }, false));

        // Zero-event scenarios such as Idle, Breathing and Walk are valid.
        // Event-bearing scenarios declare their own positive minimum in the case JSON.
        m_experimentCase.scenarioAssertionMinAppliedEvents =
            assertions->UInt({ "min_applied_events", "minAppliedEvents" }, 0u);
        m_experimentCase.scenarioAssertionMinCollisionCandidates = assertions->UInt(
            { "min_collision_candidates", "minCollisionCandidates" }, 0u);
        m_experimentCase.scenarioAssertionMinWakeRequests = assertions->UInt(
            { "min_wake_requests", "minWakeRequests" }, 0u);
        m_experimentCase.scenarioAssertionMinRepairs = assertions->UInt(
            { "min_repairs", "minRepairs" }, 0u);
        m_experimentCase.scenarioAssertionMinTransitions = assertions->UInt(
            { "min_state_transitions", "minTransitions" }, 0u);

        // Do not inherit the member default initializers here.  Every formal
        // method starts from an explicit all-disabled state so that B0--B8 and
        // P cannot accidentally share a capability merely because a new flag
        // was added to ExperimentMethodFlags later.
        ExperimentMethodFlags flags = {};
        flags.enableNoNoveltyBaseline = false;
        flags.enableMicrotype = false;
        flags.enableBudgetedPhysics = false;
        flags.enableCertificateRender = false;
        flags.enablePerStrandEdit = false;
        flags.enableCBMA = false;
        flags.enablePhysicsIslands = false;
        flags.enableRodRepair = false;
        flags.enableGpuResident = false;
        flags.enableSegmentGroupSleep = false;
        flags.enableSegmentGroupIntegratedState = false;
        flags.enableExternalStateArray = false;
        flags.enableSharedPointOwnership = false;
        flags.enableDirectionalResponsibility = false;
        flags.enableStaticCertificate = false;
        flags.enablePredictiveWake = false;
        flags.enableVelocityRecovery = false;
        flags.enableRepair = false;
        flags.enableGpuCompaction = false;
        flags.enableActiveSet = false;
        flags.enableStrandSleep = false;
        flags.enableBlockSleep = false;
        flags.enableSleepHysteresis = false;
        flags.enableStaticCache = false;
        flags.stateStorage = SegmentGroupStateStorage::ExternalArray;

        switch (m_experimentCase.methodId)
        {
        case ExperimentMethodId::B0_FullDynamic:
            flags.enableNoNoveltyBaseline = true;
            flags.stateStorage = SegmentGroupStateStorage::IntegratedRecord;
            break;
        case ExperimentMethodId::B1_StrandSleep:
            flags.enableStrandSleep = true;
            break;
        case ExperimentMethodId::B2_BlockSleep:
            flags.enableBlockSleep = true;
            break;
        case ExperimentMethodId::B3_SegmentThresholdSleep:
            flags.enableSegmentGroupSleep = true;
            break;
        case ExperimentMethodId::B4_SegmentHysteresisSleep:
            flags.enableSegmentGroupSleep = true;
            flags.enableSleepHysteresis = true;
            break;
        case ExperimentMethodId::B5_SegmentCache:
            flags.enableSegmentGroupSleep = true;
            flags.enableSleepHysteresis = true;
            flags.enableStaticCache = true;
            break;
        case ExperimentMethodId::B6_SegmentGpuCompaction:
            flags.enableSegmentGroupSleep = true;
            flags.enableSleepHysteresis = true;
            flags.enableStaticCache = true;
            flags.enableGpuCompaction = true;
            break;
        case ExperimentMethodId::B7_ActiveSetOnly:
            flags.enableActiveSet = true;
            flags.enableGpuCompaction = true;
            break;
        case ExperimentMethodId::B8_ExternalStateArray:
        case ExperimentMethodId::P_IntegratedSegmentGroup:
            flags.enableSegmentGroupSleep = true;
            flags.enableSharedPointOwnership = true;
            flags.enableDirectionalResponsibility = true;
            flags.enableStaticCertificate = true;
            flags.enablePredictiveWake = true;
            flags.enableVelocityRecovery = true;
            flags.enableRepair = true;
            flags.enableGpuCompaction = true;
            flags.enableActiveSet = true;
            flags.enableSleepHysteresis = true;
            flags.enableStaticCache = true;
            flags.enableGpuResident = true;
            flags.enableExternalStateArray = m_experimentCase.methodId == ExperimentMethodId::B8_ExternalStateArray;
            flags.enableSegmentGroupIntegratedState = m_experimentCase.methodId == ExperimentMethodId::P_IntegratedSegmentGroup;
            flags.stateStorage = flags.enableExternalStateArray
                ? SegmentGroupStateStorage::ExternalArray
                : SegmentGroupStateStorage::IntegratedRecord;
            break;
        default:
            break;
        }
        flags.enableNoNoveltyBaseline = settingBool({ "enable_no_novelty_baseline", "enableNoNoveltyBaseline" }, L"HAIRGPU_ENABLE_NO_NOVELTY_BASELINE", flags.enableNoNoveltyBaseline);
        flags.enableMicrotype = settingBool({ "enable_microtype", "enableMicrotype" }, L"HAIRGPU_ENABLE_MICROTYPE", flags.enableMicrotype);
        flags.enableBudgetedPhysics = settingBool({ "enable_budgeted_physics", "enableBudgetedPhysics" }, L"HAIRGPU_ENABLE_BUDGETED_PHYSICS", flags.enableBudgetedPhysics);
        flags.enableCertificateRender = settingBool({ "enable_certificate_render", "enableCertificateRender" }, L"HAIRGPU_ENABLE_CERTIFICATE_RENDER", flags.enableCertificateRender);
        flags.enablePerStrandEdit = settingBool({ "enable_per_strand_edit", "enablePerStrandEdit" }, L"HAIRGPU_ENABLE_PER_STRAND_EDIT", flags.enablePerStrandEdit);
        flags.enableCBMA = settingBool({ "enable_cbma", "enableCBMA" }, L"HAIRGPU_ENABLE_CBMA", flags.enableCBMA);
        flags.enablePhysicsIslands = settingBool({ "enable_physics_islands", "enablePhysicsIslands" }, L"HAIRGPU_ENABLE_PHYSICS_ISLANDS", flags.enablePhysicsIslands);
        flags.enableRodRepair = settingBool({ "enable_rod_repair", "enableRodRepair" }, L"HAIRGPU_ENABLE_ROD_REPAIR", flags.enableRodRepair);
        flags.enableGpuResident = settingBool({ "enable_gpu_resident", "enableGpuResident" }, L"HAIRGPU_ENABLE_GPU_RESIDENT", flags.enableGpuResident);
        flags.enableSegmentGroupSleep = settingBool({ "enable_segment_group_sleep", "enableSegmentGroupSleep" }, L"HAIRGPU_ENABLE_SEGMENT_GROUP_SLEEP", flags.enableSegmentGroupSleep);
        flags.enableSegmentGroupIntegratedState = settingBool({ "enable_segment_group_integrated_state", "enableSegmentGroupIntegratedState" }, L"HAIRGPU_ENABLE_SEGMENT_GROUP_INTEGRATED_STATE", flags.enableSegmentGroupIntegratedState);
        flags.enableExternalStateArray = settingBool({ "enable_external_state_array", "enableExternalStateArray" }, L"HAIRGPU_ENABLE_EXTERNAL_STATE_ARRAY", flags.enableExternalStateArray);
        flags.enableSharedPointOwnership = settingBool({ "enable_shared_point_ownership", "enableSharedPointOwnership" }, L"HAIRGPU_ENABLE_SHARED_POINT_OWNERSHIP", flags.enableSharedPointOwnership);
        flags.enableDirectionalResponsibility = settingBool({ "enable_directional_responsibility", "enableDirectionalResponsibility" }, L"HAIRGPU_ENABLE_DIRECTIONAL_RESPONSIBILITY", flags.enableDirectionalResponsibility);
        flags.enableStaticCertificate = settingBool({ "enable_static_certificate", "enableStaticCertificate" }, L"HAIRGPU_ENABLE_STATIC_CERTIFICATE", flags.enableStaticCertificate);
        flags.enablePredictiveWake = settingBool({ "enable_predictive_wake", "enablePredictiveWake" }, L"HAIRGPU_ENABLE_PREDICTIVE_WAKE", flags.enablePredictiveWake);
        flags.enableVelocityRecovery = settingBool({ "enable_velocity_recovery", "enableVelocityRecovery" }, L"HAIRGPU_ENABLE_VELOCITY_RECOVERY", flags.enableVelocityRecovery);
        flags.enableRepair = settingBool({ "enable_repair", "enableRepair" }, L"HAIRGPU_ENABLE_REPAIR", flags.enableRepair);
        flags.enableGpuCompaction = settingBool({ "enable_gpu_compaction", "enableGpuCompaction" }, L"HAIRGPU_ENABLE_GPU_COMPACTION", flags.enableGpuCompaction);
        flags.enableActiveSet = settingBool({ "enable_active_set", "enableActiveSet" }, L"HAIRGPU_ENABLE_ACTIVE_SET", flags.enableActiveSet);
        flags.enableStrandSleep = settingBool({ "enable_strand_sleep", "enableStrandSleep" }, L"HAIRGPU_ENABLE_STRAND_SLEEP", flags.enableStrandSleep);
        flags.enableBlockSleep = settingBool({ "enable_block_sleep", "enableBlockSleep" }, L"HAIRGPU_ENABLE_BLOCK_SLEEP", flags.enableBlockSleep);
        flags.enableSleepHysteresis = settingBool({ "enable_sleep_hysteresis", "enableSleepHysteresis" }, L"HAIRGPU_ENABLE_SLEEP_HYSTERESIS", flags.enableSleepHysteresis);
        flags.enableStaticCache = settingBool({ "enable_static_cache", "enableStaticCache" }, L"HAIRGPU_ENABLE_STATIC_CACHE", flags.enableStaticCache);
        std::string stateStorageText = settings->String({ "state_storage", "stateStorage" });
        std::transform(stateStorageText.begin(), stateStorageText.end(), stateStorageText.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (stateStorageText == "external" || stateStorageText == "externalarray")
            flags.stateStorage = SegmentGroupStateStorage::ExternalArray;
        else if (stateStorageText == "integrated" || stateStorageText == "integratedrecord")
            flags.stateStorage = SegmentGroupStateStorage::IntegratedRecord;
        m_experimentCase.requestedFlags = flags;
        m_experimentCase.appliedFlags = flags;
        m_experimentCase.methodFlagsAcknowledged = ValidateExperimentMethodFlags();
        if (!m_experimentCase.methodFlagsAcknowledged && m_experimentCase.strictValidationEnabled) return false;

        m_experimentCase.enableNoNoveltyBaseline = flags.enableNoNoveltyBaseline;
        m_experimentCase.enableMicrotype = flags.enableMicrotype;
        m_experimentCase.enableBudgetedPhysics = flags.enableBudgetedPhysics;
        m_experimentCase.enableCertificateRender = flags.enableCertificateRender;
        m_experimentCase.enablePerStrandEdit = flags.enablePerStrandEdit;
        m_experimentCase.enableCBMA = flags.enableCBMA;
        m_experimentCase.enablePhysicsIslands = flags.enablePhysicsIslands;
        m_experimentCase.enableRodRepair = flags.enableRodRepair;
        m_experimentCase.enableGpuResident = flags.enableGpuResident;

        m_noveltyFlags = hairgpu::NormalizeNoveltyFlags(
            flags.enableMicrotype ? 1u : 0u, flags.enableBudgetedPhysics ? 1u : 0u,
            flags.enableCertificateRender ? 1u : 0u, flags.enablePerStrandEdit ? 1u : 0u,
            flags.enableGpuResident ? 1u : 0u, flags.enableCBMA ? 1u : 0u,
            flags.enablePhysicsIslands ? 1u : 0u, flags.enableRodRepair ? 1u : 0u,
            flags.enableNoNoveltyBaseline ? 1u : 0u);
        m_noveltyFlags.enableMicrotypeCertificate = flags.enableStaticCertificate ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeDirtyPropagation = flags.enableSegmentGroupSleep ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeBlockHierarchy = flags.enableBlockSleep ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeBoundaryRepair = flags.enableRepair ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeWatchdog = flags.enableRepair ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeHemisphereOwnership = flags.enableDirectionalResponsibility ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeIndirectDispatch = flags.enableGpuCompaction ? 1u : 0u;
        m_noveltyFlags = hairgpu::NormalizeMicrotypeAblationFlags(m_noveltyFlags);

        ExperimentMethodFlags observedFlags = flags;
        observedFlags.enableNoNoveltyBaseline = m_noveltyFlags.enableNoNoveltyBaseline != 0u;
        observedFlags.enableMicrotype = m_noveltyFlags.enableMicrotype != 0u;
        observedFlags.enableBudgetedPhysics = m_noveltyFlags.enableBudgetedPhysics != 0u;
        observedFlags.enableCertificateRender = m_noveltyFlags.enableCertificateRender != 0u;
        observedFlags.enablePerStrandEdit = m_noveltyFlags.enablePerStrandEdit != 0u;
        observedFlags.enableCBMA = m_noveltyFlags.enableCBMA != 0u;
        observedFlags.enablePhysicsIslands = m_noveltyFlags.enablePhysicsIslands != 0u;
        observedFlags.enableRodRepair = m_noveltyFlags.enableRodRepair != 0u;
        observedFlags.enableGpuResident = m_noveltyFlags.enableGpuResident != 0u;
        observedFlags.enableStaticCertificate = m_noveltyFlags.enableMicrotypeCertificate != 0u;
        observedFlags.enableDirectionalResponsibility = m_noveltyFlags.enableMicrotypeHemisphereOwnership != 0u;
        observedFlags.enableGpuCompaction = m_noveltyFlags.enableMicrotypeIndirectDispatch != 0u;
        observedFlags.enableRepair = m_noveltyFlags.enableMicrotypeBoundaryRepair != 0u;
        m_experimentCase.observedFlags = observedFlags;
        if (m_experimentCase.methodFlagsAcknowledged &&
            ExperimentMethodFlagsEqual(m_experimentCase.appliedFlags, m_experimentCase.observedFlags))
        {
            m_experimentCase.observedMethodSettingsSha256 = m_experimentCase.appliedMethodSettingsSha256;
        }

        m_segmentGroupsPerBlock = std::max<uint32_t>(1u, settings->UInt({ "groupsPerBlock", "groups_per_block" }, m_segmentGroupsPerBlock));
        m_segmentGroupWatchdogIntervalFrames = std::max<uint32_t>(1u, settings->UInt({ "watchdogIntervalFrames", "watchdog_interval_frames" }, m_segmentGroupWatchdogIntervalFrames));
        m_globalHairBudget = hairgpu::BuildGlobalHairFrameBudget(m_noveltyFlags);
        m_segmentGroupTopologyDirty = true;
        m_forceAllSegmentGroupsDirty = true;

        if (!LoadScenarioTimeline() && m_experimentCase.strictValidationEnabled) return false;
        return true;
    }

    bool GpuHairViewer::ValidateExperimentMethodFlags() const
    {
        const ExperimentMethodFlags& flags = m_experimentCase.appliedFlags;
        switch (m_experimentCase.methodId)
        {
        case ExperimentMethodId::B0_FullDynamic:
            return flags.enableNoNoveltyBaseline && !flags.enableSegmentGroupSleep && !flags.enableExternalStateArray;
        case ExperimentMethodId::B8_ExternalStateArray:
            return flags.enableSegmentGroupSleep && flags.enableExternalStateArray &&
                !flags.enableSegmentGroupIntegratedState && flags.stateStorage == SegmentGroupStateStorage::ExternalArray;
        case ExperimentMethodId::P_IntegratedSegmentGroup:
            return flags.enableSegmentGroupSleep && !flags.enableExternalStateArray &&
                flags.enableSegmentGroupIntegratedState && flags.stateStorage == SegmentGroupStateStorage::IntegratedRecord;
        case ExperimentMethodId::Unknown:
            return false;
        default:
            return !(flags.enableExternalStateArray && flags.enableSegmentGroupIntegratedState);
        }
    }

    bool GpuHairViewer::LoadScenarioTimeline()
    {
        m_experimentCase.timeline = {};
        m_experimentCase.timelineLoaded = false;
        if (m_experimentCase.scenarioTimelinePath.empty()) return false;
        std::ifstream input(std::filesystem::path(m_experimentCase.scenarioTimelinePath), std::ios::binary);
        if (!input) return false;
        const std::string text{
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
        FormalJsonValue root;
        std::string error;
        if (!FormalJsonParser(text).Parse(root, error) || root.type != FormalJsonValue::Type::Object) return false;

        ScenarioTimeline timeline;
        timeline.sourcePath = m_experimentCase.scenarioTimelinePath;
        timeline.sha256 = m_experimentCase.scenarioTimelineSha256;
        const std::wstring timelineScenario = FormalUtf8ToWide(root.String({ "scenario", "scenarioId" }));
        timeline.scenarioId = ParseExperimentScenarioId(timelineScenario.empty() ? m_experimentCase.scenario : timelineScenario);
        timeline.durationFrames = root.UInt({ "durationFrames", "duration_frames" }, m_experimentCase.warmupFrames + m_experimentCase.measurementFrames);
        timeline.fixedTimeStepSeconds = root.Float({ "fixedTimeStepSeconds", "fixed_timestep_seconds" }, m_experimentCase.fixedTimeStepSeconds);
        timeline.seedDriven = root.Boolean({ "seedDriven", "seed_driven" }, true);

        const FormalJsonValue* events = root.Find("events");
        if (events && events->type == FormalJsonValue::Type::Array)
        {
            for (const FormalJsonValue& item : events->arrayValues)
            {
                if (item.type != FormalJsonValue::Type::Object) return false;
                ScenarioEvent event;
                event.frame = item.UInt({ "frame" }, std::numeric_limits<uint32_t>::max());
                event.type = ParseScenarioEventType(item.String({ "type" }));
                event.targetIndex = item.UInt({ "targetIndex", "target_index" }, 0u);
                const FormalJsonValue* value = item.FindAny({ "value", "translation", "rotation" });
                if (value && value->type == FormalJsonValue::Type::Array)
                {
                    const size_t count = std::min<size_t>(value->arrayValues.size(), event.value.size());
                    for (size_t i = 0; i < count; ++i)
                    {
                        const FormalJsonValue& component = value->arrayValues[i];
                        if (component.type != FormalJsonValue::Type::Number) return false;
                        event.value[i] = static_cast<float>(component.numberValue);
                    }
                }
                else if (value && value->type == FormalJsonValue::Type::Number)
                {
                    event.value[0] = static_cast<float>(value->numberValue);
                }
                if (event.type == ScenarioEventType::Unknown || event.frame == std::numeric_limits<uint32_t>::max()) return false;
                timeline.events.push_back(event);
            }
        }
        std::stable_sort(timeline.events.begin(), timeline.events.end(), [](const ScenarioEvent& a, const ScenarioEvent& b) { return a.frame < b.frame; });
        m_experimentCase.timeline = std::move(timeline);
        m_experimentCase.timelineLoaded = ValidateScenarioTimeline();
        return m_experimentCase.timelineLoaded;
    }

    bool GpuHairViewer::ValidateScenarioTimeline() const
    {
        const ScenarioTimeline& timeline = m_experimentCase.timeline;
        if (timeline.scenarioId == ExperimentScenarioId::Unknown || timeline.scenarioId != m_experimentCase.scenarioId) return false;
        if (timeline.durationFrames < m_experimentCase.warmupFrames + m_experimentCase.measurementFrames) return false;
        if (std::abs(timeline.fixedTimeStepSeconds - m_experimentCase.fixedTimeStepSeconds) > 1.0e-6f) return false;
        const bool allowsZeroMeasuredEvents =
            timeline.scenarioId == ExperimentScenarioId::S01_Idle;
        if (timeline.events.empty() && !allowsZeroMeasuredEvents) return false;

        uint32_t previousFrame = 0u;
        uint32_t measuredEventCount = 0u;
        uint32_t stateStimulusCount = 0u;
        bool hasCollider = false, hasStrongWind = false, hasEdit = false, hasRepair = false;
        bool hasForceDynamic = false, hasForceStatic = false, hasCamera = false;
        for (size_t i = 0; i < timeline.events.size(); ++i)
        {
            const ScenarioEvent& event = timeline.events[i];
            if (event.type == ScenarioEventType::Unknown || event.frame >= timeline.durationFrames) return false;
            if (i > 0u && event.frame < previousFrame) return false;
            previousFrame = event.frame;
            if (event.frame >= m_experimentCase.warmupFrames) ++measuredEventCount;
            switch (event.type)
            {
            case ScenarioEventType::SetColliderTransform: hasCollider = true; break;
            case ScenarioEventType::SetWind:
                hasStrongWind = hasStrongWind || std::abs(event.value[0]) >= 0.5f;
                ++stateStimulusCount;
                break;
            case ScenarioEventType::SetRootTranslation:
            case ScenarioEventType::SetRootRotation:
            case ScenarioEventType::TeleportRoot:
                ++stateStimulusCount;
                break;
            case ScenarioEventType::InjectLocalEdit: hasEdit = true; break;
            case ScenarioEventType::InjectRepairStorm: hasRepair = true; break;
            case ScenarioEventType::ForceAllDynamic: hasForceDynamic = true; ++stateStimulusCount; break;
            case ScenarioEventType::ForceAllStatic: hasForceStatic = true; ++stateStimulusCount; break;
            case ScenarioEventType::SetCameraDistance: hasCamera = true; break;
            default: break;
            }
        }
        if (measuredEventCount == 0u && !allowsZeroMeasuredEvents) return false;

        // Semantic assertions prevent a correctly shaped timeline from silently
        // exercising the wrong phenomenon. Only scenarios with a unique required
        // stimulus are constrained here; the remaining scenarios use run-time counters.
        switch (timeline.scenarioId)
        {
        case ExperimentScenarioId::S07_ColliderApproach:
        case ExperimentScenarioId::S08_StaticDynamicBoundaryContact:
            return hasCollider;
        case ExperimentScenarioId::S09_StrongWind:
            return hasStrongWind;
        case ExperimentScenarioId::S11_LocalEdit:
            return hasEdit;
        case ExperimentScenarioId::S13_RepairStorm:
            return hasRepair;
        case ExperimentScenarioId::S14_AllDynamic:
            return hasForceDynamic;
        case ExperimentScenarioId::S15_AllStatic:
            return hasForceStatic;
        case ExperimentScenarioId::S16_StateThrashing:
            return stateStimulusCount >= 2u;
        case ExperimentScenarioId::S17_CloseCamera100k:
        case ExperimentScenarioId::S18_FarCamera100k:
            return hasCamera;
        default:
            return true;
        }
    }

    void GpuHairViewer::ApplyScenarioTimelineFrame(uint32_t frameIndex)
    {
        if (!m_experimentCase.timelineLoaded) return;
        for (const ScenarioEvent& event : m_experimentCase.timeline.events)
        {
            if (event.frame != frameIndex) continue;
            switch (event.type)
            {
            case ScenarioEventType::SetWind:
                m_simConfig.windStrength = event.value[0];
                break;
            case ScenarioEventType::SetRootTranslation:
            case ScenarioEventType::SetRootRotation:
                m_enableScalpMotion = true;
                m_forceAllSegmentGroupsDirty = true;
                break;
            case ScenarioEventType::TeleportRoot:
                m_scalpCenter = m_scalpCenter + Vec3(event.value[0], event.value[1], event.value[2]);
                m_forceAllSegmentGroupsDirty = true;
                ++m_scenarioWakeRequestCount;
                break;
            case ScenarioEventType::SetColliderTransform:
                if (event.targetIndex < m_colliders.spheres.size())
                    m_colliders.spheres[event.targetIndex].center = Vec3(event.value[0], event.value[1], event.value[2]);
                ++m_scenarioCollisionCandidateCount;
                break;
            case ScenarioEventType::InjectLocalEdit:
                ++m_editVersion;
                ++m_scenarioEditInvalidationCount;
                m_forceAllSegmentGroupsDirty = true;
                break;
            case ScenarioEventType::InjectRepairStorm:
                ++m_scenarioRepairAttemptCount;
                m_forceAllSegmentGroupsDirty = true;
                break;
            case ScenarioEventType::ForceAllDynamic:
                m_enableScalpMotion = true;
                m_forceAllSegmentGroupsDirty = true;
                break;
            case ScenarioEventType::ForceAllStatic:
                m_enableScalpMotion = false;
                m_simConfig.windStrength = 0.0f;
                break;
            case ScenarioEventType::SetCameraDistance:
                m_cameraDistance = std::max(event.value[0], 0.05f);
                break;
            default:
                SetFatalExperimentFailure(1401u, L"Unknown timeline event reached execution.");
                return;
            }
            ++m_scenarioAppliedEventCount;
            ++m_noveltyTelemetry.scenarioAppliedEventCount;
            m_noveltyTelemetry.availabilityMask |= hairgpu::HairTelemetryAvailability_Scenario;
        }
    }

    void GpuHairViewer::ApplyExperimentConfigBeforeAssetCreation()
    {
        if (!m_experimentCase.enabled) return;
        m_width = std::max<UINT>(m_experimentCase.requestedWidth, 64u);
        m_height = std::max<UINT>(m_experimentCase.requestedHeight, 64u);
        const uint32_t requestedGuides = m_experimentCase.requestedGuideCount;
        const uint32_t requestedDisplayStrands = m_experimentCase.requestedDisplayStrandCount;
        if (requestedGuides == 0u ||
            requestedDisplayStrands < requestedGuides ||
            (requestedDisplayStrands % requestedGuides) != 0u)
        {
            SetFatalExperimentFailure(1501u, L"Invalid exact-strand asset configuration reached asset creation.");
            return;
        }

        m_builderGuideCount = requestedGuides;
        m_builderFollowersPerGuide = requestedDisplayStrands / requestedGuides - 1u;
        m_builderParticlesPerStrand = std::max<uint32_t>(m_experimentCase.requestedParticlesPerGuide, 2u);
        m_vsyncEnabled = m_experimentCase.vsyncEnabled;
        m_presentSyncInterval = m_vsyncEnabled ? std::clamp<UINT>(m_experimentCase.presentSyncInterval, 1u, 4u) : 0u;
        m_enableActiveSetRuntime = m_experimentCase.appliedFlags.enableActiveSet;
    }

    bool GpuHairViewer::IsExperimentModeEnabled() const
    {
        return m_experimentCase.enabled;
    }

    bool GpuHairViewer::ShouldCaptureCurrentFrame() const
    {
        if (!m_experimentCase.enabled)
        {
            return true;
        }
        if (m_globalFrameIndex < m_experimentCase.warmupFrames)
        {
            return false;
        }
        const uint32_t measured = m_globalFrameIndex - m_experimentCase.warmupFrames;
        return measured < m_experimentCase.measurementFrames;
    }

    bool GpuHairViewer::ShouldAutoExitAfterCurrentFrame() const
    {
        if (!m_experimentCase.enabled || !m_experimentCase.autoExitAfterCapture)
        {
            return false;
        }
        const uint32_t totalFrames = m_experimentCase.warmupFrames + m_experimentCase.measurementFrames;
        return totalFrames > 0u && m_globalFrameIndex >= totalFrames;
    }

    bool GpuHairViewer::ExportRealtimeCadenceCsv(const std::wstring& filePath) const
    {
        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out)
        {
            return false;
        }
        out << "sampleIndex,globalFrameIndex,measuredFrameIndex,renderFps,"
               "simulationHz,renderFrameMilliseconds,cpuFrameWorkMilliseconds,"
               "simulationMilliseconds,gpuFrameMilliseconds,"
               "gpuSimulationMilliseconds,gpuHairDrawMilliseconds,"
               "gpuThroughputFps,simulationTicksThisFrame,"
               "droppedSimulationMilliseconds,presentSyncInterval,evidenceClass\n";
        out << std::fixed << std::setprecision(6);
        for (size_t sampleIndex = 0u; sampleIndex < m_performanceSamples.size(); ++sampleIndex)
        {
            const PerformanceSample& sample = m_performanceSamples[sampleIndex];
            const char* evidence = "interactive_realtime";
            switch (static_cast<PerformanceEvidenceClass>(sample.evidenceClass))
            {
            case PerformanceEvidenceClass::DiagnosticDebugger:
                evidence = "diagnostic_debugger";
                break;
            case PerformanceEvidenceClass::FormalThroughput:
                evidence = "formal_throughput";
                break;
            default:
                break;
            }
            out << sampleIndex << ','
                << sample.globalFrameIndex << ','
                << sample.measuredFrameIndex << ','
                << sample.renderFps << ','
                << sample.simulationHz << ','
                << sample.renderFrameMilliseconds << ','
                << sample.cpuFrameWorkMilliseconds << ','
                << sample.simulationMilliseconds << ','
                << sample.gpuFrameMilliseconds << ','
                << sample.gpuSimulationMilliseconds << ','
                << sample.gpuHairDrawMilliseconds << ','
                << sample.gpuThroughputFps << ','
                << sample.simulationTicksThisFrame << ','
                << sample.droppedSimulationMilliseconds << ','
                << sample.presentSyncInterval << ','
                << evidence << '\n';
        }
        return true;
    }


    bool GpuHairViewer::ExportPerSecondTelemetryCsv(const std::wstring& filePath) const
    {
        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out)
        {
            return false;
        }

        out << "secondIndex,wallTimeStartSeconds,wallTimeEndSeconds,durationSeconds,"
               "completeSecond,frameCount,simulationTickCount,renderFps,simulationHz,"
               "meanFrameMilliseconds,p95FrameMilliseconds,maxFrameMilliseconds,"
               "meanCpuWorkMilliseconds,meanSimulationMilliseconds,"
               "meanGpuFrameMilliseconds,meanGpuSimulationMilliseconds,"
               "meanGpuHairDrawMilliseconds,gpuTimingCoverage,meanGpuThroughputFps,"
               "droppedSimulationMillisecondsDelta,droppedSimulationMillisecondsEnd,"
               "nanStateCountEnd,infiniteStateCountEnd,fatalFailureEnd,"
               "collisionCandidateCountDelta,collisionMissCountDelta,"
               "wakeRequestGroupCountDelta,repairAttemptCountDelta,repairSuccessCountDelta,"
               "stateTransitionCountDelta,stateOscillationCountDelta,"
               "meanStaticCachedGroupCount,meanStaticTransformedGroupCount,"
               "meanDynamicGroupCount,meanRepairRequiredGroupCount,"
               "maxFalseStaticRate,maxBoundaryErrorMm,presentSyncInterval,"
               "evidenceClass,realtimeStatus\n";
        out << std::fixed << std::setprecision(6);

        if (m_performanceSamples.empty())
        {
            return true;
        }

        struct Bucket
        {
            uint32_t secondIndex = 0u;
            double startSeconds = 0.0;
            double durationSeconds = 0.0;
            uint32_t frameCount = 0u;
            uint64_t simulationTickCount = 0ull;
            double frameMillisecondsSum = 0.0;
            double cpuWorkMillisecondsSum = 0.0;
            double simulationMillisecondsSum = 0.0;
            double gpuFrameMillisecondsSum = 0.0;
            double gpuSimulationMillisecondsSum = 0.0;
            double gpuHairDrawMillisecondsSum = 0.0;
            double gpuThroughputFpsSum = 0.0;
            uint32_t gpuFrameSampleCount = 0u;
            uint32_t gpuSimulationSampleCount = 0u;
            uint32_t gpuHairDrawSampleCount = 0u;
            uint32_t gpuThroughputSampleCount = 0u;
            double staticCachedGroupCountSum = 0.0;
            double staticTransformedGroupCountSum = 0.0;
            double dynamicGroupCountSum = 0.0;
            double repairRequiredGroupCountSum = 0.0;
            float maxFalseStaticRate = 0.0f;
            float maxBoundaryErrorMm = 0.0f;
            std::vector<float> frameMilliseconds;
            PerformanceSample lastSample = {};
            bool hasSample = false;
        };

        auto evidenceName = [](uint32_t value) -> const char*
        {
            switch (static_cast<PerformanceEvidenceClass>(value))
            {
            case PerformanceEvidenceClass::DiagnosticDebugger:
                return "diagnostic_debugger";
            case PerformanceEvidenceClass::FormalThroughput:
                return "formal_throughput";
            default:
                return "interactive_realtime";
            }
        };

        auto safeDelta = [](uint64_t current, uint64_t previous) -> uint64_t
        {
            return current >= previous ? current - previous : current;
        };

        const PerformanceSample& firstCapturedSample = m_performanceSamples.front();
        uint64_t previousCollisionCandidateCount =
            firstCapturedSample.collisionCandidateCount;
        uint64_t previousCollisionMissCount =
            firstCapturedSample.collisionMissCount;
        uint64_t previousWakeRequestGroupCount =
            firstCapturedSample.wakeRequestGroupCount;
        uint64_t previousRepairAttemptCount =
            firstCapturedSample.repairAttemptCount;
        uint64_t previousRepairSuccessCount =
            firstCapturedSample.repairSuccessCount;
        uint64_t previousStateTransitionCount =
            firstCapturedSample.stateTransitionCount;
        uint64_t previousStateOscillationCount =
            firstCapturedSample.stateOscillationCount;
        double previousDroppedSimulationMilliseconds = std::max(
            0.0,
            static_cast<double>(firstCapturedSample.droppedSimulationMilliseconds));
        double wallTimeSeconds = 0.0;
        Bucket bucket = {};
        bucket.secondIndex = 1u;
        bucket.startSeconds = 0.0;

        auto flushBucket = [&](bool completeSecond)
        {
            if (!bucket.hasSample || bucket.frameCount == 0u)
            {
                return;
            }

            std::vector<float> orderedFrameMilliseconds = bucket.frameMilliseconds;
            std::sort(orderedFrameMilliseconds.begin(), orderedFrameMilliseconds.end());
            const size_t p95Index = orderedFrameMilliseconds.empty()
                ? 0u
                : static_cast<size_t>(std::ceil(
                    0.95 * static_cast<double>(orderedFrameMilliseconds.size() - 1u)));
            const float p95FrameMilliseconds = orderedFrameMilliseconds.empty()
                ? 0.0f
                : orderedFrameMilliseconds[std::min(p95Index, orderedFrameMilliseconds.size() - 1u)];
            const float maxFrameMilliseconds = orderedFrameMilliseconds.empty()
                ? 0.0f
                : orderedFrameMilliseconds.back();

            const double durationSeconds = std::max(bucket.durationSeconds, 1.0e-6);
            const double renderFps =
                static_cast<double>(bucket.frameCount) / durationSeconds;
            const double simulationHz =
                static_cast<double>(bucket.simulationTickCount) / durationSeconds;
            const double meanFrameMilliseconds =
                bucket.frameMillisecondsSum / static_cast<double>(bucket.frameCount);
            const double meanCpuWorkMilliseconds =
                bucket.cpuWorkMillisecondsSum / static_cast<double>(bucket.frameCount);
            const double meanSimulationMilliseconds =
                bucket.simulationMillisecondsSum / static_cast<double>(bucket.frameCount);
            const double meanGpuFrameMilliseconds = bucket.gpuFrameSampleCount > 0u
                ? bucket.gpuFrameMillisecondsSum / static_cast<double>(bucket.gpuFrameSampleCount)
                : 0.0;
            const double meanGpuSimulationMilliseconds = bucket.gpuSimulationSampleCount > 0u
                ? bucket.gpuSimulationMillisecondsSum / static_cast<double>(bucket.gpuSimulationSampleCount)
                : 0.0;
            const double meanGpuHairDrawMilliseconds = bucket.gpuHairDrawSampleCount > 0u
                ? bucket.gpuHairDrawMillisecondsSum / static_cast<double>(bucket.gpuHairDrawSampleCount)
                : 0.0;
            const double gpuTimingCoverage =
                static_cast<double>(bucket.gpuFrameSampleCount) /
                static_cast<double>(bucket.frameCount);
            const double meanGpuThroughputFps = bucket.gpuThroughputSampleCount > 0u
                ? bucket.gpuThroughputFpsSum / static_cast<double>(bucket.gpuThroughputSampleCount)
                : 0.0;
            const double droppedSimulationMillisecondsEnd =
                std::max(0.0, static_cast<double>(bucket.lastSample.droppedSimulationMilliseconds));
            const double droppedSimulationMillisecondsDelta =
                droppedSimulationMillisecondsEnd >= previousDroppedSimulationMilliseconds
                    ? droppedSimulationMillisecondsEnd - previousDroppedSimulationMilliseconds
                    : droppedSimulationMillisecondsEnd;

            const uint64_t collisionCandidateCountDelta = safeDelta(
                bucket.lastSample.collisionCandidateCount,
                previousCollisionCandidateCount);
            const uint64_t collisionMissCountDelta = safeDelta(
                bucket.lastSample.collisionMissCount,
                previousCollisionMissCount);
            const uint64_t wakeRequestGroupCountDelta = safeDelta(
                bucket.lastSample.wakeRequestGroupCount,
                previousWakeRequestGroupCount);
            const uint64_t repairAttemptCountDelta = safeDelta(
                bucket.lastSample.repairAttemptCount,
                previousRepairAttemptCount);
            const uint64_t repairSuccessCountDelta = safeDelta(
                bucket.lastSample.repairSuccessCount,
                previousRepairSuccessCount);
            const uint64_t stateTransitionCountDelta = safeDelta(
                bucket.lastSample.stateTransitionCount,
                previousStateTransitionCount);
            const uint64_t stateOscillationCountDelta = safeDelta(
                bucket.lastSample.stateOscillationCount,
                previousStateOscillationCount);

            const double sampleDenominator = static_cast<double>(bucket.frameCount);
            const double meanStaticCachedGroupCount =
                bucket.staticCachedGroupCountSum / sampleDenominator;
            const double meanStaticTransformedGroupCount =
                bucket.staticTransformedGroupCountSum / sampleDenominator;
            const double meanDynamicGroupCount =
                bucket.dynamicGroupCountSum / sampleDenominator;
            const double meanRepairRequiredGroupCount =
                bucket.repairRequiredGroupCountSum / sampleDenominator;

            const char* realtimeStatus = "realtime_60";
            if (bucket.lastSample.fatalFailure != 0u ||
                bucket.lastSample.nanStateCount != 0u ||
                bucket.lastSample.infiniteStateCount != 0u)
            {
                realtimeStatus = "invalid";
            }
            else if (simulationHz < 58.0 ||
                     droppedSimulationMillisecondsDelta > 1.0)
            {
                realtimeStatus = "simulation_lag";
            }
            else if (renderFps < 24.0)
            {
                realtimeStatus = "below_24";
            }
            else if (renderFps < 59.0)
            {
                realtimeStatus = "realtime_24";
            }

            out << bucket.secondIndex << ','
                << bucket.startSeconds << ','
                << (bucket.startSeconds + durationSeconds) << ','
                << durationSeconds << ','
                << (completeSecond ? 1 : 0) << ','
                << bucket.frameCount << ','
                << bucket.simulationTickCount << ','
                << renderFps << ','
                << simulationHz << ','
                << meanFrameMilliseconds << ','
                << p95FrameMilliseconds << ','
                << maxFrameMilliseconds << ','
                << meanCpuWorkMilliseconds << ','
                << meanSimulationMilliseconds << ','
                << meanGpuFrameMilliseconds << ','
                << meanGpuSimulationMilliseconds << ','
                << meanGpuHairDrawMilliseconds << ','
                << gpuTimingCoverage << ','
                << meanGpuThroughputFps << ','
                << droppedSimulationMillisecondsDelta << ','
                << droppedSimulationMillisecondsEnd << ','
                << bucket.lastSample.nanStateCount << ','
                << bucket.lastSample.infiniteStateCount << ','
                << bucket.lastSample.fatalFailure << ','
                << collisionCandidateCountDelta << ','
                << collisionMissCountDelta << ','
                << wakeRequestGroupCountDelta << ','
                << repairAttemptCountDelta << ','
                << repairSuccessCountDelta << ','
                << stateTransitionCountDelta << ','
                << stateOscillationCountDelta << ','
                << meanStaticCachedGroupCount << ','
                << meanStaticTransformedGroupCount << ','
                << meanDynamicGroupCount << ','
                << meanRepairRequiredGroupCount << ','
                << bucket.maxFalseStaticRate << ','
                << bucket.maxBoundaryErrorMm << ','
                << bucket.lastSample.presentSyncInterval << ','
                << evidenceName(bucket.lastSample.evidenceClass) << ','
                << realtimeStatus << '\n';

            previousDroppedSimulationMilliseconds = droppedSimulationMillisecondsEnd;
            previousCollisionCandidateCount = bucket.lastSample.collisionCandidateCount;
            previousCollisionMissCount = bucket.lastSample.collisionMissCount;
            previousWakeRequestGroupCount = bucket.lastSample.wakeRequestGroupCount;
            previousRepairAttemptCount = bucket.lastSample.repairAttemptCount;
            previousRepairSuccessCount = bucket.lastSample.repairSuccessCount;
            previousStateTransitionCount = bucket.lastSample.stateTransitionCount;
            previousStateOscillationCount = bucket.lastSample.stateOscillationCount;
        };

        for (const PerformanceSample& sample : m_performanceSamples)
        {
            float frameMilliseconds = sample.renderFrameMilliseconds;
            if (!std::isfinite(frameMilliseconds) || frameMilliseconds <= 0.0f)
            {
                frameMilliseconds = sample.frameMilliseconds;
            }
            if (!std::isfinite(frameMilliseconds) || frameMilliseconds <= 0.0f)
            {
                continue;
            }

            const double frameSeconds =
                static_cast<double>(frameMilliseconds) / 1000.0;
            if (bucket.hasSample &&
                bucket.durationSeconds + frameSeconds > 1.0 &&
                bucket.durationSeconds >= 0.50)
            {
                const uint32_t nextSecondIndex = bucket.secondIndex + 1u;
                const double completedBucketSeconds = bucket.durationSeconds;
                flushBucket(true);
                wallTimeSeconds += completedBucketSeconds;
                bucket = {};
                bucket.secondIndex = nextSecondIndex;
                bucket.startSeconds = wallTimeSeconds;
            }

            bucket.hasSample = true;
            ++bucket.frameCount;
            bucket.durationSeconds += frameSeconds;
            bucket.simulationTickCount += sample.simulationTicksThisFrame;
            bucket.frameMillisecondsSum += frameMilliseconds;
            bucket.cpuWorkMillisecondsSum += sample.cpuFrameWorkMilliseconds;
            bucket.simulationMillisecondsSum += sample.simulationMilliseconds;
            bucket.frameMilliseconds.push_back(frameMilliseconds);

            if (std::isfinite(sample.gpuFrameMilliseconds) &&
                sample.gpuFrameMilliseconds > 0.0f)
            {
                bucket.gpuFrameMillisecondsSum += sample.gpuFrameMilliseconds;
                ++bucket.gpuFrameSampleCount;
            }
            if (std::isfinite(sample.gpuSimulationMilliseconds) &&
                sample.gpuSimulationMilliseconds > 0.0f)
            {
                bucket.gpuSimulationMillisecondsSum += sample.gpuSimulationMilliseconds;
                ++bucket.gpuSimulationSampleCount;
            }
            if (std::isfinite(sample.gpuHairDrawMilliseconds) &&
                sample.gpuHairDrawMilliseconds > 0.0f)
            {
                bucket.gpuHairDrawMillisecondsSum += sample.gpuHairDrawMilliseconds;
                ++bucket.gpuHairDrawSampleCount;
            }
            if (std::isfinite(sample.gpuThroughputFps) &&
                sample.gpuThroughputFps > 0.0f)
            {
                bucket.gpuThroughputFpsSum += sample.gpuThroughputFps;
                ++bucket.gpuThroughputSampleCount;
            }

            bucket.staticCachedGroupCountSum += sample.staticCachedGroupCount;
            bucket.staticTransformedGroupCountSum += sample.staticTransformedGroupCount;
            bucket.dynamicGroupCountSum += sample.dynamicGroupCount;
            bucket.repairRequiredGroupCountSum += sample.repairRequiredGroupCount;
            bucket.maxFalseStaticRate = std::max(
                bucket.maxFalseStaticRate,
                sample.falseStaticRate);
            bucket.maxBoundaryErrorMm = std::max(
                bucket.maxBoundaryErrorMm,
                sample.staticDynamicBoundaryMaxErrorMm);
            bucket.lastSample = sample;
        }

        if (bucket.hasSample)
        {
            flushBucket(bucket.durationSeconds >= 0.95);
        }

        return true;
    }

    GpuHairViewer::PerformanceSample GpuHairViewer::BuildPerformanceSample(float frameSeconds, float instantFps) const
    {
        PerformanceSample sample = {};
        sample.elapsedSeconds = m_elapsedSeconds;
        sample.frameMilliseconds = frameSeconds * 1000.0f;
        sample.frameIntervalMilliseconds = m_lastFrameIntervalMilliseconds;
        sample.cpuFrameWorkMilliseconds = m_lastCpuFrameWorkMilliseconds;
        sample.simulationMilliseconds = m_lastSimulationStepMilliseconds;
        sample.fps = instantFps;
        sample.renderFps = instantFps;
        sample.simulationHz = m_simulationHz;
        sample.renderFrameMilliseconds = frameSeconds * 1000.0f;
        sample.gpuFrameMilliseconds = 0.0f;
        sample.gpuSimulationMilliseconds = 0.0f;
        sample.gpuHairDrawMilliseconds = 0.0f;
        sample.gpuThroughputFps = 0.0f;
        sample.simulationTicksThisFrame = m_simulationTicksThisFrame;
        sample.droppedSimulationMilliseconds = m_droppedSimulationMilliseconds;
        sample.presentSyncInterval = m_lastObservedPresentSyncInterval;
        sample.evidenceClass = static_cast<uint32_t>(
            IsExperimentModeEnabled()
                ? PerformanceEvidenceClass::FormalThroughput
                : (m_localDebugCaptureActive
                    ? PerformanceEvidenceClass::DiagnosticDebugger
                    : PerformanceEvidenceClass::InteractiveRealtime));
        sample.guideCount = m_asset.guideStrandCount;
        sample.followerCount = m_asset.followerStrandCount;
        sample.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
        sample.totalParticleCount = static_cast<uint32_t>(m_asset.restPoints.size());
        sample.activeSetClusterCount = m_activeSetPlan.summary.clusterCount;
        sample.activeSetSelectedClusterCount = m_activeSetPlan.summary.selectedClusterCount;
        sample.activeSetSelectedGuideCount = m_activeSetPlan.summary.selectedGuideCount;
        sample.activeSetSelectedStrandCount = m_activeSetPlan.summary.selectedStrandCount;
        sample.activeSetHighestScore = m_activeSetPlan.summary.highestScore;
        sample.activeSetAverageSelectedScore = m_activeSetPlan.summary.averageSelectedScore;
        sample.activeSetEvaluationMilliseconds = m_activeSetEvaluationMilliseconds;
        sample.activeSetBudgetScale = m_activeSetPlan.summary.budgetScale;
        sample.activeSetRecommendedSolverIterations = m_activeSetPlan.summary.recommendedSolverIterations;
        sample.fullTierCount = m_activeSetPlan.summary.fullTierCount;
        sample.reducedTierCount = m_activeSetPlan.summary.reducedTierCount;
        sample.frozenTierCount = m_activeSetPlan.summary.frozenTierCount;
        sample.cachedTierCount = m_activeSetPlan.summary.cachedTierCount;
        sample.correctiveStrandCount = m_sparseCorrectiveReport.correctedStrandCount;
        sample.correctivePointCount = m_sparseCorrectiveReport.correctedPointCount;
        sample.correctiveAveragePenetration = m_sparseCorrectiveReport.averagePenetration;
        sample.correctiveMaxPenetration = m_sparseCorrectiveReport.maxPenetration;
        sample.correctiveAverageLengthErrorBefore = m_sparseCorrectiveReport.averageLengthErrorBefore;
        sample.correctiveAverageLengthErrorAfter = m_sparseCorrectiveReport.averageLengthErrorAfter;

        sample.globalFrameIndex = m_globalFrameIndex;
        sample.measuredFrameIndex = m_measuredFrameIndex;
        sample.sourceFrameIndex = m_noveltyTelemetry.sourceFrameIndex;
        sample.availabilityMask = m_noveltyTelemetry.availabilityMask | hairgpu::HairTelemetryAvailability_FrameInterval | hairgpu::HairTelemetryAvailability_CpuFrameWork | hairgpu::HairTelemetryAvailability_ObservedSettings;
        sample.metricAvailabilityMask = m_noveltyTelemetry.metricAvailabilityMask;
        sample.unavailableMetricCount = m_noveltyTelemetry.unavailableMetricCount;
        sample.syntheticMetricCount = m_noveltyTelemetry.syntheticMetricCount;
        sample.fullCpuGroupUploadCount = m_fullCpuGroupUploadCount;
        sample.sameFrameReadbackCount = m_sameFrameReadbackCount;
        // P0 visual gate: non-finite position counters are emitted on every measured frame.
        sample.nanStateCount = m_nanStateCount;
        sample.infiniteStateCount = m_infiniteStateCount;
        sample.fatalFailure = m_noveltyTelemetry.fatalFailure;
        sample.fatalFailureCode = m_noveltyTelemetry.fatalFailureCode;
        sample.scenarioAppliedEventCount = m_scenarioAppliedEventCount;
        sample.scenarioAssertionFailureCount = m_scenarioAssertionFailureCount;
        sample.collisionCandidateCount = m_noveltyTelemetry.collisionCandidateCount;
        sample.collisionMissCount = m_noveltyTelemetry.collisionMissCount;
        sample.wakeRequestGroupCount = m_noveltyTelemetry.wakeRequestGroupCount;
        sample.repairAttemptCount = m_noveltyTelemetry.repairAttemptCount;
        sample.repairSuccessCount = m_noveltyTelemetry.repairSuccessCount;
        sample.stateTransitionCount = m_noveltyTelemetry.stateTransitionCount;
        sample.stateOscillationCount = m_noveltyTelemetry.stateOscillationCount;
        sample.warmupFrames = m_experimentCase.warmupFrames;
        sample.measurementFrames = m_experimentCase.measurementFrames;
        sample.seed = m_experimentCase.seed;
        sample.certificateFailureCount = 0u;
        sample.certificateFailureRate = 0.0f;
        sample.fallbackCount = 0u;
        sample.modeSkipCount = 0u;
        sample.modeQSOnlyCount = sample.activeSetSelectedStrandCount;
        sample.modeDynamicCount = 0u;
        sample.modeIslandCount = 0u;
        sample.modeRepairCount = sample.correctiveStrandCount;
        sample.modeFloatFallbackCount = 0u;
        sample.islandCount = 0u;
        sample.islandMemberCount = 0u;
        sample.islandCommitCount = 0u;
        sample.islandRejectCount = 0u;
        sample.repairTaskCount = m_noveltyTelemetry.repairAttemptCount;
        sample.repairCommittedCount = m_noveltyTelemetry.repairVerifiedCount;
        sample.repairRejectedCount = m_noveltyTelemetry.repairFailureCount;
        sample.repairDeferredCount = m_noveltyTelemetry.repairUnverifiedCount;
        sample.repairSuccessRate = m_noveltyTelemetry.repairSuccessRate;
        sample.cpuReadbackCount = m_noveltyTelemetry.cpuReadbackCount;
        // Filled only when the delayed timestamp query for this source frame resolves.
        sample.gpuHairMilliseconds = 0.0f;
        sample.vramMegabytes = m_activeSetPlan.summary.estimatedMemoryMegabytes;
        sample.bandwidthMegabytesPerFrame = 0.0f;
        sample.meanDecodeErrorMm = 0.0f;
        sample.maxPenetrationMm = sample.correctiveMaxPenetration * 1000.0f;

