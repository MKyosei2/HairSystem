// GpuHairViewer.cpp ordered part 14: scalp_overlay_and_window_messages
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

        if (m_showScalpWireframe)
        {
            for (const TriangleGpuData& tri : m_scalpTriangles)
            {
                if (tri.a >= m_animatedScalpParticles.size() || tri.b >= m_animatedScalpParticles.size() || tri.c >= m_animatedScalpParticles.size())
                {
                    continue;
                }
                addScalpLine(tri.a, tri.b, 0.25f, 0.85f, 1.0f, 0.55f);
                addScalpLine(tri.b, tri.c, 0.25f, 0.85f, 1.0f, 0.55f);
                addScalpLine(tri.c, tri.a, 0.25f, 0.85f, 1.0f, 0.55f);
            }
        }

        if (m_selectedStrandIndex >= 0 && static_cast<size_t>(m_selectedStrandIndex) < m_asset.strands.size())
        {
            const bool rootMode = (m_strandGizmoMode == StrandGizmoMode::RootTranslate);
            const Vec3 anchor = GetSelectedStrandAnchorPoint(rootMode);
            const float gizmoScale = std::max(0.06f, m_cameraDistance * 0.10f);
            addWorldLine(anchor, anchor + Vec3(gizmoScale, 0.0f, 0.0f), 1.0f, 0.22f, 0.22f, 0.95f);
            addWorldLine(anchor, anchor + Vec3(0.0f, gizmoScale, 0.0f), 0.22f, 1.0f, 0.22f, 0.95f);
            addWorldLine(anchor, anchor + Vec3(0.0f, 0.0f, gizmoScale), 0.22f, 0.52f, 1.0f, 0.95f);

            const StrandRange& selectedStrand = m_asset.strands[static_cast<size_t>(m_selectedStrandIndex)];
            const float pointMarkerScale = std::max(0.024f, m_cameraDistance * 0.0160f);
            for (uint32_t i = 0u; i < selectedStrand.pointCount; ++i)
            {
                const uint32_t particleIndex = selectedStrand.pointOffset + i;
                if (i + 1u < selectedStrand.pointCount)
                {
                    addHairLine(particleIndex, particleIndex + 1u, 1.0f, 0.25f, 0.86f, 1.00f);
                }
                const float t = (selectedStrand.pointCount > 1u) ? (static_cast<float>(i) / static_cast<float>(selectedStrand.pointCount - 1u)) : 0.0f;
                const bool isRoot = (i == 0u);
                const bool isTip = (i + 1u == selectedStrand.pointCount);
                const float r = isRoot ? 1.00f : (isTip ? 0.18f : 1.00f);
                const float g = isRoot ? 0.26f : (isTip ? 0.92f : 0.92f);
                const float b = isRoot ? 0.26f : (isTip ? 1.00f : 0.18f);
                const float scaleBoost = isRoot ? 1.55f : (isTip ? 1.40f : 1.15f);
                for (uint32_t axis = 0u; axis < 3u; ++axis)
                {
                    GpuDebugLineEntry entry = {};
                    entry.type = 3u;
                    entry.indexA = particleIndex;
                    entry.axisOrFlags = axis;
                    entry.data0[3] = pointMarkerScale * scaleBoost * (0.92f + 0.40f * (1.0f - std::fabs(t - 0.5f)));
                    entry.color[0] = r; entry.color[1] = g; entry.color[2] = b; entry.color[3] = 1.0f;
                    m_gpuDebugLineEntries.push_back(entry);
                    ++m_debugLineStats.rootMarkerCount;
                }
                GpuDebugLineEntry verticalStem = {};
                verticalStem.type = 3u;
                verticalStem.indexA = particleIndex;
                verticalStem.axisOrFlags = 1u;
                verticalStem.data0[3] = pointMarkerScale * (isRoot ? 1.80f : (isTip ? 1.60f : 1.25f));
                verticalStem.color[0] = r; verticalStem.color[1] = g; verticalStem.color[2] = b; verticalStem.color[3] = 0.92f;
                m_gpuDebugLineEntries.push_back(verticalStem);
                ++m_debugLineStats.rootMarkerCount;
            }
        }

        if (!m_lockedStrands.empty())
        {
            const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
            for (uint32_t strandIndex = 0u; strandIndex < std::min<uint32_t>(guideCount, static_cast<uint32_t>(m_lockedStrands.size())); ++strandIndex)
            {
                if (!IsStrandLocked(strandIndex))
                {
                    continue;
                }
                const StrandRange& strand = m_asset.strands[strandIndex];
                if (strand.pointCount > 0u)
                {
                    addRootMarker(strand.pointOffset, true);
                }
            }
        }

        if (m_showColliderDebug)
        {
            constexpr int segments = 24;
            for (const SphereCollider& sphere : m_colliders.spheres)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    for (int i = 0; i < segments; ++i)
                    {
                        const float a0 = (static_cast<float>(i) / segments) * 6.28318530718f;
                        const float a1 = (static_cast<float>(i + 1) / segments) * 6.28318530718f;
                        Vec3 p0 = sphere.center;
                        Vec3 p1 = sphere.center;
                        if (axis == 0)
                        {
                            p0.y += std::cos(a0) * sphere.radius; p0.z += std::sin(a0) * sphere.radius;
                            p1.y += std::cos(a1) * sphere.radius; p1.z += std::sin(a1) * sphere.radius;
                        }
                        else if (axis == 1)
                        {
                            p0.x += std::cos(a0) * sphere.radius; p0.z += std::sin(a0) * sphere.radius;
                            p1.x += std::cos(a1) * sphere.radius; p1.z += std::sin(a1) * sphere.radius;
                        }
                        else
                        {
                            p0.x += std::cos(a0) * sphere.radius; p0.y += std::sin(a0) * sphere.radius;
                            p1.x += std::cos(a1) * sphere.radius; p1.y += std::sin(a1) * sphere.radius;
                        }
                        addWorldLine(p0, p1, 1.0f, 0.55f, 0.15f, 0.8f);
                    }
                }
            }

            for (const CapsuleCollider& capsule : m_colliders.capsules)
            {
                const Vec3 axis = Normalize(capsule.pointB - capsule.pointA);
                Vec3 temp = (std::fabs(axis.y) < 0.95f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
                const Vec3 tangent = Normalize(Cross(axis, temp));
                const Vec3 bitangent = Normalize(Cross(axis, tangent));
                constexpr int segments = 24;
                for (int i = 0; i < segments; ++i)
                {
                    const float a0 = (static_cast<float>(i) / segments) * 6.28318530718f;
                    const float a1 = (static_cast<float>(i + 1) / segments) * 6.28318530718f;
                    const Vec3 ring0 = tangent * std::cos(a0) + bitangent * std::sin(a0);
                    const Vec3 ring1 = tangent * std::cos(a1) + bitangent * std::sin(a1);
                    const Vec3 a0p = capsule.pointA + ring0 * capsule.radiusA;
                    const Vec3 a1p = capsule.pointA + ring1 * capsule.radiusA;
                    const Vec3 b0p = capsule.pointB + ring0 * capsule.radiusB;
                    const Vec3 b1p = capsule.pointB + ring1 * capsule.radiusB;
                    addWorldLine(a0p, a1p, 0.85f, 0.35f, 0.90f, 0.75f);
                    addWorldLine(b0p, b1p, 0.85f, 0.35f, 0.90f, 0.75f);
                    if ((i % 6) == 0)
                    {
                        addWorldLine(a0p, b0p, 0.85f, 0.35f, 0.90f, 0.75f);
                    }
                }
            }
        }

        const bool needStrandDebug = m_showGuideSkeleton || m_showFollowerSkeleton || m_showRootMarkers || m_showFollowLinks;
        if (needStrandDebug)
        {
            const uint32_t guideCount = m_asset.guideStrandCount;
            const uint32_t followerCount = m_asset.followerStrandCount;
            const uint32_t totalCount = static_cast<uint32_t>(m_asset.strands.size());
            const uint32_t followerStride = std::max<uint32_t>(1u, followerCount / 96u + ((followerCount > 96u) ? 1u : 0u));

            auto addStrandSkeleton = [&](uint32_t strandIndex, float r, float g, float b, float alpha)
            {
                if (strandIndex >= totalCount)
                {
                    return;
                }
                const StrandRange& strand = m_asset.strands[strandIndex];
                for (uint32_t i = 1; i < strand.pointCount; ++i)
                {
                    const uint32_t aIndex = strand.pointOffset + i - 1u;
                    const uint32_t bIndex = strand.pointOffset + i;
                    addHairLine(aIndex, bIndex, r, g, b, alpha);
                }
            };

            if (m_showGuideSkeleton || m_showRootMarkers)
            {
                for (uint32_t strandIndex = 0; strandIndex < guideCount; ++strandIndex)
                {
                    const StrandRange& strand = m_asset.strands[strandIndex];
                    if (m_showGuideSkeleton)
                    {
                        addStrandSkeleton(strandIndex, 0.12f, 0.95f, 0.92f, 0.85f);
                    }
                    if (m_showRootMarkers)
                    {
                        addRootMarker(strand.pointOffset, true);
                    }
                }
            }

            if (m_showFollowerSkeleton || m_showRootMarkers || m_showFollowLinks)
            {
                for (uint32_t followerLocalIndex = 0; followerLocalIndex < followerCount; ++followerLocalIndex)
                {
                    if (followerLocalIndex % followerStride != 0u)
                    {
                        continue;
                    }
                    const uint32_t strandIndex = guideCount + followerLocalIndex;
                    if (strandIndex >= totalCount)
                    {
                        continue;
                    }
                    const StrandRange& strand = m_asset.strands[strandIndex];
                    if (m_showFollowerSkeleton)
                    {
                        addStrandSkeleton(strandIndex, 1.00f, 0.52f, 0.18f, 0.42f);
                    }
                    if (m_showRootMarkers)
                    {
                        addRootMarker(strand.pointOffset, false);
                    }
                    if (m_showFollowLinks && strandIndex < m_asset.followData.size())
                    {
                        const int32_t parentGuide = m_asset.followData[strandIndex].parentGuide;
                        if (parentGuide >= 0 && static_cast<uint32_t>(parentGuide) < guideCount)
                        {
                            addHairLine(strand.pointOffset, m_asset.strands[static_cast<uint32_t>(parentGuide)].pointOffset, 0.86f, 0.30f, 1.00f, 0.32f);
                        }
                    }
                }
            }
        }

        m_debugLineStats.totalLineCount = m_debugLineStats.worldLineCount + m_debugLineStats.hairLineCount + m_debugLineStats.scalpLineCount + m_debugLineStats.rootMarkerCount;
        m_gpuDebugLinesDirty = false;
    }

    void GpuHairViewer::UpdateRenderStatistics()
    {
        const uint64_t totalParticles = static_cast<uint64_t>(m_asset.restPoints.size());
        const uint64_t totalStrands = static_cast<uint64_t>(m_asset.strands.size());
        const uint64_t guideParticles = static_cast<uint64_t>(m_asset.guideStrandCount) * static_cast<uint64_t>(m_asset.particlesPerStrand);
        const uint64_t followerParticles = (totalParticles >= guideParticles) ? (totalParticles - guideParticles) : 0ull;
        (void)totalStrands;
        (void)guideParticles;
        (void)followerParticles;
    }

    LRESULT CALLBACK GpuHairViewer::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        GpuHairViewer* app = reinterpret_cast<GpuHairViewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<GpuHairViewer*>(create->lpCreateParams);
            if (app)
            {
                app->m_hwnd = hwnd;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }

        return app ? app->HandleMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT GpuHairViewer::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            Resize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_LBUTTONDOWN:
        {
            const RECT renderRect = this->GetRenderViewportRect();
            const POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (PtInRect(&renderRect, p))
            {
                m_mouseDownPoint = p;
                m_lastMouse = p;
                const bool orbitOverride = (GetKeyState(VK_MENU) & 0x8000) != 0;
                if (!orbitOverride && IsBrushDrivenTool() && BeginViewportBrushStroke(p))
                {
                    // brush stroke owns the drag until mouse-up or escape.
                }
                else if (!BeginStrandGizmoDrag(p))
                {
                    m_dragging = true;
                }
                SetCapture(m_hwnd);
            }
            return 0;
        }
        case WM_LBUTTONUP:
        {
            const POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int dx = p.x - m_mouseDownPoint.x;
            const int dy = p.y - m_mouseDownPoint.y;
            const bool wasClick = (dx * dx + dy * dy) <= 16;
            const bool wasGizmoDrag = m_gizmoDragging;
            const bool wasBrushStroke = m_brushStrokeActive;
            EndStrandGizmoDrag(false);
            EndViewportBrushStroke(false);
            m_dragging = false;
            ReleaseCapture();
            if (wasClick && !wasGizmoDrag && !wasBrushStroke)
            {
                PickStrandAtPoint(p);
                UpdateWindowTitle();
            }
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (m_gizmoDragging)
            {
                UpdateStrandGizmoDrag(p);
            }
            else if (m_brushStrokeActive)
            {
                UpdateViewportBrushStroke(p);
            }
            else if (m_dragging)
            {
                const float dx = static_cast<float>(p.x - m_lastMouse.x);
                const float dy = static_cast<float>(p.y - m_lastMouse.y);
                m_lastMouse = p;
                m_cameraYaw += dx * 0.01f;
                m_cameraPitch = Clamp(m_cameraPitch + dy * 0.01f, -1.2f, 1.2f);
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            POINT screenPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &screenPoint);
            const RECT renderRect = this->GetRenderViewportRect();
            if (PtInRect(&renderRect, screenPoint))
            {
                const float wheel = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
                if (IsBrushDrivenTool())
                {
                    m_quickEditRadiusU = Clamp(m_quickEditRadiusU + wheel * 0.015f, 0.01f, 0.60f);
                    m_quickEditRadiusV = Clamp(m_quickEditRadiusV + wheel * 0.015f, 0.01f, 0.60f);
                    SyncQuickEditFormControlsFromState();
                }
                else
                {
                    m_cameraDistance = Clamp(m_cameraDistance - wheel * 0.15f, 0.6f, 8.0f);
                }
            }
            return 0;
        }
        case WM_HSCROLL:
            if (!m_syncingControls)
            {
                SyncStateFromControls();
            }
            return 0;
        case WM_COMMAND:
        {
            if (m_syncingControls)
            {
                return 0;
            }

            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == IDC_LIST_INLINE_LAYERS && code == LBN_SELCHANGE)
            {
                m_selectedInlineLayerIndex = GetSelectedInlineLayerIndex();
                LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
                return 0;
            }
            switch (id)
            {
            case IDC_BTN_WORKSPACE_GROOM:
                ApplyWorkspaceMode(WorkspaceMode::Groom);
                return 0;
            case IDC_BTN_WORKSPACE_SIM:
                ApplyWorkspaceMode(WorkspaceMode::Sim);
                return 0;
            case IDC_BTN_WORKSPACE_REVIEW:
                ApplyWorkspaceMode(WorkspaceMode::Review);
                return 0;
            case IDC_BTN_WORKSPACE_REPORT:
                ApplyWorkspaceMode(WorkspaceMode::Report);
                return 0;
            case IDC_BTN_TOOL_SELECT:
                ApplyGroomToolPreset(GroomTool::Select);
                return 0;
            case IDC_BTN_TOOL_COMB:
                ApplyGroomToolPreset(GroomTool::Comb);
                return 0;
            case IDC_BTN_TOOL_GRAB:
                ApplyGroomToolPreset(GroomTool::Grab);
                return 0;
            case IDC_BTN_TOOL_SMOOTH:
                ApplyGroomToolPreset(GroomTool::Smooth);
                return 0;
            case IDC_BTN_TOOL_LENGTH:
                ApplyGroomToolPreset(GroomTool::Length);
                return 0;
            case IDC_BTN_TOOL_VOLUME:
                ApplyGroomToolPreset(GroomTool::Volume);
                return 0;
            case IDC_BTN_TOOL_PART:
                ApplyGroomToolPreset(GroomTool::Part);
                return 0;
            case IDC_BTN_TOOL_FREEZE:
                ApplyGroomToolPreset(GroomTool::Freeze);
                return 0;
            case IDC_BTN_TOOL_STRAND_FIX:
                ApplyGroomToolPreset(GroomTool::StrandFix);
                return 0;
            case IDC_BTN_UNDO_EDIT:
                UndoStrandEdit();
                RefreshAssetStatsText();
                RefreshQuickEditFormLabels();
                return 0;
            case IDC_BTN_REDO_EDIT:
                RedoStrandEdit();
                RefreshAssetStatsText();
                RefreshQuickEditFormLabels();
                return 0;
            case IDC_BTN_TRAY_LAYERS:
                m_bottomTrayTab = BottomTrayTab::Layers;
                SyncControlsFromState();
                LayoutUiControls();
                return 0;
            case IDC_BTN_TRAY_HISTORY:
                m_bottomTrayTab = BottomTrayTab::History;
                SyncControlsFromState();
                LayoutUiControls();
                return 0;
            case IDC_BTN_TRAY_METRICS:
                m_bottomTrayTab = BottomTrayTab::Metrics;
                SyncControlsFromState();
                LayoutUiControls();
                return 0;
            case IDC_BTN_TOGGLE_INSPECTOR:
                m_showInspectorPane = !m_showInspectorPane;
                SyncControlsFromState();
                LayoutUiControls();
                return 0;
            case IDC_BTN_TOGGLE_TRAY:
                m_showBottomTray = !m_showBottomTray;
                SyncControlsFromState();
                LayoutUiControls();
                return 0;
            case IDC_BTN_TOGGLE_ADVANCED:
                m_showAdvancedInspector = !m_showAdvancedInspector;
                if (m_showAdvancedInspector)
                {
                    m_showInspectorPane = true;
                }
                SyncControlsFromState();
                LayoutUiControls();
                return 0;
            case IDC_BTN_FAST_VIEWPORT:
                m_fastGroomViewport = !m_fastGroomViewport;
                SyncControlsFromState();
                return 0;
            case IDC_BTN_PLAYPAUSE:
                m_paused = !m_paused;
                SyncControlsFromState();
                return 0;
            case IDC_BTN_RESET:
                ResetSimulation();
                return 0;
            case IDC_BTN_STEP:
                StepSingleFrame();
                return 0;
            case IDC_BTN_APPLY_ASSET:
                SyncStateFromControls();
                RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                return 0;
            case IDC_BTN_BROWSE_EDIT_STACK:
                BrowseAndSetEditStackPath();
                return 0;
            case IDC_BTN_CLEAR_EDIT_STACK:
                if (!ClearEditStackAndRestoreAsset())
                {
                    MessageBoxW(hwnd, L"Failed to clear the current edit stack.", L"Edit Stack", MB_OK | MB_ICONERROR);
                }
                return 0;
            case IDC_BTN_APPLY_INLINE_EDIT_STACK:
                ApplyInlineEditStackFromUi(true);
                return 0;
            case IDC_BTN_SAVE_INLINE_EDIT_STACK:
            {
                wchar_t fileName[MAX_PATH] = L"viewer_inline_edit_stack.csv";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Edit Stack CSV (*.csv)\0*.csv\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!SaveInlineEditStackToPath(fileName, true))
                    {
                        MessageBoxW(hwnd, L"Failed to save the inline edit stack.", L"Inline Edit Stack", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_RESET_INLINE_EDIT_STACK:
                ResetInlineEditStackText();
                return 0;
            case IDC_BTN_QUICK_EDIT_REPLACE:
                PushQuickEditFormToInlineText(true, true);
                return 0;
            case IDC_BTN_QUICK_EDIT_APPEND:
                PushQuickEditFormToInlineText(false, true);
                return 0;
            case IDC_BTN_QUICK_EDIT_MOVE_UP:
                if (!MoveSelectedInlineLayer(-1, true))
                {
                    MessageBoxW(hwnd, L"The selected inline layer could not be moved up.", L"Quick Edit Form", MB_OK | MB_ICONWARNING);
                }
                return 0;
            case IDC_BTN_QUICK_EDIT_MOVE_DOWN:
                if (!MoveSelectedInlineLayer(1, true))
                {
                    MessageBoxW(hwnd, L"The selected inline layer could not be moved down.", L"Quick Edit Form", MB_OK | MB_ICONWARNING);
                }
                return 0;
            case IDC_CHECK_QUICK_EDIT_ENABLED:
                if (!m_syncingControls)
                {
                    ToggleSelectedInlineLayerEnabled(true);
                }
                return 0;
            case IDC_CHECK_QUICK_EDIT_GUIDES_ONLY:
                SyncStateFromControls();
                RefreshQuickEditFormLabels();
                if (IsEditInfluenceOverlayMode())
                {
                    UpdateScalpOverlayVertexBuffer();
                }
                return 0;
            case IDC_EDIT_QUICK_EDIT_GROUP_ID:
                if (code == EN_CHANGE)
                {
                    SyncStateFromControls();
                    RefreshQuickEditFormLabels();
                    if (IsEditInfluenceOverlayMode())
                    {
                        UpdateScalpOverlayVertexBuffer();
                    }
                }
                return 0;
            case IDC_BTN_QUICK_EDIT_DUPLICATE:
                if (!DuplicateSelectedInlineLayer(true))
                {
                    MessageBoxW(hwnd, L"No inline layer could be duplicated.", L"Quick Edit Form", MB_OK | MB_ICONWARNING);
                }
                return 0;
            case IDC_BTN_QUICK_EDIT_DELETE:
                if (!DeleteSelectedInlineLayer(true))
                {
                    MessageBoxW(hwnd, L"No inline layer could be deleted.", L"Quick Edit Form", MB_OK | MB_ICONWARNING);
                }
                return 0;
            case IDC_BTN_IMPORT_ASSET:
            {
                wchar_t fileName[MAX_PATH] = L"";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Hair Assets (*.csv;*.abc;*.hairbin;*.hair)\0*.csv;*.abc;*.hairbin;*.hair\0Curves CSV (*.csv)\0*.csv\0Alembic (*.abc)\0*.abc\0Hair Binary (*.hairbin;*.hair)\0*.hairbin;*.hair\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn))
                {
                    if (!LoadHairAssetFromPath(fileName))
                    {
                        const std::wstring message = L"Import failed.\r\n\r\n" + m_lastImportStatus;
                        MessageBoxW(hwnd, message.c_str(), L"Hair Import", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_SAVE_SETTINGS:
            {
                wchar_t fileName[MAX_PATH] = L"hair_viewer_settings.ini";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"INI Files\0*.ini\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!SaveSettingsToFile(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to save settings.", L"Save Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_LOAD_SETTINGS:
            {
                wchar_t fileName[MAX_PATH] = L"";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"INI Files\0*.ini\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn))
                {
                    if (!LoadSettingsFromFile(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to load settings.", L"Load Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_EXPORT_CSV:
            {
                wchar_t fileName[MAX_PATH] = L"hair_frame.csv";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"CSV Files\0*.csv\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!ExportCurrentFrameCsv(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to export current strand positions.", L"Export Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_EXPORT_PERF_CSV:
            {
                wchar_t fileName[MAX_PATH] = L"hair_performance.csv";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"CSV Files\0*.csv\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!ExportPerformanceCsv(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to export performance history.", L"Export Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_EXPORT_ACTIVESET_JSON:
            {
                wchar_t fileName[MAX_PATH] = L"active_set_plan.json";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"JSON Files\0*.json\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!ExportActiveSetPlanJson(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to export the active-set plan JSON.", L"Export Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_EXPORT_RUNTIME_REPORT:
            {
                wchar_t fileName[MAX_PATH] = L"hair_runtime_report.txt";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Text Files\0*.txt\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!ExportRuntimeReport(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to export the runtime report bundle.", L"Export Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_BROWSE_PARTING_MASK:
                if (BrowseAndSetMaskPath(true))
                {
                    RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                }
                return 0;
            case IDC_BTN_BROWSE_REGION_MASK:
                if (BrowseAndSetMaskPath(false))
                {
                    RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                }
                return 0;
            case IDC_PREVIEW_PARTING_MASK:
                if (code == STN_CLICKED) { ShowMaskPreviewPopup(true); }
                return 0;
            case IDC_PREVIEW_REGION_MASK:
                if (code == STN_CLICKED) { ShowMaskPreviewPopup(false); }
                return 0;
