// GpuHairViewer.cpp ordered part 15: shutdown_and_tail
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

            case IDC_COMBO_PRESET:
                if (code == CBN_SELCHANGE)
                {
                    const int preset = static_cast<int>(SendMessageW(m_comboPreset, CB_GETCURSEL, 0, 0));
                    ApplyPreset(preset);
                    RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                }
                return 0;
            case IDC_COMBO_RENDER_MODE:
            case IDC_COMBO_INTERACTION_MODE:
            case IDC_COMBO_DEBUG_COLOR:
            case IDC_COMBO_MASK_SOURCE:
            case IDC_COMBO_MASK_OVERLAY:
            case IDC_COMBO_QUICK_EDIT_MODE:
                if (code == CBN_SELCHANGE)
                {
                    SyncStateFromControls();
                    RefreshMaskPreviewControls();
                    if (LOWORD(wParam) == IDC_COMBO_INTERACTION_MODE)
                    {
                        LayoutUiControls();
                    }
                    if (LOWORD(wParam) == IDC_COMBO_MASK_SOURCE)
                    {
                        RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                    }
                    else if (LOWORD(wParam) == IDC_COMBO_MASK_OVERLAY)
                    {
                        UpdateScalpOverlayVertexBuffer();
                    }
                }
                return 0;
            case IDC_EDIT_QUICK_EDIT_NAME:
                if (code == EN_KILLFOCUS)
                {
                    SyncStateFromControls();
                }
                return 0;
            case IDC_EDIT_GUIDES:
            case IDC_EDIT_FOLLOWERS:
            case IDC_EDIT_PARTICLES:
                if (code == EN_KILLFOCUS)
                {
                    SyncStateFromControls();
                    RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                }
                return 0;
            case IDC_CHECK_SELF_COLLISION:
            case IDC_CHECK_GRID:
            case IDC_CHECK_SCALP_MOTION:
            case IDC_CHECK_SHOW_COLLIDERS:
            case IDC_CHECK_SHOW_SCALP_MESH:
            case IDC_CHECK_SHOW_SCALP:
            case IDC_CHECK_SHOW_GRID_BOUNDS:
            case IDC_CHECK_SHOW_FLOOR:
            case IDC_CHECK_SHOW_GUIDES:
            case IDC_CHECK_SHOW_FOLLOWERS:
            case IDC_CHECK_SHOW_ROOTS:
            case IDC_CHECK_SHOW_LINKS:
            case IDC_CHECK_ACTIVESET_ADAPTIVE_BUDGET:
            case IDC_CHECK_ACTIVESET_HARD_CLAMP:
                if (code == BN_CLICKED)
                {
                    SyncStateFromControls();
                }
                return 0;
            default:
                break;
            }
            break;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                EndStrandGizmoDrag(true);
                EndViewportBrushStroke(true);
                ReleaseCapture();
                m_dragging = false;
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
            {
                switch (wParam)
                {
                case 'Z':
                    UndoStrandEdit();
                    return 0;
                case 'Y':
                    RedoStrandEdit();
                    return 0;
                case 'S':
                {
                    wchar_t fileName[MAX_PATH] = L"viewer_strand_edits.strandedit";
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"Strand Edit Session (*.strandedit)\0*.strandedit\0All Files\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                    if (GetSaveFileNameW(&ofn) && !SaveStrandEditSessionToPath(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to save the strand edit session.", L"Strand Edit", MB_OK | MB_ICONERROR);
                    }
                    return 0;
                }
                case 'O':
                {
                    wchar_t fileName[MAX_PATH] = L"";
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"Strand Edit Session (*.strandedit)\0*.strandedit\0All Files\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&ofn) && !LoadStrandEditSessionFromPath(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to load the strand edit session.", L"Strand Edit", MB_OK | MB_ICONERROR);
                    }
                    return 0;
                }
                default:
                    break;
                }
            }
            if (IsGroomWorkspace() && (GetKeyState(VK_MENU) & 0x8000) == 0)
            {
                switch (wParam)
                {
                case 'Q': ApplyGroomToolPreset(GroomTool::Select); return 0;
                case 'W': ApplyGroomToolPreset(GroomTool::Comb); return 0;
                case 'E': ApplyGroomToolPreset(GroomTool::Grab); return 0;
                case 'A': ApplyGroomToolPreset(GroomTool::Smooth); return 0;
                case 'S': ApplyGroomToolPreset(GroomTool::Length); return 0;
                case 'D': ApplyGroomToolPreset(GroomTool::Volume); return 0;
                case 'P': ApplyGroomToolPreset(GroomTool::Part); return 0;
                case 'X': ApplyGroomToolPreset(GroomTool::Freeze); return 0;
                case 'V': ApplyGroomToolPreset(GroomTool::StrandFix); return 0;
                case VK_TAB:
                    m_showBottomTray = !m_showBottomTray;
                    SyncControlsFromState();
                    LayoutUiControls();
                    return 0;
                default:
                    break;
                }
            }

            switch (wParam)
            {
            case VK_SPACE:
                m_paused = !m_paused;
                SyncControlsFromState();
                return 0;
            case 'R':
                ResetSimulation();
                return 0;
            case 'N':
                if (m_paused)
                {
                    StepSingleFrame();
                }
                return 0;
            case 'C':
                m_debugColorMode = static_cast<DebugColorMode>((static_cast<uint32_t>(m_debugColorMode) + 1u) % 4u);
                SyncControlsFromState();
                return 0;
            case 'F':
                m_showFloorGuide = !m_showFloorGuide;
                SyncControlsFromState();
                return 0;
            case 'H':
                m_showGridBounds = !m_showGridBounds;
                SyncControlsFromState();
                return 0;
            case '1':
                m_widthScale = Clamp(m_widthScale * 0.85f, 0.2f, 8.0f);
                SyncControlsFromState();
                return 0;
            case '2':
                m_widthScale = Clamp(m_widthScale * 1.15f, 0.2f, 8.0f);
                SyncControlsFromState();
                return 0;
            case '3':
                m_enableSelfCollision = !m_enableSelfCollision;
                SyncControlsFromState();
                return 0;
            case '4':
                SyncControlsFromState();
                return 0;
            case '5':
                m_selfCollisionRadius = Clamp(m_selfCollisionRadius * 0.90f, 0.004f, 0.040f);
                SyncControlsFromState();
                return 0;
            case '6':
                m_selfCollisionRadius = Clamp(m_selfCollisionRadius * 1.10f, 0.004f, 0.040f);
                SyncControlsFromState();
                return 0;
            case '7':
                m_simConfig.stretchCompliance *= 0.5f;
                SyncControlsFromState();
                return 0;
            case '8':
                m_simConfig.stretchCompliance *= 2.0f;
                SyncControlsFromState();
                return 0;
            case '9':
                m_simConfig.bendCompliance *= 0.5f;
                SyncControlsFromState();
                return 0;
            case '0':
                m_simConfig.bendCompliance *= 2.0f;
                SyncControlsFromState();
                return 0;
            case 'G':
                m_enableGridSelfCollision = !m_enableGridSelfCollision;
                SyncControlsFromState();
                return 0;
            case 'B':
                m_selfCollisionBroadphaseMode = 0u;
                m_enableGridSelfCollision = true;
                SyncControlsFromState();
                return 0;
            case 'M':
                m_showScalpMesh = !m_showScalpMesh;
                SyncControlsFromState();
                return 0;
            case 'J':
                m_showGuideSkeleton = !m_showGuideSkeleton;
                SyncControlsFromState();
                return 0;
            case 'K':
                m_showFollowerSkeleton = !m_showFollowerSkeleton;
                SyncControlsFromState();
                return 0;
            case 'L':
                m_showFollowLinks = !m_showFollowLinks;
                SyncControlsFromState();
                return 0;
            case 'U':
                m_showRootMarkers = !m_showRootMarkers;
                SyncControlsFromState();
                return 0;
            case 'T':
                m_renderMode = (m_renderMode == RenderMode::ParticlePointSprite) ? RenderMode::ParticleBillboard :
                               (m_renderMode == RenderMode::ParticleBillboard) ? RenderMode::StrandRibbon :
                               RenderMode::ParticlePointSprite;
                SyncControlsFromState();
                return 0;
            case VK_OEM_MINUS:
                m_simConfig.selfCollisionCellSize = Clamp(m_simConfig.selfCollisionCellSize * 0.92f, 0.03f, 0.20f);
                SyncControlsFromState();
                return 0;
            case VK_OEM_PLUS:
                m_simConfig.selfCollisionCellSize = Clamp(m_simConfig.selfCollisionCellSize * 1.08f, 0.03f, 0.20f);
                SyncControlsFromState();
                return 0;
            case VK_LEFT:
                if (m_selectedStrandIndex >= 0) { ApplySelectedStrandOffset(Vec3(0.0f, 0.0f, 0.0f), Vec3(-0.010f, 0.0f, 0.0f)); }
                return 0;
            case VK_RIGHT:
                if (m_selectedStrandIndex >= 0) { ApplySelectedStrandOffset(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.010f, 0.0f, 0.0f)); }
                return 0;
            case VK_UP:
                if (m_selectedStrandIndex >= 0) { ApplySelectedStrandOffset(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.010f, 0.0f)); }
                return 0;
            case VK_DOWN:
                if (m_selectedStrandIndex >= 0) { ApplySelectedStrandOffset(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, -0.010f, 0.0f)); }
                return 0;
            case VK_PRIOR:
                if (m_selectedStrandIndex >= 0) { ApplySelectedStrandOffset(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -0.010f)); }
                return 0;
            case VK_NEXT:
                if (m_selectedStrandIndex >= 0) { ApplySelectedStrandOffset(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.010f)); }
                return 0;
            default:
                break;
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool GpuHairViewer::LoadScalpAnimationFromPath(const std::wstring& filePath, bool showMessageOnFailure)
    {
        std::string error;
        hairgpu::BoneAnimationClip clip = {};
        if (!hairgpu::LoadBoneAnimationCsv(std::filesystem::path(filePath), clip, error))
        {
            if (showMessageOnFailure)
            {
                const std::wstring werr(error.begin(), error.end());
                MessageBoxW(m_hwnd, werr.c_str(), L"Scalp Bone Animation", MB_OK | MB_ICONERROR);
            }
            return false;
        }
        m_scalpAnimationClip = std::move(clip);
        m_lastScalpAnimationSource = filePath;
        return true;
    }



}
