// GpuHairViewer.cpp ordered part 10: phase1_telemetry_csv_jsonl_summary
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

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

    void GpuHairViewer::EvaluateScalpCoverageTint(float u, float v, float& r, float& g, float& b) const
    {
        const float dx = u - 0.58f;
        const float dy = v - 0.28f;
        const float crown = hairgpu::Clamp(1.0f - std::sqrt(dx * dx * 2.4f + dy * dy * 1.4f) / 0.34f, 0.0f, 1.0f);

        const float partWidth = std::max(m_scalpPartLineWidth, 1.0e-4f);
        const float partLine = std::exp(-(dx * dx) / std::max(partWidth * partWidth, 1.0e-5f)) * std::exp(-(dy * dy) / 0.070f);

        const float shoulderOffset = std::max(partWidth * 1.15f, 0.022f);
        const float shoulderWidth = std::max(partWidth * 1.75f, 0.040f);
        const float shoulderL = std::exp(-((dx + shoulderOffset) * (dx + shoulderOffset)) / std::max(shoulderWidth * shoulderWidth, 1.0e-5f)) * std::exp(-(dy * dy) / 0.085f);
        const float shoulderR = std::exp(-((dx - shoulderOffset) * (dx - shoulderOffset)) / std::max(shoulderWidth * shoulderWidth, 1.0e-5f)) * std::exp(-(dy * dy) / 0.085f);
        const float frontBand = std::exp(-((v - 0.84f) * (v - 0.84f)) / 0.018f);

        const float rawCoverage = 0.18f + 0.58f * crown - 0.34f * partLine + 0.26f * (shoulderL + shoulderR) + 0.10f * frontBand;
        const float coverage = hairgpu::Clamp(rawCoverage * m_scalpCoverageStrength, 0.0f, 1.0f);

        const float darkness = hairgpu::Clamp(m_scalpTintDarkness, 0.0f, 1.0f);
        const float hairBlend = hairgpu::Clamp(m_scalpHairBlend, 0.0f, 1.0f);

        const float scalpBaseR = 0.46f, scalpBaseG = 0.34f, scalpBaseB = 0.25f;
        const float scalpDarkR = 0.26f, scalpDarkG = 0.18f, scalpDarkB = 0.12f;
        const float hairBaseR = 0.18f, hairBaseG = 0.12f, hairBaseB = 0.08f;
        const float hairTintR = 0.14f, hairTintG = 0.10f, hairTintB = 0.07f;

        const float scalpR = scalpBaseR * (1.0f - darkness) + scalpDarkR * darkness;
        const float scalpG = scalpBaseG * (1.0f - darkness) + scalpDarkG * darkness;
        const float scalpB = scalpBaseB * (1.0f - darkness) + scalpDarkB * darkness;

        const float hairR = hairBaseR * (1.0f - hairBlend) + hairTintR * hairBlend;
        const float hairG = hairBaseG * (1.0f - hairBlend) + hairTintG * hairBlend;
        const float hairB = hairBaseB * (1.0f - hairBlend) + hairTintB * hairBlend;

        const float partLight = hairgpu::Clamp(0.08f + 0.40f * partLine, 0.0f, 0.38f);

        r = scalpR * (1.0f - coverage) + hairR * coverage + partLight * 0.08f;
        g = scalpG * (1.0f - coverage) + hairG * coverage + partLight * 0.06f;
        b = scalpB * (1.0f - coverage) + hairB * coverage + partLight * 0.04f;

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

    // HAIRGPU_EXPERIMENT_RUNTIME_PATCH_V1
    bool GpuHairViewer::LoadExperimentCaseFromEnvironment()
    {
        m_experimentCase = {};
        const std::wstring caseJsonPath = GetEnvironmentVariableString(L"HAIRGPU_CASE_JSON");
        std::string caseJson;
        const auto isJsonSpace = [](char c) { return c == ' ' || c == '	' || c == '' || c == '
'; };
        if (!caseJsonPath.empty())
        {
            std::ifstream input(std::filesystem::path(caseJsonPath), std::ios::binary);
            if (!input) return false;
            caseJson.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            m_experimentCase.caseJsonPath = caseJsonPath;
        }
        const auto jsonValue = [&caseJson, &isJsonSpace](const std::string& key) -> std::string
        {
            if (caseJson.empty()) return {};
            const std::string needle = "\"" + key + "\"";
            size_t pos = caseJson.find(needle);
            if (pos == std::string::npos) return {};
            pos = caseJson.find(':', pos + needle.size());
            if (pos == std::string::npos) return {};
            ++pos;
            while (pos < caseJson.size() && isJsonSpace(caseJson[pos])) ++pos;
            if (pos < caseJson.size() && caseJson[pos] == '"')
            {
                ++pos;
                std::string out;
                bool escaped = false;
                for (; pos < caseJson.size(); ++pos)
                {
                    const char ch = caseJson[pos];
                    if (!escaped && ch == '"') break;
                    if (!escaped && ch == '\') { escaped = true; continue; }
                    out.push_back(ch);
                    escaped = false;
                }
                return out;
            }
            size_t end = pos;
            while (end < caseJson.size() && caseJson[end] != ',' && caseJson[end] != '}' && caseJson[end] != ']') ++end;
            std::string token = caseJson.substr(pos, end - pos);
            while (!token.empty() && isJsonSpace(token.front())) token.erase(token.begin());
            while (!token.empty() && isJsonSpace(token.back())) token.pop_back();
            return token;
        };
        const auto utf8ToWideLocal = [](const std::string& raw) -> std::wstring
        {
            if (raw.empty()) return {};
            const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
            if (required <= 0) return std::wstring(raw.begin(), raw.end());
            std::wstring wide(static_cast<size_t>(required), L' ');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), wide.data(), required);
            return wide;
        };
        const auto getString = [&](const char* jsonKey, const wchar_t* envKey, const std::wstring& fallback = L"")
        {
            const std::string raw = jsonValue(jsonKey);
            if (!raw.empty()) return utf8ToWideLocal(raw);
            const std::wstring env = GetEnvironmentVariableString(envKey);
            return env.empty() ? fallback : env;
        };
        const auto getUInt = [&](const char* jsonKey, const wchar_t* envKey, uint32_t fallback)
        {
            const std::string raw = jsonValue(jsonKey);
            if (!raw.empty()) { try { return static_cast<uint32_t>(std::stoul(raw)); } catch (...) {} }
            return EnvironmentValueToUInt(GetEnvironmentVariableString(envKey), fallback);
        };
        const auto getFloat = [&](const char* jsonKey, const wchar_t* envKey, float fallback)
        {
            std::string raw = jsonValue(jsonKey);
            if (raw.empty()) raw = WideToUtf8(GetEnvironmentVariableString(envKey));
            if (raw.empty()) return fallback;
            try { return std::stof(raw); } catch (...) { return fallback; }
        };
        const auto getBool = [&](const char* jsonKey, const wchar_t* envKey, bool fallback)
        {
            std::string raw = jsonValue(jsonKey);
            std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (raw.empty()) return GetEnvironmentVariableString(envKey).empty() ? fallback : EnvironmentValueIsTrue(GetEnvironmentVariableString(envKey));
            return raw == "true" || raw == "1" || raw == "yes" || raw == "on";
        };

        m_experimentCase.caseId = getString("case_id", L"HAIRGPU_EXPERIMENT_CASE_ID");
        if (m_experimentCase.caseId.empty()) return caseJsonPath.empty();
        m_experimentCase.enabled = true;
        m_experimentCase.variant = getString("method", L"HAIRGPU_VARIANT", getString("variant", L"HAIRGPU_VARIANT"));
        m_experimentCase.scenario = getString("scenario", L"HAIRGPU_SCENARIO");
        m_experimentCase.measurementModeName = getString("measurement_mode", L"HAIRGPU_MEASUREMENT_MODE", L"performance");
        m_experimentCase.scenarioTimelinePath = getString("scenario_timeline", L"HAIRGPU_SCENARIO_TIMELINE");
        m_experimentCase.assetManifestPath = getString("asset_manifest", L"HAIRGPU_ASSET_MANIFEST");
        m_experimentCase.settingsHash = getString("settings_hash", L"HAIRGPU_SETTINGS_HASH");
        m_experimentCase.executableSha256 = getString("executableSha256", L"HAIRGPU_EXECUTABLE_SHA256");
        m_experimentCase.commitSha = getString("commitSha", L"HAIRGPU_COMMIT_SHA");
        m_experimentCase.buildConfiguration = getString("build_configuration", L"HAIRGPU_BUILD_CONFIGURATION", L"Release");
        m_experimentCase.platform = getString("platform", L"HAIRGPU_PLATFORM", L"x64");
        m_experimentCase.hardwareProfile = getString("hardware_profile", L"HAIRGPU_HARDWARE_PROFILE");
        m_experimentCase.scenarioTimelineSha256 = getString("scenarioTimelineSha256", L"HAIRGPU_SCENARIO_TIMELINE_SHA256");
        m_experimentCase.assetManifestSha256 = getString("assetManifestSha256", L"HAIRGPU_ASSET_MANIFEST_SHA256");
        m_experimentCase.assetSha256 = getString("assetSha256", L"HAIRGPU_ASSET_SHA256");
        m_experimentCase.appliedMethodSettingsSha256 = getString("appliedMethodSettingsSha256", L"HAIRGPU_APPLIED_METHOD_SETTINGS_SHA256");
        m_experimentCase.assetFrozen = getBool("assetFrozen", L"HAIRGPU_ASSET_FROZEN", false);
        m_experimentCase.seed = getUInt("seed", L"HAIRGPU_SEED", 0u);
        m_experimentCase.repetitionIndex = getUInt("repeat_index", L"HAIRGPU_REPEAT_INDEX", 0u);
        m_experimentCase.warmupFrames = getUInt("warmup_frames", L"HAIRGPU_WARMUP_FRAMES", 0u);
        m_experimentCase.measurementFrames = getUInt("measurement_frames", L"HAIRGPU_MEASUREMENT_FRAMES", 0u);
        m_experimentCase.autoExitAfterCapture = getBool("auto_exit_after_capture", L"HAIRGPU_AUTO_EXIT_AFTER_CAPTURE", true);
        m_experimentCase.requestedWidth = getUInt("width", L"HAIRGPU_RENDER_WIDTH", m_width);
        m_experimentCase.requestedHeight = getUInt("height", L"HAIRGPU_RENDER_HEIGHT", m_height);
        m_experimentCase.requestedGuideCount = getUInt("guide_strand_count", L"HAIRGPU_GUIDE_STRAND_COUNT", m_builderGuideCount);
        m_experimentCase.requestedDisplayStrandCount = getUInt("display_strand_count", L"HAIRGPU_DISPLAY_STRAND_COUNT", 0u);
        m_experimentCase.requestedParticlesPerGuide = getUInt("particles_per_guide", L"HAIRGPU_PARTICLES_PER_GUIDE", m_builderParticlesPerStrand);
        m_experimentCase.requestedSolverIterations = getUInt("solver_iterations", L"HAIRGPU_SOLVER_ITERATIONS", m_simConfig.solverIterations);
        m_experimentCase.fixedTimeStepEnabled = getBool("fixed_timestep", L"HAIRGPU_FIXED_TIMESTEP_ENABLED", true);
        m_experimentCase.fixedTimeStepSeconds = getFloat("fixed_timestep_seconds", L"HAIRGPU_FIXED_TIMESTEP_SECONDS", m_simConfig.fixedTimeStep);
        m_experimentCase.vsyncEnabled = getBool("vsync", L"HAIRGPU_VSYNC", false);
        m_experimentCase.presentSyncInterval = getUInt("present_sync_interval", L"HAIRGPU_PRESENT_SYNC_INTERVAL", m_experimentCase.vsyncEnabled ? 1u : 0u);
        m_experimentCase.gpuTimestampDelayFrames = std::max<uint32_t>(2u, getUInt("gpu_timestamp_delay_frames", L"HAIRGPU_GPU_TIMESTAMP_DELAY_FRAMES", 4u));
        m_experimentCase.scenarioAssertionMinAppliedEvents = getUInt("min_scenario_applied_events", L"HAIRGPU_ASSERT_MIN_SCENARIO_EVENTS", 0u);
        m_experimentCase.scenarioAssertionMinCollisionCandidates = getUInt("min_collision_candidates", L"HAIRGPU_ASSERT_MIN_COLLISION_CANDIDATES", 0u);
        m_experimentCase.scenarioAssertionMinWakeRequests = getUInt("min_wake_requests", L"HAIRGPU_ASSERT_MIN_WAKE_REQUESTS", 0u);
        m_experimentCase.scenarioAssertionMinRepairs = getUInt("min_repair_attempts", L"HAIRGPU_ASSERT_MIN_REPAIRS", 0u);
        m_experimentCase.scenarioAssertionMinTransitions = getUInt("min_state_transitions", L"HAIRGPU_ASSERT_MIN_TRANSITIONS", 0u);

        std::wstring mode = m_experimentCase.measurementModeName;
        std::transform(mode.begin(), mode.end(), mode.begin(), [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c; });
        m_experimentCase.measurementMode = mode == L"quality" ? MeasurementMode::Quality : mode == L"determinism" ? MeasurementMode::Determinism : mode == L"robustness" ? MeasurementMode::Robustness : MeasurementMode::Performance;

        ExperimentMethodFlags& requested = m_experimentCase.requestedFlags;
#define HAIRGPU_READ_FLAG(member, jsonKey, envKey, fallback) requested.member = getBool(jsonKey, envKey, fallback)
        HAIRGPU_READ_FLAG(enableNoNoveltyBaseline, "enableNoNoveltyBaseline", L"HAIRGPU_ENABLE_NO_NOVELTY_BASELINE", false);
        HAIRGPU_READ_FLAG(enableMicrotype, "enableMicrotype", L"HAIRGPU_ENABLE_MICROTYPE", false);
        HAIRGPU_READ_FLAG(enableBudgetedPhysics, "enableBudgetedPhysics", L"HAIRGPU_ENABLE_BUDGETED_PHYSICS", false);
        HAIRGPU_READ_FLAG(enableCertificateRender, "enableCertificateRender", L"HAIRGPU_ENABLE_CERTIFICATE_RENDER", false);
        HAIRGPU_READ_FLAG(enablePerStrandEdit, "enablePerStrandEdit", L"HAIRGPU_ENABLE_PER_STRAND_EDIT", false);
        HAIRGPU_READ_FLAG(enableCBMA, "enableCBMA", L"HAIRGPU_ENABLE_CBMA", false);
        HAIRGPU_READ_FLAG(enablePhysicsIslands, "enablePhysicsIslands", L"HAIRGPU_ENABLE_PHYSICS_ISLANDS", false);
        HAIRGPU_READ_FLAG(enableRodRepair, "enableRodRepair", L"HAIRGPU_ENABLE_ROD_REPAIR", false);
        HAIRGPU_READ_FLAG(enableGpuResident, "enableGpuResident", L"HAIRGPU_ENABLE_GPU_RESIDENT", false);
        HAIRGPU_READ_FLAG(enableSegmentGroupSleep, "enableSegmentGroupSleep", L"HAIRGPU_ENABLE_SEGMENT_GROUP_SLEEP", false);
        HAIRGPU_READ_FLAG(enableSegmentGroupIntegratedState, "enableSegmentGroupIntegratedState", L"HAIRGPU_ENABLE_SEGMENT_GROUP_INTEGRATED_STATE", true);
        HAIRGPU_READ_FLAG(enableExternalStateArray, "enableExternalStateArray", L"HAIRGPU_ENABLE_EXTERNAL_STATE_ARRAY", false);
        HAIRGPU_READ_FLAG(enableSharedPointOwnership, "enableSharedPointOwnership", L"HAIRGPU_ENABLE_SHARED_POINT_OWNERSHIP", true);
        HAIRGPU_READ_FLAG(enableDirectionalResponsibility, "enableDirectionalResponsibility", L"HAIRGPU_ENABLE_DIRECTIONAL_RESPONSIBILITY", true);
        HAIRGPU_READ_FLAG(enableStaticCertificate, "enableStaticCertificate", L"HAIRGPU_ENABLE_STATIC_CERTIFICATE", true);
        HAIRGPU_READ_FLAG(enablePredictiveWake, "enablePredictiveWake", L"HAIRGPU_ENABLE_PREDICTIVE_WAKE", true);
        HAIRGPU_READ_FLAG(enableVelocityRecovery, "enableVelocityRecovery", L"HAIRGPU_ENABLE_VELOCITY_RECOVERY", true);
        HAIRGPU_READ_FLAG(enableRepair, "enableRepair", L"HAIRGPU_ENABLE_REPAIR", true);
        HAIRGPU_READ_FLAG(enableGpuCompaction, "enableGpuCompaction", L"HAIRGPU_ENABLE_GPU_COMPACTION", true);
        HAIRGPU_READ_FLAG(enableActiveSet, "enableActiveSet", L"HAIRGPU_ENABLE_ACTIVE_SET", true);
        HAIRGPU_READ_FLAG(enableStrandSleep, "enableStrandSleep", L"HAIRGPU_ENABLE_STRAND_SLEEP", false);
        HAIRGPU_READ_FLAG(enableBlockSleep, "enableBlockSleep", L"HAIRGPU_ENABLE_BLOCK_SLEEP", false);
        HAIRGPU_READ_FLAG(enableSleepHysteresis, "enableSleepHysteresis", L"HAIRGPU_ENABLE_SLEEP_HYSTERESIS", true);
        HAIRGPU_READ_FLAG(enableStaticCache, "enableStaticCache", L"HAIRGPU_ENABLE_STATIC_CACHE", true);
#undef HAIRGPU_READ_FLAG
        requested.stateStorage = requested.enableExternalStateArray ? SegmentGroupStateStorage::ExternalArray : SegmentGroupStateStorage::IntegratedRecord;
        m_experimentCase.appliedFlags = requested;
        if (requested.enableNoNoveltyBaseline)
        {
            m_experimentCase.appliedFlags = {};
            m_experimentCase.appliedFlags.enableNoNoveltyBaseline = true;
            m_experimentCase.appliedFlags.enableSegmentGroupIntegratedState = false;
            m_experimentCase.appliedFlags.enableSharedPointOwnership = false;
            m_experimentCase.appliedFlags.enableDirectionalResponsibility = false;
            m_experimentCase.appliedFlags.enableStaticCertificate = false;
            m_experimentCase.appliedFlags.enablePredictiveWake = false;
            m_experimentCase.appliedFlags.enableVelocityRecovery = false;
            m_experimentCase.appliedFlags.enableRepair = false;
            m_experimentCase.appliedFlags.enableGpuCompaction = false;
            m_experimentCase.appliedFlags.enableActiveSet = false;
            m_experimentCase.appliedFlags.enableSleepHysteresis = false;
            m_experimentCase.appliedFlags.enableStaticCache = false;
        }

        const ExperimentMethodFlags& applied = m_experimentCase.appliedFlags;
        m_experimentCase.enableNoNoveltyBaseline = applied.enableNoNoveltyBaseline;
        m_experimentCase.enableMicrotype = applied.enableMicrotype;
        m_experimentCase.enableBudgetedPhysics = applied.enableBudgetedPhysics;
        m_experimentCase.enableCertificateRender = applied.enableCertificateRender;
        m_experimentCase.enablePerStrandEdit = applied.enablePerStrandEdit;
        m_experimentCase.enableCBMA = applied.enableCBMA;
        m_experimentCase.enablePhysicsIslands = applied.enablePhysicsIslands;
        m_experimentCase.enableRodRepair = applied.enableRodRepair;
        m_experimentCase.enableGpuResident = applied.enableGpuResident;

        m_noveltyFlags = hairgpu::NormalizeNoveltyFlags(
            applied.enableMicrotype ? 1u : 0u,
            applied.enableBudgetedPhysics ? 1u : 0u,
            applied.enableCertificateRender ? 1u : 0u,
            applied.enablePerStrandEdit ? 1u : 0u,
            applied.enableGpuResident ? 1u : 0u,
            applied.enableCBMA ? 1u : 0u,
            applied.enablePhysicsIslands ? 1u : 0u,
            applied.enableRodRepair ? 1u : 0u,
            applied.enableNoNoveltyBaseline ? 1u : 0u);
        m_noveltyFlags.enableMicrotypeCertificate = applied.enableStaticCertificate ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeDirtyPropagation = applied.enableSegmentGroupSleep ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeBlockHierarchy = applied.enableBlockSleep ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeBoundaryRepair = applied.enableRepair ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeWatchdog = applied.enableRepair ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeHemisphereOwnership = applied.enableDirectionalResponsibility ? 1u : 0u;
        m_noveltyFlags.enableMicrotypeIndirectDispatch = applied.enableGpuCompaction ? 1u : 0u;
        m_noveltyFlags = hairgpu::NormalizeMicrotypeAblationFlags(m_noveltyFlags);
        m_segmentGroupsPerBlock = std::max<uint32_t>(1u, getUInt("groupsPerBlock", L"HAIRGPU_MICROTYPE_GROUPS_PER_BLOCK", m_segmentGroupsPerBlock));
        m_segmentGroupWatchdogIntervalFrames = std::max<uint32_t>(1u, getUInt("watchdogIntervalFrames", L"HAIRGPU_MICROTYPE_WATCHDOG_INTERVAL", m_segmentGroupWatchdogIntervalFrames));
        m_globalHairBudget = hairgpu::BuildGlobalHairFrameBudget(m_noveltyFlags);
        m_segmentGroupTopologyDirty = true;
        m_forceAllSegmentGroupsDirty = true;
        return true;
    }

    void GpuHairViewer::ApplyExperimentConfigBeforeAssetCreation()
    {
        if (!m_experimentCase.enabled) return;
        m_width = std::max<UINT>(m_experimentCase.requestedWidth, 64u);
        m_height = std::max<UINT>(m_experimentCase.requestedHeight, 64u);
        m_builderGuideCount = std::max<uint32_t>(m_experimentCase.requestedGuideCount, 1u);
        m_builderParticlesPerStrand = std::max<uint32_t>(m_experimentCase.requestedParticlesPerGuide, 2u);
        if (m_experimentCase.requestedDisplayStrandCount > m_builderGuideCount)
        {
            const uint32_t followers = m_experimentCase.requestedDisplayStrandCount - m_builderGuideCount;
            m_builderFollowersPerGuide = std::max<uint32_t>(followers / m_builderGuideCount, 1u);
        }
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

    GpuHairViewer::PerformanceSample GpuHairViewer::BuildPerformanceSample(float frameSeconds, float instantFps) const
    {
        PerformanceSample sample = {};
        sample.elapsedSeconds = m_elapsedSeconds;
        sample.frameMilliseconds = frameSeconds * 1000.0f;
        sample.frameIntervalMilliseconds = m_lastFrameIntervalMilliseconds;
        sample.cpuFrameWorkMilliseconds = m_lastCpuFrameWorkMilliseconds;
        sample.simulationMilliseconds = m_lastSimulationStepMilliseconds;
        sample.fps = instantFps;
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
        sample.repairTaskCount = sample.correctiveStrandCount;
        sample.repairCommittedCount = sample.correctiveStrandCount;
        sample.repairRejectedCount = 0u;
        sample.repairDeferredCount = 0u;
        sample.repairSuccessRate = sample.repairTaskCount > 0u ? static_cast<float>(sample.repairCommittedCount) / static_cast<float>(sample.repairTaskCount) : 0.0f;
        sample.cpuReadbackCount = 0u;
        // Filled only when the delayed timestamp query for this source frame resolves.
        sample.gpuHairMilliseconds = 0.0f;
        sample.vramMegabytes = m_activeSetPlan.summary.estimatedMemoryMegabytes;
        sample.bandwidthMegabytesPerFrame = 0.0f;
        sample.meanDecodeErrorMm = 0.0f;
        sample.maxPenetrationMm = sample.correctiveMaxPenetration * 1000.0f;

