// GpuHairViewer.cpp ordered part 07: draw_path_and_gpu_timing
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

namespace
{
    constexpr UINT kHairGpuNonBlockingQueryFlags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
}

    const wchar_t* GpuHairViewer::GetGroomToolDisplayName() const
    {
        switch (m_groomTool)
        {
        case GroomTool::Select: return L"Select";
        case GroomTool::Comb: return L"Comb";
        case GroomTool::Grab: return L"Grab";
        case GroomTool::Smooth: return L"Smooth";
        case GroomTool::Length: return L"Length";
        case GroomTool::Volume: return L"Volume";
        case GroomTool::Part: return L"Part";
        case GroomTool::Freeze: return L"Freeze";
        case GroomTool::StrandFix: return L"Strand Fix";
        case GroomTool::Clump: return L"Clump";
        case GroomTool::Curl: return L"Curl";
        case GroomTool::Unclump: return L"Unclump";
        default: return L"Comb";
        }
    }

    const wchar_t* GpuHairViewer::GetBottomTrayDisplayName() const
    {
        switch (m_bottomTrayTab)
        {
        case BottomTrayTab::Layers: return L"Layers";
        case BottomTrayTab::History: return L"History";
        case BottomTrayTab::Metrics: return L"Metrics";
        default: return L"Layers";
        }
    }

    bool GpuHairViewer::IsGroomWorkspace() const { return m_workspaceMode == WorkspaceMode::Groom; }
    bool GpuHairViewer::IsSimWorkspace() const { return m_workspaceMode == WorkspaceMode::Sim; }
    bool GpuHairViewer::IsReviewWorkspace() const { return m_workspaceMode == WorkspaceMode::Review; }
    bool GpuHairViewer::IsReportWorkspace() const { return m_workspaceMode == WorkspaceMode::Report; }

    void GpuHairViewer::LayoutUiControls()
    {
        if (!m_hwnd || !m_uiPanel)
        {
            return;
        }

        RECT client = {};
        GetClientRect(m_hwnd, &client);
        const int panelX = (std::max)(static_cast<int>(client.right) - kUiPanelWidth, 0);
        const int panelHeight = (std::max)(static_cast<int>(client.bottom), 1);
        MoveWindow(m_uiPanel, panelX, 0, kUiPanelWidth, panelHeight, TRUE);

        // Record the real hair viewport, not merely the outer swap-chain size.
        // Formal runs are rejected later when this differs from the requested
        // fixed resolution, preventing the UI panel from silently shrinking
        // a nominal 1920 x 1080 measurement.
        const RECT observedViewport = GetRenderViewportRect();
        m_actualRenderWidth = static_cast<uint32_t>(
            (std::max<LONG>)(observedViewport.right - observedViewport.left, 0));
        m_actualRenderHeight = static_cast<uint32_t>(
            (std::max<LONG>)(observedViewport.bottom - observedViewport.top, 0));
        m_experimentCase.observedRenderWidth = m_actualRenderWidth;
        m_experimentCase.observedRenderHeight = m_actualRenderHeight;
        m_experimentCase.observedDisplayStrandCount = static_cast<uint32_t>(m_asset.strands.size());

        const int fullWidth = kUiPanelWidth - kUiMargin * 2;
        const int x = panelX + kUiMargin;
        const int leftRailWidth = 84;
        const int contentGap = 10;
        const int inspectorX = x + leftRailWidth + contentGap;
        const int inspectorWidth = fullWidth - leftRailWidth - contentGap;
        const int labelHeight = 18;
        const int controlHeight = 24;
        const int sliderHeight = 28;
        const int gap = 6;
        const int quadWidth = (fullWidth - 24) / 4;
        const int sixWidth = (fullWidth - 20) / 6;
        const int twoWidth = (inspectorWidth - 8) / 2;
        const int threeWidth = (inspectorWidth - 16) / 3;
        int y = kUiMargin;

        auto findStaticByText = [&](const wchar_t* textValue) -> HWND
        {
            for (HWND child = GetWindow(m_hwnd, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT))
            {
                if (child == m_uiPanel)
                {
                    continue;
                }
                wchar_t klass[32] = {};
                GetClassNameW(child, klass, 32);
                if (wcscmp(klass, L"Static") != 0)
                {
                    continue;
                }
                wchar_t buffer[128] = {};
                GetWindowTextW(child, buffer, static_cast<int>(std::size(buffer)));
                if (wcscmp(buffer, textValue) == 0)
                {
                    return child;
                }
            }
            return nullptr;
        };

        HWND labelPreset = findStaticByText(L"Preset");
        HWND labelRender = findStaticByText(L"Render");
        HWND labelColorDebug = findStaticByText(L"Color Debug");

        auto show = [&](HWND hwnd, bool visible)
        {
            if (hwnd)
            {
                ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
            }
        };
        auto place = [&](HWND hwnd, int px, int py, int w, int h, bool visible = true)
        {
            if (!hwnd)
            {
                return;
            }
            ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
            if (visible)
            {
                MoveWindow(hwnd, px, py, w, h, TRUE);
            }
        };
        auto advance = [&](int amount) -> int
        {
            const int old = y;
            y += amount;
            return old;
        };
        auto placeInspectorSlider = [&](HWND label, HWND slider, bool visible)
        {
            if (!visible)
            {
                show(label, false);
                show(slider, false);
                return;
            }
            place(label, inspectorX, y, inspectorWidth, labelHeight, true);
            y += labelHeight;
            place(slider, inspectorX, y, inspectorWidth, sliderHeight, true);
            y += sliderHeight;
        };
        auto placeInspectorSliderHalf = [&](HWND labelA, HWND sliderA, HWND labelB, HWND sliderB, bool visible)
        {
            if (!visible)
            {
                show(labelA, false); show(sliderA, false); show(labelB, false); show(sliderB, false);
                return;
            }
            place(labelA, inspectorX, y, twoWidth, labelHeight, true);
            place(labelB, inspectorX + twoWidth + 8, y, twoWidth, labelHeight, true);
            y += labelHeight;
            place(sliderA, inspectorX, y, twoWidth, sliderHeight, true);
            place(sliderB, inspectorX + twoWidth + 8, y, twoWidth, sliderHeight, true);
            y += sliderHeight;
        };

        std::vector<HWND> managed = {
            labelPreset, labelRender, labelColorDebug,
            m_comboPreset, m_btnPlayPause, m_btnReset, m_btnStep, m_btnApplyAsset, m_btnImportAsset,
            m_btnSaveSettings, m_btnLoadSettings, m_btnExportCsv, m_btnExportPerfCsv,
            m_btnBrowseEditStack, m_btnClearEditStack, m_labelEditStack, m_labelInlineEditStack, m_editInlineEditStack,
            m_btnApplyInlineEditStack, m_btnSaveInlineEditStack, m_btnResetInlineEditStack,
            m_labelQuickEditForm, m_labelLayerList, m_listInlineLayers, m_labelQuickEditName, m_editQuickEditName,
            m_btnQuickEditMoveUp, m_btnQuickEditMoveDown, m_checkQuickEditEnabled, m_btnQuickEditDelete, m_btnQuickEditDuplicate,
            m_comboQuickEditMode, m_labelQuickEditFeather, m_trackQuickEditFeather, m_labelQuickEditLengthScale, m_trackQuickEditLengthScale,
            m_labelQuickEditLayerWeight, m_trackQuickEditLayerWeight, m_labelQuickEditCenterU, m_trackQuickEditCenterU,
            m_labelQuickEditCenterV, m_trackQuickEditCenterV, m_labelQuickEditRadiusU, m_trackQuickEditRadiusU,
            m_labelQuickEditRadiusV, m_trackQuickEditRadiusV, m_labelQuickEditInnerRadius, m_trackQuickEditInnerRadius,
            m_labelQuickEditAngleCenter, m_trackQuickEditAngleCenter, m_labelQuickEditAngleHalfExtent, m_trackQuickEditAngleHalfExtent,
            m_labelQuickEditRootOffsetX, m_trackQuickEditRootOffsetX, m_labelQuickEditRootOffsetY, m_trackQuickEditRootOffsetY,
            m_labelQuickEditRootOffsetZ, m_trackQuickEditRootOffsetZ, m_labelQuickEditTipOffsetX, m_trackQuickEditTipOffsetX,
            m_labelQuickEditTipOffsetY, m_trackQuickEditTipOffsetY, m_labelQuickEditTipOffsetZ, m_trackQuickEditTipOffsetZ,
            m_labelQuickEditSideOffset, m_trackQuickEditSideOffset, m_labelQuickEditYaw, m_trackQuickEditYaw,
            m_labelQuickEditWidthScale, m_trackQuickEditWidthScale, m_labelQuickEditFollowerInfluence, m_trackQuickEditFollowerInfluence,
            m_labelQuickEditBoundarySmoothing, m_trackQuickEditBoundarySmoothing, m_labelQuickEditBoundaryRadiusUv, m_trackQuickEditBoundaryRadiusUv,
            m_labelQuickEditGroupId, m_editQuickEditGroupId, m_checkQuickEditGuidesOnly, m_labelQuickEditVisualization,
            m_btnQuickEditReplace, m_btnQuickEditAppend, m_btnBrowsePartingMask, m_btnBrowseRegionMask, m_labelMaskSource,
            m_comboMaskSource, m_labelMaskOverlay, m_comboMaskOverlay, m_labelPartingMask, m_previewPartingMask,
            m_labelRegionMask, m_previewRegionMask, m_labelScalpCoverageStrength, m_trackScalpCoverageStrength,
            m_labelScalpPartWidth, m_trackScalpPartWidth, m_labelScalpDarkness, m_trackScalpDarkness,
            m_labelScalpHairBlend, m_trackScalpHairBlend, m_editGuides, m_editFollowers, m_editParticles,
            m_comboRenderMode, m_comboInteractionMode, m_comboDebugColor,
            m_checkSelfCollision, m_checkGrid, m_checkScalpMotion, m_checkShowColliders, m_checkShowScalpMesh,
            m_checkShowScalp, m_checkShowGridBounds, m_checkShowFloor, m_checkShowGuides, m_checkShowFollowers,
            m_checkShowRoots, m_checkShowLinks, m_labelWidth, m_trackWidth, m_labelGravity, m_trackGravity,
            m_labelDamping, m_trackDamping, m_labelWind, m_trackWind, m_labelIterations, m_trackIterations,
            m_labelFollow, m_trackFollow, m_labelSelfRadius, m_trackSelfRadius, m_labelSelfStiffness, m_trackSelfStiffness,
            m_labelGridCellSize, m_trackGridCellSize, m_labelStretchCompliance, m_trackStretchCompliance, m_labelBendCompliance, m_trackBendCompliance,
            m_labelActiveSetTuning, m_checkActiveSetAdaptiveBudget, m_checkActiveSetHardClamp, m_labelActiveSetMaxClusters, m_trackActiveSetMaxClusters,
            m_labelActiveSetMaxGuides, m_trackActiveSetMaxGuides, m_labelActiveSetThreshold, m_trackActiveSetThreshold,
            m_labelActiveSetTargetFrame, m_trackActiveSetTargetFrame, m_labelActiveSetFixedCost, m_trackActiveSetFixedCost,
            m_labelActiveSetClusterCost, m_trackActiveSetClusterCost, m_labelActiveSetScoreWeights, m_labelActiveSetMotionWeight,
            m_trackActiveSetMotionWeight, m_labelActiveSetContactWeight, m_trackActiveSetContactWeight, m_labelActiveSetUncertaintyWeight,
            m_trackActiveSetUncertaintyWeight, m_labelActiveSetEditWeight, m_trackActiveSetEditWeight, m_btnExportActiveSetJson,
            m_btnExportRuntimeReport, m_labelAssetStats, m_labelStats, m_labelEditWorkflow, m_labelEditLegend,
            m_btnWorkspaceGroom, m_btnWorkspaceSim, m_btnWorkspaceReview, m_btnWorkspaceReport,
            m_btnToolSelect, m_btnToolComb, m_btnToolGrab, m_btnToolSmooth, m_btnToolLength, m_btnToolVolume,
            m_btnToolPart, m_btnToolFreeze, m_btnToolStrandFix, m_btnUndoEdit, m_btnRedoEdit,
            m_btnTrayLayers, m_btnTrayHistory, m_btnTrayMetrics, m_btnToggleInspector, m_btnToggleTray, m_btnToggleAdvanced, m_btnFastViewport,
            m_labelWorkspaceTitle, m_labelInspectorTitle, m_labelTrayTitle, m_labelToolHint, m_labelSessionStatus
        };
        for (HWND hwnd : managed)
        {
            show(hwnd, false);
        }

        SetWindowTextFormat(m_labelWorkspaceTitle, std::wstring(L"Workspace  •  ") + GetWorkspaceDisplayName());
        SetWindowTextFormat(m_labelInspectorTitle, IsGroomWorkspace() ? (std::wstring(L"Tool Options  •  ") + GetGroomToolDisplayName()) :
            (IsSimWorkspace() ? L"Tool Options  •  Simulation" : (IsReviewWorkspace() ? L"Tool Options  •  Review" : L"Tool Options  •  Report")));
        SetWindowTextFormat(m_labelTrayTitle, std::wstring(L"Panels  •  ") + GetBottomTrayDisplayName());

        const std::wstring toolHint = IsGroomWorkspace()
            ? (std::wstring(L"DCC-style workflow: choose a tool on the left, tweak only the top options, then edit in the viewport.\n"
               L"Active tool: ") + GetGroomToolDisplayName() + L"   LMB edit / Alt+LMB orbit / Wheel radius")
            : (IsSimWorkspace()
                ? L"Simulation toolbar. Keep Groom for shaping, then tune physics here."
                : (IsReviewWorkspace()
                    ? L"Review toolbar. Compare overlays and masks without opening the full advanced stack."
                    : L"Report toolbar. Export telemetry while keeping the viewport uncluttered."));
        SetWindowTextFormat(m_labelToolHint, toolHint);

        std::wstring sessionStatus = L"Toolbar: Import / Save / Export / Rebuild";
        sessionStatus += L"\nEdit stack: ";
        sessionStatus += (m_editStackPath.empty() ? L"inline session" : std::filesystem::path(m_editStackPath).filename().wstring());
        sessionStatus += L"   Overlay: ";
        sessionStatus += GetMaskOverlayModeDisplayName();
        SetWindowTextFormat(m_labelSessionStatus, sessionStatus);

        place(m_labelWorkspaceTitle, x, advance(labelHeight), fullWidth, labelHeight);
        const int workspaceY = advance(controlHeight);
        place(m_btnWorkspaceGroom, x, workspaceY, quadWidth, controlHeight);
        place(m_btnWorkspaceSim, x + quadWidth + 8, workspaceY, quadWidth, controlHeight);
        place(m_btnWorkspaceReview, x + (quadWidth + 8) * 2, workspaceY, quadWidth, controlHeight);
        place(m_btnWorkspaceReport, x + (quadWidth + 8) * 3, workspaceY, quadWidth, controlHeight);

        const int toolbarY1 = advance(controlHeight + gap);
        place(m_btnImportAsset, x, toolbarY1, quadWidth, controlHeight);
        place(m_btnSaveSettings, x + quadWidth + 8, toolbarY1, quadWidth, controlHeight);
        place(m_btnSaveInlineEditStack, x + (quadWidth + 8) * 2, toolbarY1, quadWidth, controlHeight);
        place(m_btnApplyAsset, x + (quadWidth + 8) * 3, toolbarY1, quadWidth, controlHeight);

        const int toolbarY2 = advance(controlHeight + gap);
        place(m_btnUndoEdit, x, toolbarY2, sixWidth, controlHeight);
        place(m_btnRedoEdit, x + (sixWidth + 4), toolbarY2, sixWidth, controlHeight);
        place(m_btnToggleInspector, x + (sixWidth + 4) * 2, toolbarY2, sixWidth, controlHeight);
        place(m_btnToggleAdvanced, x + (sixWidth + 4) * 3, toolbarY2, sixWidth, controlHeight);
        place(m_btnToggleTray, x + (sixWidth + 4) * 4, toolbarY2, sixWidth, controlHeight);
        place(m_btnFastViewport, x + (sixWidth + 4) * 5, toolbarY2, sixWidth, controlHeight);

        const int toolbarY3 = advance(controlHeight + gap);
        place(m_btnPlayPause, x, toolbarY3, quadWidth, controlHeight);
        place(m_btnStep, x + quadWidth + 8, toolbarY3, quadWidth, controlHeight);
        place(m_btnBrowseEditStack, x + (quadWidth + 8) * 2, toolbarY3, quadWidth, controlHeight);
        place(m_btnClearEditStack, x + (quadWidth + 8) * 3, toolbarY3, quadWidth, controlHeight);

        place(m_labelSessionStatus, x, advance(40), fullWidth, 36);

        const int bodyTop = y + gap;
        const int trayReservedHeight = m_showBottomTray ? 164 : 0;
        const int bodyBottom = panelHeight - trayReservedHeight - kUiMargin;
        const int bodyHeight = std::max(bodyBottom - bodyTop, 220);

        int leftY = bodyTop;
        if (IsGroomWorkspace())
        {
            place(m_btnToolSelect, x, leftY, leftRailWidth, controlHeight); leftY += controlHeight + 4;
            place(m_btnToolComb, x, leftY, leftRailWidth, controlHeight); leftY += controlHeight + 4;
            place(m_btnToolGrab, x, leftY, leftRailWidth, controlHeight); leftY += controlHeight + 4;
            place(m_btnToolSmooth, x, leftY, leftRailWidth, controlHeight); leftY += controlHeight + 4;
            place(m_btnToolLength, x, leftY, leftRailWidth, controlHeight); leftY += controlHeight + 4;
            place(m_btnToolVolume, x, leftY, leftRailWidth, controlHeight); leftY += controlHeight + 4;
            place(m_btnToolPart, x, leftY, leftRailWidth, controlHeight); leftY += controlHeight + 4;
            place(m_btnToolFreeze, x, leftY, leftRailWidth, controlHeight); leftY += controlHeight + 4;
            place(m_btnToolStrandFix, x, leftY, leftRailWidth, controlHeight);
        }
        else
        {
            place(m_btnToolSelect, x, leftY, leftRailWidth, controlHeight);
        }

        y = bodyTop;
        if (m_showInspectorPane)
        {
            place(m_labelInspectorTitle, inspectorX, y, inspectorWidth, labelHeight);
            y += labelHeight + 2;
            place(m_labelToolHint, inspectorX, y, inspectorWidth, 42);
            y += 46;

            if (IsGroomWorkspace())
            {
                const bool showArea = m_groomTool == GroomTool::Comb || m_groomTool == GroomTool::Grab || m_groomTool == GroomTool::Length || m_groomTool == GroomTool::Volume || m_groomTool == GroomTool::Part || m_groomTool == GroomTool::Freeze;
                const bool showSector = (m_groomTool == GroomTool::Part) || (m_quickEditMode == hairgpu::RegionSelectMode::Sector);
                const bool showStrandOffsets = m_groomTool == GroomTool::Grab || m_groomTool == GroomTool::StrandFix;
                const bool showCombOffsets = m_groomTool == GroomTool::Comb;
                const bool showLength = m_groomTool == GroomTool::Length || m_groomTool == GroomTool::Comb;
                const bool showVolume = m_groomTool == GroomTool::Volume;
                const bool showSmooth = m_groomTool == GroomTool::Smooth;
                const bool showGrouping = m_groomTool == GroomTool::Freeze || m_groomTool == GroomTool::Select;

                place(m_labelQuickEditName, inspectorX, y, inspectorWidth, labelHeight); y += labelHeight;
                place(m_editQuickEditName, inspectorX, y, inspectorWidth, controlHeight); y += controlHeight + 4;
                place(m_checkQuickEditEnabled, inspectorX, y, twoWidth, controlHeight);
                place(m_checkQuickEditGuidesOnly, inspectorX + twoWidth + 8, y, twoWidth, controlHeight);
                y += controlHeight + 4;
                place(m_comboQuickEditMode, inspectorX, y, inspectorWidth, 120); y += controlHeight + gap;

                placeInspectorSliderHalf(m_labelQuickEditRadiusU, m_trackQuickEditRadiusU, m_labelQuickEditRadiusV, m_trackQuickEditRadiusV, showArea || showStrandOffsets || showSmooth);
                placeInspectorSlider(m_labelQuickEditLayerWeight, m_trackQuickEditLayerWeight, true);
                placeInspectorSlider(m_labelQuickEditFeather, m_trackQuickEditFeather, showArea || showSmooth);
                placeInspectorSlider(m_labelQuickEditLengthScale, m_trackQuickEditLengthScale, showLength);
                placeInspectorSlider(m_labelQuickEditWidthScale, m_trackQuickEditWidthScale, showVolume);
                placeInspectorSlider(m_labelQuickEditBoundarySmoothing, m_trackQuickEditBoundarySmoothing, showSmooth || showCombOffsets || showVolume);
                placeInspectorSlider(m_labelQuickEditBoundaryRadiusUv, m_trackQuickEditBoundaryRadiusUv, showSmooth);
                placeInspectorSlider(m_labelQuickEditSideOffset, m_trackQuickEditSideOffset, showCombOffsets || showVolume);
                placeInspectorSlider(m_labelQuickEditYaw, m_trackQuickEditYaw, showCombOffsets || m_groomTool == GroomTool::Part);
                placeInspectorSlider(m_labelQuickEditFollowerInfluence, m_trackQuickEditFollowerInfluence, showVolume || showCombOffsets);
                placeInspectorSlider(m_labelQuickEditTipOffsetX, m_trackQuickEditTipOffsetX, showCombOffsets || showStrandOffsets);
                placeInspectorSlider(m_labelQuickEditTipOffsetY, m_trackQuickEditTipOffsetY, showStrandOffsets);
                placeInspectorSlider(m_labelQuickEditTipOffsetZ, m_trackQuickEditTipOffsetZ, showCombOffsets || showStrandOffsets);

                const int actionY = std::min(y + 4, bodyBottom - (m_showAdvancedInspector ? 170 : 76));
                place(m_btnQuickEditReplace, inspectorX, actionY, twoWidth, controlHeight);
                place(m_btnQuickEditAppend, inspectorX + twoWidth + 8, actionY, twoWidth, controlHeight);

                if (m_showAdvancedInspector)
                {
                    y = actionY + controlHeight + 8;
                    place(m_labelQuickEditForm, inspectorX, y, inspectorWidth, labelHeight); y += labelHeight;
                    placeInspectorSliderHalf(m_labelQuickEditCenterU, m_trackQuickEditCenterU, m_labelQuickEditCenterV, m_trackQuickEditCenterV, showArea);
                    placeInspectorSlider(m_labelQuickEditInnerRadius, m_trackQuickEditInnerRadius, showArea);
                    placeInspectorSlider(m_labelQuickEditAngleCenter, m_trackQuickEditAngleCenter, showSector);
                    placeInspectorSlider(m_labelQuickEditAngleHalfExtent, m_trackQuickEditAngleHalfExtent, showSector);
                    placeInspectorSlider(m_labelQuickEditRootOffsetX, m_trackQuickEditRootOffsetX, showStrandOffsets);
                    placeInspectorSlider(m_labelQuickEditRootOffsetY, m_trackQuickEditRootOffsetY, showStrandOffsets);
                    placeInspectorSlider(m_labelQuickEditRootOffsetZ, m_trackQuickEditRootOffsetZ, showStrandOffsets);
                    if (showGrouping)
                    {
                        place(m_labelQuickEditGroupId, inspectorX, y, inspectorWidth, labelHeight); y += labelHeight;
                        place(m_editQuickEditGroupId, inspectorX, y, inspectorWidth, controlHeight); y += controlHeight + 4;
                    }
                    place(m_labelQuickEditVisualization, inspectorX, y, inspectorWidth, 56); y += 60;
                    place(m_btnQuickEditDuplicate, inspectorX, y, twoWidth, controlHeight);
                    place(m_btnQuickEditDelete, inspectorX + twoWidth + 8, y, twoWidth, controlHeight); y += controlHeight + 4;
                    place(m_btnQuickEditMoveUp, inspectorX, y, twoWidth, controlHeight);
                    place(m_btnQuickEditMoveDown, inspectorX + twoWidth + 8, y, twoWidth, controlHeight);
                }
            }
            else if (IsSimWorkspace())
            {
                place(labelRender, inspectorX, y, inspectorWidth, labelHeight); y += labelHeight;
                place(m_comboRenderMode, inspectorX, y, inspectorWidth, 120); y += controlHeight + gap;
                place(labelColorDebug, inspectorX, y, inspectorWidth, labelHeight); y += labelHeight;
                place(m_comboDebugColor, inspectorX, y, inspectorWidth, 140); y += controlHeight + gap;
                place(m_checkSelfCollision, inspectorX, y, twoWidth, controlHeight);
                place(m_checkShowColliders, inspectorX + twoWidth + 8, y, twoWidth, controlHeight); y += controlHeight + 4;
                placeInspectorSlider(m_labelWidth, m_trackWidth, true);
                placeInspectorSlider(m_labelGravity, m_trackGravity, true);
                placeInspectorSlider(m_labelDamping, m_trackDamping, true);
                placeInspectorSlider(m_labelWind, m_trackWind, true);
                placeInspectorSlider(m_labelIterations, m_trackIterations, true);
                if (m_showAdvancedInspector)
                {
                    place(m_checkShowScalpMesh, inspectorX, y, twoWidth, controlHeight);
                    place(m_checkShowScalp, inspectorX + twoWidth + 8, y, twoWidth, controlHeight); y += controlHeight + 4;
                    place(m_checkShowGuides, inspectorX, y, twoWidth, controlHeight);
                    place(m_checkShowFollowers, inspectorX + twoWidth + 8, y, twoWidth, controlHeight); y += controlHeight + 4;
                    placeInspectorSlider(m_labelFollow, m_trackFollow, true);
                    placeInspectorSlider(m_labelSelfRadius, m_trackSelfRadius, true);
                    placeInspectorSlider(m_labelSelfStiffness, m_trackSelfStiffness, true);
                    placeInspectorSlider(m_labelGridCellSize, m_trackGridCellSize, true);
                    placeInspectorSlider(m_labelStretchCompliance, m_trackStretchCompliance, true);
                    placeInspectorSlider(m_labelBendCompliance, m_trackBendCompliance, true);
                }
            }
            else if (IsReviewWorkspace())
            {
                place(m_labelMaskOverlay, inspectorX, y, inspectorWidth, labelHeight); y += labelHeight;
                place(m_comboMaskOverlay, inspectorX, y, inspectorWidth, 160); y += controlHeight + gap;
                place(m_labelMaskSource, inspectorX, y, inspectorWidth, labelHeight); y += labelHeight;
                place(m_comboMaskSource, inspectorX, y, inspectorWidth, 120); y += controlHeight + gap;
                place(m_btnBrowsePartingMask, inspectorX, y, twoWidth, controlHeight);
                place(m_btnBrowseRegionMask, inspectorX + twoWidth + 8, y, twoWidth, controlHeight); y += controlHeight + 4;
                place(m_labelPartingMask, inspectorX, y, twoWidth, labelHeight);
                place(m_labelRegionMask, inspectorX + twoWidth + 8, y, twoWidth, labelHeight); y += labelHeight;
                place(m_previewPartingMask, inspectorX, y, twoWidth, 72);
                place(m_previewRegionMask, inspectorX + twoWidth + 8, y, twoWidth, 72); y += 78;
                if (m_showAdvancedInspector)
                {
                    placeInspectorSlider(m_labelScalpCoverageStrength, m_trackScalpCoverageStrength, true);
                    placeInspectorSlider(m_labelScalpPartWidth, m_trackScalpPartWidth, true);
                    placeInspectorSlider(m_labelScalpDarkness, m_trackScalpDarkness, true);
                    placeInspectorSlider(m_labelScalpHairBlend, m_trackScalpHairBlend, true);
                    place(m_labelAssetStats, inspectorX, std::min(y + 4, bodyBottom - 88), inspectorWidth, 84);
                }
            }
            else
            {
                place(m_btnExportActiveSetJson, inspectorX, y, inspectorWidth, controlHeight); y += controlHeight + 4;
                place(m_btnExportRuntimeReport, inspectorX, y, inspectorWidth, controlHeight); y += controlHeight + 8;
                if (m_showAdvancedInspector)
                {
                    place(m_labelActiveSetTuning, inspectorX, y, inspectorWidth, labelHeight); y += labelHeight + 4;
                    place(m_checkActiveSetAdaptiveBudget, inspectorX, y, twoWidth, controlHeight);
                    place(m_checkActiveSetHardClamp, inspectorX + twoWidth + 8, y, twoWidth, controlHeight); y += controlHeight + 4;
                    placeInspectorSlider(m_labelActiveSetMaxClusters, m_trackActiveSetMaxClusters, true);
                    placeInspectorSlider(m_labelActiveSetMaxGuides, m_trackActiveSetMaxGuides, true);
                    placeInspectorSlider(m_labelActiveSetThreshold, m_trackActiveSetThreshold, true);
                    placeInspectorSlider(m_labelActiveSetTargetFrame, m_trackActiveSetTargetFrame, true);
                    placeInspectorSlider(m_labelActiveSetFixedCost, m_trackActiveSetFixedCost, true);
                    placeInspectorSlider(m_labelActiveSetClusterCost, m_trackActiveSetClusterCost, true);
                    place(m_labelActiveSetScoreWeights, inspectorX, y, inspectorWidth, labelHeight); y += labelHeight;
                    placeInspectorSlider(m_labelActiveSetMotionWeight, m_trackActiveSetMotionWeight, true);
                    placeInspectorSlider(m_labelActiveSetContactWeight, m_trackActiveSetContactWeight, true);
                    placeInspectorSlider(m_labelActiveSetUncertaintyWeight, m_trackActiveSetUncertaintyWeight, true);
                    placeInspectorSlider(m_labelActiveSetEditWeight, m_trackActiveSetEditWeight, true);
                }
            }
        }

        if (m_showBottomTray)
        {
            const int trayY = panelHeight - 152;
            place(m_labelTrayTitle, x, trayY, fullWidth, labelHeight);
            place(m_btnTrayLayers, x, trayY + labelHeight + 4, quadWidth, controlHeight);
            place(m_btnTrayHistory, x + quadWidth + 8, trayY + labelHeight + 4, quadWidth, controlHeight);
            place(m_btnTrayMetrics, x + (quadWidth + 8) * 2, trayY + labelHeight + 4, quadWidth, controlHeight);
            const int trayBodyY = trayY + labelHeight + controlHeight + 10;
            const int trayBodyHeight = panelHeight - trayBodyY - kUiMargin;
            if (m_bottomTrayTab == BottomTrayTab::Layers)
            {
                place(m_labelLayerList, x, trayBodyY, fullWidth, labelHeight);
                place(m_listInlineLayers, x, trayBodyY + labelHeight + 2, fullWidth, trayBodyHeight - labelHeight - 2);
            }
            else if (m_bottomTrayTab == BottomTrayTab::History)
            {
                place(m_labelEditLegend, x, trayBodyY, fullWidth, trayBodyHeight);
            }
            else
            {
                place(m_labelStats, x, trayBodyY, fullWidth, trayBodyHeight);
            }
        }
    }


    void GpuHairViewer::RefreshAssetStatsText()
    {
        const std::filesystem::path inlineTempPath = GetInlineEditStackTempPath();
        const std::wstring editStackDisplayName = m_editStackPath.empty() ? std::wstring(L"none") : ((std::filesystem::path(m_editStackPath) == inlineTempPath) ? std::wstring(L"[inline]") : std::filesystem::path(m_editStackPath).filename().wstring());

        std::wstringstream ss;
        ss << L"Asset\n"
           << L"Source: " << m_lastAssetSource << L"\n"
           << L"Guides: " << m_asset.guideStrandCount
           << L"  Followers: " << m_asset.followerStrandCount << L"\n"
           << L"Total strands: " << m_asset.strands.size() << L"\n"
           << L"Particles/strand: " << m_asset.particlesPerStrand
           << L"  Local segments/strand: " << (m_asset.particlesPerStrand > 0u ? (m_asset.particlesPerStrand - 1u) : 0u) << L"\n"
           << L"Segment length: " << std::fixed << std::setprecision(4) << m_simConfig.segmentLength << L" m\n"
           << L"Total particles: " << m_asset.restPoints.size() << L"\n"
           << L"Segments: " << m_renderData.segmentCount << L"\n"
           << L"Import: " << m_lastImportStatus << L"\n"
           << L"Edit stack: " << editStackDisplayName << L"\n"
           << L"Edit status: " << m_lastEditStackStatus << L"\n"
           << L"Active edit guides: " << m_runtimeEditState.activeGuideCount << L" / " << m_asset.guideStrandCount << L"\n"
           << L"Runtime: particle-only GPU path\nEdit boxes auto-rebuild";

        if (m_labelEditStack)
        {
            SetWindowTextFormat(m_labelEditStack, L"Edit Stack: " + editStackDisplayName + L"  [" + m_lastEditStackStatus + L"]");
        }
        if (m_labelAssetStats)
        {
            SetWindowTextFormat(m_labelAssetStats, ss.str());
        }
    }

    void GpuHairViewer::SyncControlsFromState()
    {
        m_syncingControls = true;

        SetDlgItemInt(m_hwnd, IDC_EDIT_GUIDES, m_builderGuideCount, FALSE);
        SetDlgItemInt(m_hwnd, IDC_EDIT_FOLLOWERS, m_builderFollowersPerGuide, FALSE);
        SetDlgItemInt(m_hwnd, IDC_EDIT_PARTICLES, m_builderParticlesPerStrand, FALSE);

        SendMessageW(m_comboPreset, CB_SETCURSEL,
            (m_activePresetName == L"Calm Studio") ? 1 :
            (m_activePresetName == L"Windy Demo") ? 2 :
            (m_activePresetName == L"Heavy Grounded") ? 3 :
            (m_activePresetName == L"Game-ish LOD") ? 4 : 0, 0);
        SendMessageW(m_comboRenderMode, CB_SETCURSEL,
            (m_renderMode == RenderMode::ParticlePointSprite) ? 0 :
            (m_renderMode == RenderMode::ParticleBillboard) ? 1 : 2, 0);
        SendMessageW(m_comboInteractionMode, CB_SETCURSEL, static_cast<WPARAM>(m_interactionMode == InteractionMode::Edit ? 1 : 0), 0);
        SendMessageW(m_comboDebugColor, CB_SETCURSEL, static_cast<WPARAM>(m_debugColorMode), 0);
        SendMessageW(m_comboMaskSource, CB_SETCURSEL, static_cast<WPARAM>(m_maskSourceMode), 0);
        SendMessageW(m_comboMaskOverlay, CB_SETCURSEL, static_cast<WPARAM>(m_maskOverlayMode), 0);

        SendMessageW(m_checkSelfCollision, BM_SETCHECK, m_enableSelfCollision ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkGrid, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(m_checkScalpMotion, BM_SETCHECK, m_enableScalpMotion ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowColliders, BM_SETCHECK, m_showColliderDebug ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowScalpMesh, BM_SETCHECK, m_showScalpMesh ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowScalp, BM_SETCHECK, m_showScalpWireframe ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowGridBounds, BM_SETCHECK, m_showGridBounds ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowFloor, BM_SETCHECK, m_showFloorGuide ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowGuides, BM_SETCHECK, m_showGuideSkeleton ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowFollowers, BM_SETCHECK, m_showFollowerSkeleton ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowRoots, BM_SETCHECK, m_showRootMarkers ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowLinks, BM_SETCHECK, m_showFollowLinks ? BST_CHECKED : BST_UNCHECKED, 0);

        SendMessageW(m_trackWidth, TBM_SETPOS, TRUE, SliderFromFloat(m_widthScale, 0.2f, 8.0f));
        SendMessageW(m_trackGravity, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.gravity, 0.0f, 30.0f));
        SendMessageW(m_trackDamping, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.globalDamping, 0.0f, 0.15f));
        SendMessageW(m_trackWind, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.windStrength, 0.0f, 4.0f));
        SendMessageW(m_trackIterations, TBM_SETPOS, TRUE, SliderFromFloat(static_cast<float>(m_simConfig.solverIterations), 1.0f, 16.0f));
        SendMessageW(m_trackFollow, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.followStiffness, 0.0f, 1.0f));
        SendMessageW(m_trackSelfRadius, TBM_SETPOS, TRUE, SliderFromFloat(m_selfCollisionRadius, 0.004f, 0.040f));
        SendMessageW(m_trackSelfStiffness, TBM_SETPOS, TRUE, SliderFromFloat(m_selfCollisionStiffness, 0.0f, 1.0f));
        SendMessageW(m_trackGridCellSize, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.selfCollisionCellSize, 0.03f, 0.20f));
        const float stretchT = (std::log10(std::max(m_simConfig.stretchCompliance, 1.0e-9f)) + 9.0f) / 5.0f;
        const float bendT = (std::log10(std::max(m_simConfig.bendCompliance, 1.0e-8f)) + 8.0f) / 5.0f;
        SendMessageW(m_trackStretchCompliance, TBM_SETPOS, TRUE, SliderFromFloat(stretchT, 0.0f, 1.0f));
        SendMessageW(m_trackBendCompliance, TBM_SETPOS, TRUE, SliderFromFloat(bendT, 0.0f, 1.0f));
        SendMessageW(m_trackScalpCoverageStrength, TBM_SETPOS, TRUE, SliderFromFloat(m_scalpCoverageStrength, 0.0f, 2.0f));
        SendMessageW(m_trackScalpPartWidth, TBM_SETPOS, TRUE, SliderFromFloat(m_scalpPartLineWidth, 0.008f, 0.120f));
        SendMessageW(m_trackScalpDarkness, TBM_SETPOS, TRUE, SliderFromFloat(m_scalpTintDarkness, 0.0f, 1.0f));
        SendMessageW(m_trackScalpHairBlend, TBM_SETPOS, TRUE, SliderFromFloat(m_scalpHairBlend, 0.0f, 1.0f));
        SendMessageW(m_checkActiveSetAdaptiveBudget, BM_SETCHECK, m_activeSetBudget.enableBudgetAdaptation ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkActiveSetHardClamp, BM_SETCHECK, m_activeSetBudget.enableHardBudgetClamp ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_trackActiveSetMaxClusters, TBM_SETPOS, TRUE, SliderFromFloat(static_cast<float>(m_activeSetBudget.maxActiveClusters), 1.0f, 64.0f));
        SendMessageW(m_trackActiveSetMaxGuides, TBM_SETPOS, TRUE, SliderFromFloat(static_cast<float>(m_activeSetBudget.maxActiveGuides), 8.0f, 2048.0f));
        SendMessageW(m_trackActiveSetThreshold, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetBudget.promoteScoreThreshold, 0.0f, 1.0f));
        SendMessageW(m_trackActiveSetTargetFrame, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetBudget.targetFrameMilliseconds, 4.0f, 16.67f));
        SendMessageW(m_trackActiveSetFixedCost, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetBudget.estimatedFixedCostMilliseconds, 0.0f, 16.0f));
        SendMessageW(m_trackActiveSetClusterCost, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetBudget.estimatedClusterCostMilliseconds, 0.05f, 4.0f));
        SendMessageW(m_trackActiveSetMotionWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetScoringSettings.motionWeight, 0.0f, 1.0f));
        SendMessageW(m_trackActiveSetContactWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetScoringSettings.contactWeight, 0.0f, 1.0f));
        SendMessageW(m_trackActiveSetUncertaintyWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetScoringSettings.uncertaintyWeight, 0.0f, 1.0f));
        SendMessageW(m_trackActiveSetEditWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetScoringSettings.editWeight, 0.0f, 1.0f));
        SendMessageW(m_comboQuickEditMode, CB_SETCURSEL,
            (m_quickEditMode == hairgpu::RegionSelectMode::Rectangle) ? 1 :
            (m_quickEditMode == hairgpu::RegionSelectMode::Sector) ? 2 : 0, 0);
        SendMessageW(m_checkQuickEditEnabled, BM_SETCHECK, m_quickEditEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextFormat(m_editQuickEditName, m_quickEditLayerName);
        SendMessageW(m_trackQuickEditFeather, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditFeather, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditLengthScale, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditLengthScale, 0.6f, 1.4f));
        SendMessageW(m_trackQuickEditBoundarySmoothing, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundarySmoothing, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditBoundaryRadiusUv, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f));

        SetWindowTextFormat(m_labelWidth, L"Width Scale: " + FormatFloat(m_widthScale, 2));
        SetWindowTextFormat(m_labelGravity, L"Gravity: " + FormatFloat(m_simConfig.gravity, 2));
        SetWindowTextFormat(m_labelDamping, L"Damping: " + FormatFloat(m_simConfig.globalDamping, 3));
        SetWindowTextFormat(m_labelWind, L"Wind Strength: " + FormatFloat(m_simConfig.windStrength, 2));
        SetWindowTextFormat(m_labelIterations, L"Solver Iterations: " + std::to_wstring(m_simConfig.solverIterations));
        SetWindowTextFormat(m_labelFollow, L"Follow Stiffness: " + FormatFloat(m_simConfig.followStiffness, 3));
        SetWindowTextFormat(m_labelSelfRadius, L"Self Radius: " + FormatFloat(m_selfCollisionRadius, 4));
        SetWindowTextFormat(m_labelSelfStiffness, L"Self Stiffness: " + FormatFloat(m_selfCollisionStiffness, 3));
        SetWindowTextFormat(m_labelGridCellSize, L"Grid Cell Size: " + FormatFloat(m_simConfig.selfCollisionCellSize, 3));
        SetWindowTextFormat(m_labelStretchCompliance, L"Stretch Compliance: " + FormatScientific(m_simConfig.stretchCompliance));
        SetWindowTextFormat(m_labelBendCompliance, L"Bend Compliance: " + FormatScientific(m_simConfig.bendCompliance));
        SetWindowTextFormat(m_labelScalpCoverageStrength, L"Scalp Coverage Strength: " + FormatFloat(m_scalpCoverageStrength, 2));
        SetWindowTextFormat(m_labelScalpPartWidth, L"Part Line Width: " + FormatFloat(m_scalpPartLineWidth, 3));
        SetWindowTextFormat(m_labelScalpDarkness, L"Scalp Darkness: " + FormatFloat(m_scalpTintDarkness, 2));
        SetWindowTextFormat(m_labelScalpHairBlend, L"Hair Tint Blend: " + FormatFloat(m_scalpHairBlend, 2));
        RefreshActiveSetTuningLabels();
        RefreshInlineLayerListFromText(m_selectedInlineLayerIndex);
        RefreshQuickEditFormLabels();
        SetWindowTextFormat(m_btnPlayPause, m_paused ? L"Play" : L"Pause");
        SetWindowTextFormat(m_btnSaveSettings, L"Save Session");
        SetWindowTextFormat(m_btnSaveInlineEditStack, L"Export Stack");
        SetWindowTextFormat(m_btnWorkspaceGroom, m_workspaceMode == WorkspaceMode::Groom ? L"[Groom]" : L"Groom");
        SetWindowTextFormat(m_btnWorkspaceSim, m_workspaceMode == WorkspaceMode::Sim ? L"[Sim]" : L"Sim");
        SetWindowTextFormat(m_btnWorkspaceReview, m_workspaceMode == WorkspaceMode::Review ? L"[Review]" : L"Review");
        SetWindowTextFormat(m_btnWorkspaceReport, m_workspaceMode == WorkspaceMode::Report ? L"[Report]" : L"Report");
        SetWindowTextFormat(m_btnToolSelect, m_groomTool == GroomTool::Select ? L"> Select" : L"Q  Select");
        SetWindowTextFormat(m_btnToolComb, m_groomTool == GroomTool::Comb ? L"> Comb" : L"W  Comb");
        SetWindowTextFormat(m_btnToolGrab, m_groomTool == GroomTool::Grab ? L"> Grab" : L"E  Grab");
        SetWindowTextFormat(m_btnToolSmooth, m_groomTool == GroomTool::Smooth ? L"> Smooth" : L"A  Smooth");
        SetWindowTextFormat(m_btnToolLength, m_groomTool == GroomTool::Length ? L"> Length" : L"S  Length");
        SetWindowTextFormat(m_btnToolVolume, m_groomTool == GroomTool::Volume ? L"> Volume" : L"D  Volume");
        SetWindowTextFormat(m_btnToolPart, m_groomTool == GroomTool::Part ? L"> Part" : L"P  Part");
        SetWindowTextFormat(m_btnToolFreeze, m_groomTool == GroomTool::Freeze ? L"> Freeze" : L"X  Freeze");
        SetWindowTextFormat(m_btnToolStrandFix, m_groomTool == GroomTool::StrandFix ? L"> Fix" : L"V  Fix");
        SetWindowTextFormat(m_btnTrayLayers, m_bottomTrayTab == BottomTrayTab::Layers ? L"[Layers]" : L"Layers");
        SetWindowTextFormat(m_btnTrayHistory, m_bottomTrayTab == BottomTrayTab::History ? L"[History]" : L"History");
        SetWindowTextFormat(m_btnTrayMetrics, m_bottomTrayTab == BottomTrayTab::Metrics ? L"[Metrics]" : L"Metrics");
        SetWindowTextFormat(m_btnToggleInspector, m_showInspectorPane ? L"Inspector On" : L"Inspector Off");
        SetWindowTextFormat(m_btnToggleAdvanced, m_showAdvancedInspector ? L"Advanced On" : L"Advanced Off");
        SetWindowTextFormat(m_btnToggleTray, m_showBottomTray ? L"Panels On" : L"Panels Off");
        SetWindowTextFormat(m_btnFastViewport, m_fastGroomViewport ? L"Fast View On" : L"Fast View Off");
        RefreshAssetStatsText();

        m_gpuDebugLinesDirty = true;
        m_syncingControls = false;
    }

    void GpuHairViewer::SyncStateFromControls()
    {
        if (m_syncingControls)
        {
            return;
        }
        m_enableSelfCollision = (SendMessageW(m_checkSelfCollision, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_enableGridSelfCollision = true;
        m_enableScalpMotion = false;
        m_showColliderDebug = (SendMessageW(m_checkShowColliders, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showScalpMesh = (SendMessageW(m_checkShowScalpMesh, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_maskOverlayMode = static_cast<MaskOverlayMode>(SendMessageW(m_comboMaskOverlay, CB_GETCURSEL, 0, 0));
        m_showScalpWireframe = (SendMessageW(m_checkShowScalp, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showGridBounds = (SendMessageW(m_checkShowGridBounds, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showFloorGuide = (SendMessageW(m_checkShowFloor, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showGuideSkeleton = (SendMessageW(m_checkShowGuides, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showFollowerSkeleton = (SendMessageW(m_checkShowFollowers, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showRootMarkers = (SendMessageW(m_checkShowRoots, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showFollowLinks = (SendMessageW(m_checkShowLinks, BM_GETCHECK, 0, 0) == BST_CHECKED);
        {
            const LRESULT renderSel = SendMessageW(m_comboRenderMode, CB_GETCURSEL, 0, 0);
            m_renderMode = (renderSel <= 0) ? RenderMode::ParticlePointSprite : ((renderSel == 1) ? RenderMode::ParticleBillboard : RenderMode::StrandRibbon);
        }
        m_interactionMode = IsGroomWorkspace() ? InteractionMode::Edit :
            ((SendMessageW(m_comboInteractionMode, CB_GETCURSEL, 0, 0) == 1) ? InteractionMode::Edit : InteractionMode::View);
        m_debugColorMode = static_cast<DebugColorMode>(std::max<LRESULT>(0, SendMessageW(m_comboDebugColor, CB_GETCURSEL, 0, 0)));
        if (m_interactionMode == InteractionMode::Edit && m_debugColorMode == DebugColorMode::Natural)
        {
            m_debugColorMode = DebugColorMode::Editability;
        }

        m_widthScale = FloatFromSlider(static_cast<int>(SendMessageW(m_trackWidth, TBM_GETPOS, 0, 0)), 0.2f, 8.0f);
        m_simConfig.gravity = FloatFromSlider(static_cast<int>(SendMessageW(m_trackGravity, TBM_GETPOS, 0, 0)), 0.0f, 30.0f);
        m_simConfig.globalDamping = FloatFromSlider(static_cast<int>(SendMessageW(m_trackDamping, TBM_GETPOS, 0, 0)), 0.0f, 0.15f);
        m_simConfig.windStrength = FloatFromSlider(static_cast<int>(SendMessageW(m_trackWind, TBM_GETPOS, 0, 0)), 0.0f, 4.0f);
        m_simConfig.solverIterations = static_cast<uint32_t>(std::round(FloatFromSlider(static_cast<int>(SendMessageW(m_trackIterations, TBM_GETPOS, 0, 0)), 1.0f, 16.0f)));
        m_simConfig.followStiffness = FloatFromSlider(static_cast<int>(SendMessageW(m_trackFollow, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_selfCollisionRadius = FloatFromSlider(static_cast<int>(SendMessageW(m_trackSelfRadius, TBM_GETPOS, 0, 0)), 0.004f, 0.040f);
        m_selfCollisionStiffness = FloatFromSlider(static_cast<int>(SendMessageW(m_trackSelfStiffness, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_simConfig.selfCollisionCellSize = FloatFromSlider(static_cast<int>(SendMessageW(m_trackGridCellSize, TBM_GETPOS, 0, 0)), 0.03f, 0.20f);
        const float stretchT = FloatFromSlider(static_cast<int>(SendMessageW(m_trackStretchCompliance, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        const float bendT = FloatFromSlider(static_cast<int>(SendMessageW(m_trackBendCompliance, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_simConfig.stretchCompliance = std::pow(10.0f, -9.0f + stretchT * 5.0f);
        m_simConfig.bendCompliance = std::pow(10.0f, -8.0f + bendT * 5.0f);
        m_scalpCoverageStrength = FloatFromSlider(static_cast<int>(SendMessageW(m_trackScalpCoverageStrength, TBM_GETPOS, 0, 0)), 0.0f, 2.0f);
        m_scalpPartLineWidth = FloatFromSlider(static_cast<int>(SendMessageW(m_trackScalpPartWidth, TBM_GETPOS, 0, 0)), 0.008f, 0.120f);
        m_scalpTintDarkness = FloatFromSlider(static_cast<int>(SendMessageW(m_trackScalpDarkness, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_scalpHairBlend = FloatFromSlider(static_cast<int>(SendMessageW(m_trackScalpHairBlend, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        {
            const LRESULT quickModeSel = SendMessageW(m_comboQuickEditMode, CB_GETCURSEL, 0, 0);
            m_quickEditMode = (quickModeSel == 1) ? hairgpu::RegionSelectMode::Rectangle : ((quickModeSel == 2) ? hairgpu::RegionSelectMode::Sector : hairgpu::RegionSelectMode::Ellipse);
        }
        m_quickEditEnabled = (SendMessageW(m_checkQuickEditEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_quickEditLayerName = GetWindowTextString(m_editQuickEditName);
        m_quickEditFeather = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditFeather, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_quickEditLengthScale = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditLengthScale, TBM_GETPOS, 0, 0)), 0.6f, 1.4f);
        m_quickEditLayerWeight = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditLayerWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.5f);
        m_quickEditCenterU = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditCenterU, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_quickEditCenterV = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditCenterV, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_quickEditRadiusU = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRadiusU, TBM_GETPOS, 0, 0)), 0.01f, 0.60f);
        m_quickEditRadiusV = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRadiusV, TBM_GETPOS, 0, 0)), 0.01f, 0.60f);
        m_quickEditInnerRadius = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditInnerRadius, TBM_GETPOS, 0, 0)), 0.0f, 0.95f);
        m_quickEditAngleCenterRadians = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditAngleCenter, TBM_GETPOS, 0, 0)), -3.1415926f, 3.1415926f);
        m_quickEditAngleHalfExtentRadians = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditAngleHalfExtent, TBM_GETPOS, 0, 0)), 0.05f, 3.1415926f);
        m_quickEditRootOffsetX = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRootOffsetX, TBM_GETPOS, 0, 0)), -0.12f, 0.12f);
        m_quickEditRootOffsetY = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRootOffsetY, TBM_GETPOS, 0, 0)), -0.12f, 0.12f);
        m_quickEditRootOffsetZ = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRootOffsetZ, TBM_GETPOS, 0, 0)), -0.12f, 0.12f);
        m_quickEditTipOffsetX = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditTipOffsetX, TBM_GETPOS, 0, 0)), -0.20f, 0.20f);
        m_quickEditTipOffsetY = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditTipOffsetY, TBM_GETPOS, 0, 0)), -0.20f, 0.20f);
        m_quickEditTipOffsetZ = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditTipOffsetZ, TBM_GETPOS, 0, 0)), -0.20f, 0.20f);
        m_quickEditSideOffset = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditSideOffset, TBM_GETPOS, 0, 0)), -0.20f, 0.20f);
        m_quickEditYawRadians = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditYaw, TBM_GETPOS, 0, 0)), -1.5707963f, 1.5707963f);
        m_quickEditWidthScale = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditWidthScale, TBM_GETPOS, 0, 0)), 0.50f, 1.80f);
        m_quickEditFollowerInfluence = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditFollowerInfluence, TBM_GETPOS, 0, 0)), 0.0f, 1.5f);
        m_quickEditBoundarySmoothing = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditBoundarySmoothing, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_quickEditBoundaryRadiusUv = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditBoundaryRadiusUv, TBM_GETPOS, 0, 0)), 0.01f, 0.30f);
        m_quickEditGroupId = ParseOptionalGroupIdText(GetWindowTextString(m_editQuickEditGroupId));
        m_quickEditGuidesOnly = (SendMessageW(m_checkQuickEditGuidesOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_quickEditInnerRadius = hairgpu::Clamp(m_quickEditInnerRadius, 0.0f, 0.95f);
        const float quickEditMinRadius = std::min(m_quickEditRadiusU, m_quickEditRadiusV);
        if (quickEditMinRadius > 1.0e-4f)
        {
            m_quickEditInnerRadius = std::min(m_quickEditInnerRadius, quickEditMinRadius * 0.95f);
        }
        m_quickEditBoundarySmoothing = hairgpu::Clamp(m_quickEditBoundarySmoothing, 0.0f, 1.0f);
        m_quickEditBoundaryRadiusUv = hairgpu::Clamp(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f);

        m_activeSetBudget.enableBudgetAdaptation = (SendMessageW(m_checkActiveSetAdaptiveBudget, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_activeSetBudget.enableHardBudgetClamp = (SendMessageW(m_checkActiveSetHardClamp, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_activeSetBudget.maxActiveClusters = static_cast<uint32_t>(std::round(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetMaxClusters, TBM_GETPOS, 0, 0)), 1.0f, 64.0f)));
        m_activeSetBudget.maxActiveGuides = static_cast<uint32_t>(std::round(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetMaxGuides, TBM_GETPOS, 0, 0)), 8.0f, 2048.0f)));
        m_activeSetBudget.promoteScoreThreshold = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetThreshold, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);
        m_activeSetBudget.targetFrameMilliseconds = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetTargetFrame, TBM_GETPOS, 0, 0)), 4.0f, 16.67f), 4.0f, 16.67f);
        m_activeSetBudget.estimatedFixedCostMilliseconds = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetFixedCost, TBM_GETPOS, 0, 0)), 0.0f, 16.0f), 0.0f, 16.0f);
        m_activeSetBudget.estimatedClusterCostMilliseconds = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetClusterCost, TBM_GETPOS, 0, 0)), 0.05f, 4.0f), 0.05f, 4.0f);
        m_activeSetBudget.minActiveClusters = std::min<uint32_t>(m_activeSetBudget.minActiveClusters, m_activeSetBudget.maxActiveClusters);
        m_activeSetBudget.maxActiveGuides = std::max<uint32_t>(m_activeSetBudget.maxActiveGuides, m_activeSetBudget.maxActiveClusters);
        m_activeSetBudget.frameBudgetMilliseconds = 16.67f;
        m_activeSetScoringSettings.motionWeight = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetMotionWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);
        m_activeSetScoringSettings.contactWeight = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetContactWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);
        m_activeSetScoringSettings.uncertaintyWeight = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetUncertaintyWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);
        m_activeSetScoringSettings.editWeight = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetEditWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);

        BOOL ok = FALSE;
        UINT value = GetDlgItemInt(m_hwnd, IDC_EDIT_GUIDES, &ok, FALSE);
        if (ok) { m_builderGuideCount = std::max<UINT>(64u, std::min<UINT>(value, 12000u)); }
        value = GetDlgItemInt(m_hwnd, IDC_EDIT_FOLLOWERS, &ok, FALSE);
        if (ok) { m_builderFollowersPerGuide = std::min<UINT>(value, 64u); }
        value = GetDlgItemInt(m_hwnd, IDC_EDIT_PARTICLES, &ok, FALSE);
        if (ok) { m_builderParticlesPerStrand = std::max<UINT>(8u, std::min<UINT>(value, 160u)); }

