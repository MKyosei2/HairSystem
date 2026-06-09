// GpuHairViewer.cpp ordered part 06: render_mesh_and_visibility_recovery
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

        if (m_gpuDebugLinesDirty)
        {
            RebuildGpuDebugLineEntries();
        }
        if (!m_gpuDebugLineEntries.empty() && EnsureGpuDebugLineCapacity(m_gpuDebugLineEntries.size()) && m_debugLineTemplateVertexBuffer && m_debugLineEntriesBuffer.srv)
        {
            m_context->UpdateSubresource(m_debugLineEntriesBuffer.buffer.Get(), 0, nullptr, m_gpuDebugLineEntries.data(), 0, 0);

            const UINT debugStride = sizeof(DebugLineTemplateVertex);
            ID3D11Buffer* debugBuffer = m_debugLineTemplateVertexBuffer.Get();
            ID3D11ShaderResourceView* debugSrvs[] = {
                m_positions[m_currentBufferIndex].srv.Get(),
                m_scalpPositions.srv.Get(),
                m_debugLineEntriesBuffer.srv.Get()
            };
            m_context->IASetInputLayout(m_debugInputLayout.Get());
            m_context->IASetVertexBuffers(0, 1, &debugBuffer, &debugStride, &offset);
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            m_context->VSSetShader(m_debugLineVS.Get(), nullptr, 0);
            m_context->GSSetShader(nullptr, nullptr, 0);
            m_context->PSSetShader(m_debugLinePS.Get(), nullptr, 0);
            m_context->VSSetConstantBuffers(0, 1, &renderCb);
            m_context->PSSetConstantBuffers(0, 1, &renderCb);
            m_context->VSSetShaderResources(0, 3, debugSrvs);
            m_context->DrawInstanced(2u, static_cast<UINT>(m_gpuDebugLineEntries.size()), 0u, 0u);
            ID3D11ShaderResourceView* nullDebugSrvs[3] = {};
            m_context->VSSetShaderResources(0, 3, nullDebugSrvs);
            ++m_lastTotalDrawCalls;
        }

        if (gpuTimingFrame && gpuTimingFrame->issued)
        {
            m_context->End(gpuTimingFrame->debugPipelineStats.Get());
            m_context->End(gpuTimingFrame->debugDrawEnd.Get());
            m_context->End(gpuTimingFrame->frameEnd.Get());
            m_context->End(gpuTimingFrame->disjoint.Get());
        }

        m_lastTotalDrawCalls += m_lastCurrentHairDrawCalls;
        UpdateRenderStatistics();

        m_swapChain->Present(1, 0);
        UpdateWindowTitle();
    }

    void GpuHairViewer::Resize(UINT width, UINT height)
    {
        m_width = std::max<UINT>(width, 1u);
        m_height = std::max<UINT>(height, 1u);
        if (!m_swapChain)
        {
            return;
        }

        m_rtv.Reset();
        m_dsv.Reset();
        m_depthTexture.Reset();
        m_context->OMSetRenderTargets(0, nullptr, nullptr);

        m_swapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0);

        ComPtr<ID3D11Texture2D> backBuffer;
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);

        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = m_width;
        depthDesc.Height = m_height;
        depthDesc.ArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthDesc.SampleDesc.Count = 1;
        m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthTexture);
        m_device->CreateDepthStencilView(m_depthTexture.Get(), nullptr, &m_dsv);
        QueueVisibilityRecovery();
        LayoutUiControls();
    }

    void GpuHairViewer::UpdateWindowTitle()
    {
        std::wstringstream ss;
        ss << L"Hair GPU Viewer ActiveSet Runtime [FOLLOWER_COMPACTION + STRAND_PICK + SDF]  |  preset=" << m_activePresetName
           << L"  fps=" << std::fixed << std::setprecision(1) << m_smoothedFps
           << L"  guides=" << m_asset.guideStrandCount
           << L"  followers=" << m_asset.followerStrandCount
           << L"  totalStrands=" << m_asset.strands.size()
           << L"  particles=" << m_asset.restPoints.size()
           << L"  render=" << (m_renderMode == RenderMode::ParticlePointSprite ? L"point" : (m_renderMode == RenderMode::ParticleBillboard ? L"billboard" : L"ribbon"))
           << L"  mode=" << (m_interactionMode == InteractionMode::Edit ? L"edit" : L"view")
           << L"  gpuFrameMs=" << std::setprecision(2) << m_lastGpuFrameMilliseconds
           << L"  vis=" << m_lastEstimatedVisibleParticles
           << L"  suspect=" << (m_lastVisibilitySuspect ? L"yes" : L"no")
           << L"  cpuFallback=" << (m_lastUsedCpuVisibilityFallback ? L"yes" : L"no")
           << L"  readback=" << (m_cpuReadbackMode == CpuReadbackMode::Off ? L"off" : (m_cpuReadbackMode == CpuReadbackMode::FullParticlesDebug ? L"full-debug" : L"on-demand"))
           << L"  readbackValid=" << (m_lastVisibilityReadbackValid ? L"yes" : L"no")
           << L"  initialPoseFallback=" << (m_lastVisibilityUsedInitialPoseFallback ? L"yes" : L"no")
           << L"  recoveryPending=" << (m_visibilityRecoveryPending ? L"yes" : L"no")
           << L"  recoveries=" << m_visibilityRecoveryCount
           << L"  readbackFailures=" << m_visibilityReadbackFailureCount
           << L"  widthScale=" << m_widthScale
           << L"  stretchCompliance=" << m_simConfig.stretchCompliance
           << L"  selfCollision=" << (m_enableSelfCollision ? L"density-grid" : L"off")
           << L"  angularXPBD=" << (m_enableRuntimeAngularBend ? L"on" : L"off")
           << L"  scalpRoots=static-gpu"
           << L"  selectedStrand=" << ((m_selectedStrandIndex >= 0) ? std::to_wstring(m_selectedStrandIndex) : L"none")
           << L"  gizmo=" << ((m_strandGizmoMode == StrandGizmoMode::TipTranslate) ? L"tip" : ((m_strandGizmoMode == StrandGizmoMode::WholeTranslate) ? L"whole" : L"root"))
           << L"  scalpAnim=" << (m_lastScalpAnimationSource.empty() ? L"sample" : std::filesystem::path(m_lastScalpAnimationSource).filename().wstring())
           << (m_paused ? L"  [PAUSED]" : L"");
        SetWindowTextW(m_hwnd, ss.str().c_str());
    }

    void GpuHairViewer::Shutdown()
    {
        if (m_context)
        {
            m_context->ClearState();
        }
    }

    bool GpuHairViewer::CompileShaderFromFile(const wchar_t* filePath, const char* entryPoint, const char* target, ID3DBlob** blobOut, std::wstring& errorText)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    #if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #endif
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();

        std::vector<std::filesystem::path> candidates;
        auto pushCandidate = [&candidates](const std::filesystem::path& candidate)
        {
            if (candidate.empty())
            {
                return;
            }
            const auto normalized = candidate.lexically_normal();
            if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end())
            {
                candidates.push_back(normalized);
            }
        };

        pushCandidate(std::filesystem::path(filePath));
        pushCandidate(std::filesystem::path(HAIRGPU_VIEWER_SHADER_DIR) / filePath);
        pushCandidate(exeDir / filePath);
        pushCandidate(exeDir / L"shaders" / filePath);
        pushCandidate(std::filesystem::current_path() / filePath);
        pushCandidate(std::filesystem::current_path() / L"shaders" / filePath);

        std::filesystem::path searchRoot = exeDir;
        for (int i = 0; i < 6; ++i)
        {
            pushCandidate(searchRoot / L"apps" / L"dx11_viewer" / L"shaders" / filePath);
            pushCandidate(searchRoot / L"dx11_viewer" / L"shaders" / filePath);
            if (!searchRoot.has_parent_path())
            {
                break;
            }
            const auto parent = searchRoot.parent_path();
            if (parent == searchRoot)
            {
                break;
            }
            searchRoot = parent;
        }

        auto loadFileBytes = [](const std::filesystem::path& path, std::string& bytes) -> bool
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                return false;
            }
            in.seekg(0, std::ios::end);
            const std::streamoff size = in.tellg();
            if (size < 0)
            {
                return false;
            }
            in.seekg(0, std::ios::beg);
            bytes.resize(static_cast<size_t>(size));
            if (size > 0)
            {
                in.read(bytes.data(), size);
            }
            return static_cast<bool>(in) || size == 0;
        };

        HRESULT hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        std::wstringstream tried;
        std::wstring compiledPath;
        for (const auto& candidate : candidates)
        {
            const bool exists = std::filesystem::exists(candidate);
            tried << candidate.c_str();
            tried << (exists ? L"  [exists]" : L"  [missing]") << L"\r\n";
            if (!exists)
            {
                continue;
            }

            std::string sourceBytes;
            if (!loadFileBytes(candidate, sourceBytes))
            {
                hr = HRESULT_FROM_WIN32(ERROR_READ_FAULT);
                continue;
            }

            errorBlob.Reset();
            shaderBlob.Reset();
            compiledPath = candidate.wstring();
            const std::string sourceName = candidate.string();
            hr = D3DCompile(sourceBytes.data(), sourceBytes.size(), sourceName.c_str(), nullptr, nullptr, entryPoint, target, flags, 0, &shaderBlob, &errorBlob);
            if (SUCCEEDED(hr))
            {
                break;
            }
        }
        if (FAILED(hr))
        {
            if (errorBlob)
            {
                const std::string message(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
                errorText.assign(message.begin(), message.end());
            }
            else
            {
                errorText = L"Unknown shader compilation error.";
                std::wstringstream hrHex;
                hrHex << std::hex << std::uppercase << static_cast<unsigned long>(hr);
                errorText += L"\r\nHRESULT: 0x";
                errorText += hrHex.str();
            }
            if (!compiledPath.empty())
            {
                errorText += L"\r\nShader source path: ";
                errorText += compiledPath;
            }
            errorText += L"\r\n\r\nTried paths:\r\n";
            errorText += tried.str();
            errorText += L"\r\nHint: verify that the .hlsl files were copied next to the executable or into its shaders subfolder.";
            return false;
        }
        *blobOut = shaderBlob.Detach();
        return true;
    }

    GpuHairViewer::StructuredBuffer GpuHairViewer::CreateStructuredBuffer(UINT elementSize, UINT elementCount, const void* initialData, bool unorderedAccess)
    {
        StructuredBuffer result;

        D3D11_BUFFER_DESC desc = {};
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (unorderedAccess ? D3D11_BIND_UNORDERED_ACCESS : 0u);
        desc.ByteWidth = std::max<UINT>(elementSize * elementCount, elementSize);
        desc.StructureByteStride = elementSize;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.Usage = D3D11_USAGE_DEFAULT;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = initialData;
        m_device->CreateBuffer(&desc, initialData ? &data : nullptr, &result.buffer);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = elementCount;
        m_device->CreateShaderResourceView(result.buffer.Get(), &srvDesc, &result.srv);

        if (unorderedAccess)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = elementCount;
            m_device->CreateUnorderedAccessView(result.buffer.Get(), &uavDesc, &result.uav);
        }

        return result;
    }


    RECT GpuHairViewer::GetRenderViewportRect() const
    {
        RECT rect = { 0, 0, static_cast<LONG>(std::max<UINT>(m_width > static_cast<UINT>(kUiPanelWidth) ? m_width - kUiPanelWidth : m_width, 1u)),
                      static_cast<LONG>(std::max<UINT>(m_height, 1u)) };
        return rect;
    }

    bool GpuHairViewer::CreateUiControls()
    {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto createControl = [&](DWORD exStyle, const wchar_t* klass, const wchar_t* text, DWORD style, int id) -> HWND
        {
            HWND hwnd = CreateWindowExW(exStyle, klass, text, style | WS_CHILD | WS_VISIBLE,
                0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            if (hwnd && font)
            {
                SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            return hwnd;
        };

        m_uiPanel = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_LEFT, 0);
        if (!m_uiPanel)
        {
            return false;
        }

        createControl(0, L"STATIC", L"Preset", SS_LEFT, 0);
        m_comboPreset = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_PRESET);
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Rapunzel Preview"));
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Calm Studio"));
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Windy Demo"));
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Heavy Grounded"));
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Game-ish LOD"));

        m_btnPlayPause = createControl(0, L"BUTTON", L"Pause", BS_PUSHBUTTON, IDC_BTN_PLAYPAUSE);
        m_btnReset = createControl(0, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_BTN_RESET);
        m_btnStep = createControl(0, L"BUTTON", L"Step", BS_PUSHBUTTON, IDC_BTN_STEP);
        m_btnApplyAsset = createControl(0, L"BUTTON", L"Rebuild Hair", BS_PUSHBUTTON, IDC_BTN_APPLY_ASSET);
        m_btnImportAsset = createControl(0, L"BUTTON", L"Import Hair...", BS_PUSHBUTTON, IDC_BTN_IMPORT_ASSET);
        m_btnSaveSettings = createControl(0, L"BUTTON", L"Save Settings", BS_PUSHBUTTON, IDC_BTN_SAVE_SETTINGS);
        m_btnLoadSettings = createControl(0, L"BUTTON", L"Load Settings", BS_PUSHBUTTON, IDC_BTN_LOAD_SETTINGS);
        m_btnExportCsv = createControl(0, L"BUTTON", L"Export Frame CSV", BS_PUSHBUTTON, IDC_BTN_EXPORT_CSV);
        m_btnExportPerfCsv = createControl(0, L"BUTTON", L"Export Perf CSV", BS_PUSHBUTTON, IDC_BTN_EXPORT_PERF_CSV);
        m_btnBrowseEditStack = createControl(0, L"BUTTON", L"Open Saved Edits...", BS_PUSHBUTTON, IDC_BTN_BROWSE_EDIT_STACK);
        m_btnClearEditStack = createControl(0, L"BUTTON", L"Reset All Edits", BS_PUSHBUTTON, IDC_BTN_CLEAR_EDIT_STACK);
        m_labelEditStack = createControl(0, L"STATIC", L"Edit stack: none", SS_LEFT, 0);
        m_labelInlineEditStack = createControl(0, L"STATIC", L"Advanced Stack Text (optional)", SS_LEFT, IDC_LABEL_INLINE_EDIT_STACK);
        m_editInlineEditStack = createControl(WS_EX_CLIENTEDGE, L"EDIT", BuildDefaultInlineEditStackText().c_str(), ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, IDC_EDIT_INLINE_EDIT_STACK);
        m_btnApplyInlineEditStack = createControl(0, L"BUTTON", L"Apply Inline", BS_PUSHBUTTON, IDC_BTN_APPLY_INLINE_EDIT_STACK);
        m_btnSaveInlineEditStack = createControl(0, L"BUTTON", L"Save Inline...", BS_PUSHBUTTON, IDC_BTN_SAVE_INLINE_EDIT_STACK);
        m_btnResetInlineEditStack = createControl(0, L"BUTTON", L"Reset Inline", BS_PUSHBUTTON, IDC_BTN_RESET_INLINE_EDIT_STACK);
        m_labelQuickEditForm = createControl(0, L"STATIC", L"3) Edit the Selected Layer", SS_LEFT, IDC_LABEL_QUICK_EDIT_FORM);
        m_labelLayerList = createControl(0, L"STATIC", L"2) Pick a Layer", SS_LEFT, IDC_LABEL_INLINE_LAYER_LIST);
        m_listInlineLayers = createControl(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"", LBS_NOTIFY | WS_VSCROLL | LBS_HASSTRINGS, IDC_LIST_INLINE_LAYERS);
        m_labelQuickEditName = createControl(0, L"STATIC", L"Layer Name", SS_LEFT, IDC_LABEL_QUICK_EDIT_NAME);
        m_editQuickEditName = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"quick_form", ES_AUTOHSCROLL, IDC_EDIT_QUICK_EDIT_NAME);
        m_btnQuickEditMoveUp = createControl(0, L"BUTTON", L"Move Up", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_MOVE_UP);
        m_btnQuickEditMoveDown = createControl(0, L"BUTTON", L"Move Down", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_MOVE_DOWN);
        m_checkQuickEditEnabled = createControl(0, L"BUTTON", L"Layer Enabled", BS_AUTOCHECKBOX, IDC_CHECK_QUICK_EDIT_ENABLED);
        m_btnQuickEditDelete = createControl(0, L"BUTTON", L"Delete Layer", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_DELETE);
        m_btnQuickEditDuplicate = createControl(0, L"BUTTON", L"Duplicate Layer", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_DUPLICATE);
        m_comboQuickEditMode = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_QUICK_EDIT_MODE);
        SendMessageW(m_comboQuickEditMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ellipse"));
        SendMessageW(m_comboQuickEditMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Rectangle"));
        SendMessageW(m_comboQuickEditMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Sector"));
        SendMessageW(m_comboQuickEditMode, CB_SETCURSEL, 0, 0);
        m_labelQuickEditFeather = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_FEATHER);
        m_labelQuickEditLengthScale = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_LENGTHSCALE);
        m_labelQuickEditLayerWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_LAYERWEIGHT);
        m_labelQuickEditCenterU = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_CENTER_U);
        m_labelQuickEditCenterV = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_CENTER_V);
        m_labelQuickEditRadiusU = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_RADIUS_U);
        m_labelQuickEditRadiusV = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_RADIUS_V);
        m_labelQuickEditInnerRadius = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_INNER_RADIUS);
        m_labelQuickEditAngleCenter = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ANGLE_CENTER);
        m_labelQuickEditAngleHalfExtent = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ANGLE_HALF_EXTENT);
        m_labelQuickEditRootOffsetX = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ROOT_OFFSET_X);
        m_labelQuickEditRootOffsetY = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ROOT_OFFSET_Y);
        m_labelQuickEditRootOffsetZ = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ROOT_OFFSET_Z);
        m_labelQuickEditTipOffsetX = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_TIP_OFFSET_X);
        m_labelQuickEditTipOffsetY = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_TIP_OFFSET_Y);
        m_labelQuickEditTipOffsetZ = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_TIP_OFFSET_Z);
        m_labelQuickEditSideOffset = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_SIDE_OFFSET);
        m_labelQuickEditYaw = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_YAW);
        m_labelQuickEditWidthScale = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_WIDTHSCALE);
        m_labelQuickEditFollowerInfluence = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_FOLLOWER_INFLUENCE);
        m_labelQuickEditBoundarySmoothing = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_BOUNDARY_SMOOTHING);
        m_labelQuickEditBoundaryRadiusUv = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_BOUNDARY_RADIUS_UV);
        m_labelQuickEditGroupId = createControl(0, L"STATIC", L"Group Filter", SS_LEFT, IDC_LABEL_QUICK_EDIT_GROUP_ID);
        m_editQuickEditGroupId = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL, IDC_EDIT_QUICK_EDIT_GROUP_ID);
        m_checkQuickEditGuidesOnly = createControl(0, L"BUTTON", L"Guides Only", BS_AUTOCHECKBOX, IDC_CHECK_QUICK_EDIT_GUIDES_ONLY);
        m_labelQuickEditVisualization = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_VISUALIZATION);
        m_btnQuickEditReplace = createControl(0, L"BUTTON", L"Apply to This Layer", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_REPLACE);
        m_btnQuickEditAppend = createControl(0, L"BUTTON", L"Add New Layer", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_APPEND);
        m_btnBrowsePartingMask = createControl(0, L"BUTTON", L"Browse Parting Mask...", BS_PUSHBUTTON, IDC_BTN_BROWSE_PARTING_MASK);
        m_btnBrowseRegionMask = createControl(0, L"BUTTON", L"Browse Region Mask...", BS_PUSHBUTTON, IDC_BTN_BROWSE_REGION_MASK);
        m_labelMaskSource = createControl(0, L"STATIC", L"Mask Source", SS_LEFT, IDC_LABEL_MASK_SOURCE);
        m_comboMaskSource = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_MASK_SOURCE);
        SendMessageW(m_comboMaskSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto"));
        SendMessageW(m_comboMaskSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"External Files"));
        SendMessageW(m_comboMaskSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Procedural"));
        SendMessageW(m_comboMaskSource, CB_SETCURSEL, 0, 0);
        m_labelMaskOverlay = createControl(0, L"STATIC", L"Scalp Overlay", SS_LEFT, IDC_LABEL_SELF_RADIUS + 100);
        m_comboMaskOverlay = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_MASK_OVERLAY);
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Parting Overlay"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Region Overlay"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Edit Influence"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Boundary Compare"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Selection / Risk"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Motion Score"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Contact Score"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Uncertainty Score"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Edit Score"));
        SendMessageW(m_comboMaskOverlay, CB_SETCURSEL, 0, 0);
        m_labelPartingMask = createControl(0, L"STATIC", L"Parting preview", SS_LEFT, IDC_LABEL_PARTING_MASK_NAME);
        m_previewPartingMask = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_BITMAP | SS_CENTERIMAGE | SS_NOTIFY, IDC_PREVIEW_PARTING_MASK);
        m_labelRegionMask = createControl(0, L"STATIC", L"Region preview", SS_LEFT, IDC_LABEL_REGION_MASK_NAME);
        m_previewRegionMask = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_BITMAP | SS_CENTERIMAGE | SS_NOTIFY, IDC_PREVIEW_REGION_MASK);

        createControl(0, L"STATIC", L"Guides", SS_LEFT, 0);
        m_editGuides = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, IDC_EDIT_GUIDES);
        createControl(0, L"STATIC", L"Followers / guide", SS_LEFT, 0);
        m_editFollowers = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, IDC_EDIT_FOLLOWERS);
        createControl(0, L"STATIC", L"Particles / strand", SS_LEFT, 0);
        m_editParticles = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, IDC_EDIT_PARTICLES);

        createControl(0, L"STATIC", L"Render", SS_LEFT, 0);
        m_comboRenderMode = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_RENDER_MODE);
        SendMessageW(m_comboRenderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Particle Point Sprite"));
        SendMessageW(m_comboRenderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Particle Billboard"));
        SendMessageW(m_comboRenderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Strand Ribbon"));
        SendMessageW(m_comboRenderMode, CB_SETCURSEL, 2, 0);

        createControl(0, L"STATIC", L"Interaction", SS_LEFT, 0);
        m_comboInteractionMode = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_INTERACTION_MODE);
        SendMessageW(m_comboInteractionMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"View Mode"));
        SendMessageW(m_comboInteractionMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Edit Mode"));
        SendMessageW(m_comboInteractionMode, CB_SETCURSEL, 1, 0);

        createControl(0, L"STATIC", L"Color Debug", SS_LEFT, 0);
        m_comboDebugColor = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_DEBUG_COLOR);
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Natural"));
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Guide / Follower"));
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Width Heatmap"));
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Editable / Locked"));
        SendMessageW(m_comboDebugColor, CB_SETCURSEL, 0, 0);

        m_checkSelfCollision = createControl(0, L"BUTTON", L"Self Collision", BS_AUTOCHECKBOX, IDC_CHECK_SELF_COLLISION);
        m_checkGrid = createControl(0, L"BUTTON", L"Density Grid", BS_AUTOCHECKBOX, IDC_CHECK_GRID);
        m_checkScalpMotion = createControl(0, L"BUTTON", L"Scalp Motion (disabled)", BS_AUTOCHECKBOX, IDC_CHECK_SCALP_MOTION);
        m_checkShowColliders = createControl(0, L"BUTTON", L"Show Colliders", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_COLLIDERS);
        m_checkShowScalpMesh = createControl(0, L"BUTTON", L"Show Scalp Mesh", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_SCALP_MESH);
        m_checkShowScalp = createControl(0, L"BUTTON", L"Show Scalp Wireframe", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_SCALP);
        m_checkShowGridBounds = createControl(0, L"BUTTON", L"Show Grid Bounds", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_GRID_BOUNDS);
        m_checkShowFloor = createControl(0, L"BUTTON", L"Show Floor Guide", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_FLOOR);
        m_checkShowGuides = createControl(0, L"BUTTON", L"Show Guide Skeletons", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_GUIDES);
        m_checkShowFollowers = createControl(0, L"BUTTON", L"Show Follower Skeletons", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_FOLLOWERS);
        m_checkShowRoots = createControl(0, L"BUTTON", L"Show Root Markers", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_ROOTS);
        m_checkShowLinks = createControl(0, L"BUTTON", L"Show Follow Links", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_LINKS);

        auto createSlider = [&](int id) -> HWND
        {
            HWND hwnd = createControl(0, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_HORZ, id);
            SendMessageW(hwnd, TBM_SETRANGEMIN, FALSE, 0);
            SendMessageW(hwnd, TBM_SETRANGEMAX, TRUE, 1000);
            return hwnd;
        };

        m_labelScalpCoverageStrength = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SCALP_COVERAGE_STRENGTH);
        m_trackScalpCoverageStrength = createSlider(IDC_TRACK_SCALP_COVERAGE_STRENGTH);
        m_labelScalpPartWidth = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SCALP_PART_WIDTH);
        m_trackScalpPartWidth = createSlider(IDC_TRACK_SCALP_PART_WIDTH);
        m_labelScalpDarkness = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SCALP_DARKNESS);
        m_trackScalpDarkness = createSlider(IDC_TRACK_SCALP_DARKNESS);
        m_labelScalpHairBlend = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SCALP_HAIR_BLEND);
        m_trackScalpHairBlend = createSlider(IDC_TRACK_SCALP_HAIR_BLEND);

        m_trackQuickEditFeather = createSlider(IDC_TRACK_QUICK_EDIT_FEATHER);
        m_trackQuickEditLengthScale = createSlider(IDC_TRACK_QUICK_EDIT_LENGTHSCALE);
        m_trackQuickEditLayerWeight = createSlider(IDC_TRACK_QUICK_EDIT_LAYERWEIGHT);
        m_trackQuickEditCenterU = createSlider(IDC_TRACK_QUICK_EDIT_CENTER_U);
        m_trackQuickEditCenterV = createSlider(IDC_TRACK_QUICK_EDIT_CENTER_V);
        m_trackQuickEditRadiusU = createSlider(IDC_TRACK_QUICK_EDIT_RADIUS_U);
        m_trackQuickEditRadiusV = createSlider(IDC_TRACK_QUICK_EDIT_RADIUS_V);
        m_trackQuickEditInnerRadius = createSlider(IDC_TRACK_QUICK_EDIT_INNER_RADIUS);
        m_trackQuickEditAngleCenter = createSlider(IDC_TRACK_QUICK_EDIT_ANGLE_CENTER);
        m_trackQuickEditAngleHalfExtent = createSlider(IDC_TRACK_QUICK_EDIT_ANGLE_HALF_EXTENT);
        m_trackQuickEditRootOffsetX = createSlider(IDC_TRACK_QUICK_EDIT_ROOT_OFFSET_X);
        m_trackQuickEditRootOffsetY = createSlider(IDC_TRACK_QUICK_EDIT_ROOT_OFFSET_Y);
        m_trackQuickEditRootOffsetZ = createSlider(IDC_TRACK_QUICK_EDIT_ROOT_OFFSET_Z);
        m_trackQuickEditTipOffsetX = createSlider(IDC_TRACK_QUICK_EDIT_TIP_OFFSET_X);
        m_trackQuickEditTipOffsetY = createSlider(IDC_TRACK_QUICK_EDIT_TIP_OFFSET_Y);
        m_trackQuickEditTipOffsetZ = createSlider(IDC_TRACK_QUICK_EDIT_TIP_OFFSET_Z);
        m_trackQuickEditSideOffset = createSlider(IDC_TRACK_QUICK_EDIT_SIDE_OFFSET);
        m_trackQuickEditYaw = createSlider(IDC_TRACK_QUICK_EDIT_YAW);
        m_trackQuickEditWidthScale = createSlider(IDC_TRACK_QUICK_EDIT_WIDTHSCALE);
        m_trackQuickEditFollowerInfluence = createSlider(IDC_TRACK_QUICK_EDIT_FOLLOWER_INFLUENCE);
        m_trackQuickEditBoundarySmoothing = createSlider(IDC_TRACK_QUICK_EDIT_BOUNDARY_SMOOTHING);
        m_trackQuickEditBoundaryRadiusUv = createSlider(IDC_TRACK_QUICK_EDIT_BOUNDARY_RADIUS_UV);
        m_labelWidth = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_WIDTH);
        m_trackWidth = createSlider(IDC_TRACK_WIDTH);
        m_labelGravity = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_GRAVITY);
        m_trackGravity = createSlider(IDC_TRACK_GRAVITY);
        m_labelDamping = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_DAMPING);
        m_trackDamping = createSlider(IDC_TRACK_DAMPING);
        m_labelWind = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_WIND);
        m_trackWind = createSlider(IDC_TRACK_WIND);
        m_labelIterations = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ITERATIONS);
        m_trackIterations = createSlider(IDC_TRACK_ITERATIONS);
        m_labelFollow = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_FOLLOW);
        m_trackFollow = createSlider(IDC_TRACK_FOLLOW);
        m_labelSelfRadius = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SELF_RADIUS);
        m_trackSelfRadius = createSlider(IDC_TRACK_SELF_RADIUS);
        m_labelSelfStiffness = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SELF_STIFFNESS);
        m_trackSelfStiffness = createSlider(IDC_TRACK_SELF_STIFFNESS);
        m_labelGridCellSize = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_GRID_CELL);
        m_trackGridCellSize = createSlider(IDC_TRACK_GRID_CELL);
        m_labelStretchCompliance = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_STRETCH);
        m_trackStretchCompliance = createSlider(IDC_TRACK_STRETCH);
        m_labelBendCompliance = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_BEND);
        m_trackBendCompliance = createSlider(IDC_TRACK_BEND);
        m_labelActiveSetTuning = createControl(0, L"STATIC", L"Active-Set Runtime Tuning", SS_LEFT, IDC_LABEL_ACTIVESET_TUNING);
        m_checkActiveSetAdaptiveBudget = createControl(0, L"BUTTON", L"Adaptive Budget", BS_AUTOCHECKBOX, IDC_CHECK_ACTIVESET_ADAPTIVE_BUDGET);
        m_checkActiveSetHardClamp = createControl(0, L"BUTTON", L"Hard Budget Clamp", BS_AUTOCHECKBOX, IDC_CHECK_ACTIVESET_HARD_CLAMP);
        m_labelActiveSetMaxClusters = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_MAX_CLUSTERS);
        m_trackActiveSetMaxClusters = createSlider(IDC_TRACK_ACTIVESET_MAX_CLUSTERS);
        m_labelActiveSetMaxGuides = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_MAX_GUIDES);
        m_trackActiveSetMaxGuides = createSlider(IDC_TRACK_ACTIVESET_MAX_GUIDES);
        m_labelActiveSetThreshold = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_THRESHOLD);
        m_trackActiveSetThreshold = createSlider(IDC_TRACK_ACTIVESET_THRESHOLD);
        m_labelActiveSetTargetFrame = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_TARGET_FRAME);
        m_trackActiveSetTargetFrame = createSlider(IDC_TRACK_ACTIVESET_TARGET_FRAME);
        m_labelActiveSetFixedCost = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_FIXED_COST);
        m_trackActiveSetFixedCost = createSlider(IDC_TRACK_ACTIVESET_FIXED_COST);
        m_labelActiveSetClusterCost = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_CLUSTER_COST);
        m_trackActiveSetClusterCost = createSlider(IDC_TRACK_ACTIVESET_CLUSTER_COST);
        m_labelActiveSetScoreWeights = createControl(0, L"STATIC", L"Active-Set Score Weights", SS_LEFT, IDC_LABEL_ACTIVESET_SCORE_WEIGHTS);
        m_labelActiveSetMotionWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_MOTION_WEIGHT);
        m_trackActiveSetMotionWeight = createSlider(IDC_TRACK_ACTIVESET_MOTION_WEIGHT);
        m_labelActiveSetContactWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_CONTACT_WEIGHT);
        m_trackActiveSetContactWeight = createSlider(IDC_TRACK_ACTIVESET_CONTACT_WEIGHT);
        m_labelActiveSetUncertaintyWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_UNCERTAINTY_WEIGHT);
        m_trackActiveSetUncertaintyWeight = createSlider(IDC_TRACK_ACTIVESET_UNCERTAINTY_WEIGHT);
        m_labelActiveSetEditWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_EDIT_WEIGHT);
        m_trackActiveSetEditWeight = createSlider(IDC_TRACK_ACTIVESET_EDIT_WEIGHT);
        m_btnExportActiveSetJson = createControl(0, L"BUTTON", L"Export Active-Set JSON", BS_PUSHBUTTON, IDC_BTN_EXPORT_ACTIVESET_JSON);
        m_btnExportRuntimeReport = createControl(0, L"BUTTON", L"Export Runtime Report", BS_PUSHBUTTON, IDC_BTN_EXPORT_RUNTIME_REPORT);

        m_labelStats = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_STATS);
        m_labelAssetStats = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ASSET_STATS);
        m_labelEditWorkflow = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_LEFT, 0);
        m_labelEditLegend = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_LEFT, 0);

        m_btnWorkspaceGroom = createControl(0, L"BUTTON", L"Groom", BS_PUSHBUTTON, IDC_BTN_WORKSPACE_GROOM);
        m_btnWorkspaceSim = createControl(0, L"BUTTON", L"Sim", BS_PUSHBUTTON, IDC_BTN_WORKSPACE_SIM);
        m_btnWorkspaceReview = createControl(0, L"BUTTON", L"Review", BS_PUSHBUTTON, IDC_BTN_WORKSPACE_REVIEW);
        m_btnWorkspaceReport = createControl(0, L"BUTTON", L"Report", BS_PUSHBUTTON, IDC_BTN_WORKSPACE_REPORT);
        m_btnToolSelect = createControl(0, L"BUTTON", L"Select", BS_PUSHBUTTON, IDC_BTN_TOOL_SELECT);
        m_btnToolComb = createControl(0, L"BUTTON", L"Comb", BS_PUSHBUTTON, IDC_BTN_TOOL_COMB);
        m_btnToolGrab = createControl(0, L"BUTTON", L"Grab", BS_PUSHBUTTON, IDC_BTN_TOOL_GRAB);
        m_btnToolSmooth = createControl(0, L"BUTTON", L"Smooth", BS_PUSHBUTTON, IDC_BTN_TOOL_SMOOTH);
        m_btnToolLength = createControl(0, L"BUTTON", L"Length", BS_PUSHBUTTON, IDC_BTN_TOOL_LENGTH);
        m_btnToolVolume = createControl(0, L"BUTTON", L"Volume", BS_PUSHBUTTON, IDC_BTN_TOOL_VOLUME);
        m_btnToolPart = createControl(0, L"BUTTON", L"Part", BS_PUSHBUTTON, IDC_BTN_TOOL_PART);
        m_btnToolFreeze = createControl(0, L"BUTTON", L"Freeze", BS_PUSHBUTTON, IDC_BTN_TOOL_FREEZE);
        m_btnToolStrandFix = createControl(0, L"BUTTON", L"Strand Fix", BS_PUSHBUTTON, IDC_BTN_TOOL_STRAND_FIX);
        m_btnUndoEdit = createControl(0, L"BUTTON", L"Undo", BS_PUSHBUTTON, IDC_BTN_UNDO_EDIT);
        m_btnRedoEdit = createControl(0, L"BUTTON", L"Redo", BS_PUSHBUTTON, IDC_BTN_REDO_EDIT);
        m_btnTrayLayers = createControl(0, L"BUTTON", L"Layers", BS_PUSHBUTTON, IDC_BTN_TRAY_LAYERS);
        m_btnTrayHistory = createControl(0, L"BUTTON", L"History", BS_PUSHBUTTON, IDC_BTN_TRAY_HISTORY);
        m_btnTrayMetrics = createControl(0, L"BUTTON", L"Metrics", BS_PUSHBUTTON, IDC_BTN_TRAY_METRICS);
        m_btnToggleInspector = createControl(0, L"BUTTON", L"Inspector", BS_PUSHBUTTON, IDC_BTN_TOGGLE_INSPECTOR);
        m_btnToggleTray = createControl(0, L"BUTTON", L"Layers", BS_PUSHBUTTON, IDC_BTN_TOGGLE_TRAY);
        m_btnToggleAdvanced = createControl(0, L"BUTTON", L"Advanced", BS_PUSHBUTTON, IDC_BTN_TOGGLE_ADVANCED);
        m_btnFastViewport = createControl(0, L"BUTTON", L"Fast View", BS_PUSHBUTTON, IDC_BTN_FAST_VIEWPORT);
        m_labelWorkspaceTitle = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_WORKSPACE_TITLE);
        m_labelInspectorTitle = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_INSPECTOR_TITLE);
        m_labelTrayTitle = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_TRAY_TITLE);
        m_labelToolHint = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_LEFT, IDC_LABEL_TOOL_HINT);
        m_labelSessionStatus = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SESSION_STATUS);

        return m_comboPreset && m_comboRenderMode && m_comboInteractionMode && m_comboDebugColor && m_comboMaskSource && m_comboMaskOverlay && m_previewPartingMask && m_previewRegionMask && m_btnPlayPause && m_btnStep && m_btnImportAsset && m_btnBrowseEditStack && m_btnClearEditStack && m_labelEditStack && m_labelInlineEditStack && m_editInlineEditStack && m_btnApplyInlineEditStack && m_btnSaveInlineEditStack && m_btnResetInlineEditStack && m_labelQuickEditForm && m_labelLayerList && m_listInlineLayers && m_labelQuickEditName && m_editQuickEditName && m_btnQuickEditMoveUp && m_btnQuickEditMoveDown && m_checkQuickEditEnabled && m_btnQuickEditDelete && m_btnQuickEditDuplicate && m_comboQuickEditMode && m_trackQuickEditFeather && m_trackQuickEditLengthScale && m_trackQuickEditLayerWeight && m_trackQuickEditCenterU && m_trackQuickEditCenterV && m_trackQuickEditRadiusU && m_trackQuickEditRadiusV && m_trackQuickEditInnerRadius && m_trackQuickEditAngleCenter && m_trackQuickEditAngleHalfExtent && m_trackQuickEditRootOffsetX && m_trackQuickEditRootOffsetY && m_trackQuickEditRootOffsetZ && m_trackQuickEditTipOffsetX && m_trackQuickEditTipOffsetY && m_trackQuickEditTipOffsetZ && m_trackQuickEditSideOffset && m_trackQuickEditYaw && m_trackQuickEditWidthScale && m_trackQuickEditFollowerInfluence && m_trackQuickEditBoundarySmoothing && m_trackQuickEditBoundaryRadiusUv && m_editQuickEditGroupId && m_checkQuickEditGuidesOnly && m_labelQuickEditLayerWeight && m_labelQuickEditCenterU && m_labelQuickEditCenterV && m_labelQuickEditRadiusU && m_labelQuickEditRadiusV && m_labelQuickEditInnerRadius && m_labelQuickEditAngleCenter && m_labelQuickEditAngleHalfExtent && m_labelQuickEditRootOffsetX && m_labelQuickEditRootOffsetY && m_labelQuickEditRootOffsetZ && m_labelQuickEditTipOffsetX && m_labelQuickEditTipOffsetY && m_labelQuickEditTipOffsetZ && m_labelQuickEditSideOffset && m_labelQuickEditYaw && m_labelQuickEditWidthScale && m_labelQuickEditFollowerInfluence && m_labelQuickEditBoundarySmoothing && m_labelQuickEditBoundaryRadiusUv && m_labelQuickEditGroupId && m_labelQuickEditVisualization && m_btnQuickEditReplace && m_btnQuickEditAppend && m_trackWidth && m_labelActiveSetTuning && m_checkActiveSetAdaptiveBudget && m_checkActiveSetHardClamp && m_labelActiveSetMaxClusters && m_trackActiveSetMaxClusters && m_labelActiveSetMaxGuides && m_trackActiveSetMaxGuides && m_labelActiveSetThreshold && m_trackActiveSetThreshold && m_labelActiveSetTargetFrame && m_trackActiveSetTargetFrame && m_labelActiveSetFixedCost && m_trackActiveSetFixedCost && m_labelActiveSetClusterCost && m_trackActiveSetClusterCost && m_labelActiveSetScoreWeights && m_labelActiveSetMotionWeight && m_trackActiveSetMotionWeight && m_labelActiveSetContactWeight && m_trackActiveSetContactWeight && m_labelActiveSetUncertaintyWeight && m_trackActiveSetUncertaintyWeight && m_labelActiveSetEditWeight && m_trackActiveSetEditWeight && m_btnExportActiveSetJson && m_btnExportRuntimeReport && m_labelStats && m_labelAssetStats && m_labelEditWorkflow && m_labelEditLegend && m_checkShowScalpMesh && m_labelScalpCoverageStrength && m_trackScalpCoverageStrength && m_labelScalpPartWidth && m_trackScalpPartWidth && m_labelScalpDarkness && m_trackScalpDarkness && m_labelScalpHairBlend && m_trackScalpHairBlend && m_btnWorkspaceGroom && m_btnWorkspaceSim && m_btnWorkspaceReview && m_btnWorkspaceReport && m_btnToolSelect && m_btnToolComb && m_btnToolGrab && m_btnToolSmooth && m_btnToolLength && m_btnToolVolume && m_btnToolPart && m_btnToolFreeze && m_btnToolStrandFix && m_btnUndoEdit && m_btnRedoEdit && m_btnTrayLayers && m_btnTrayHistory && m_btnTrayMetrics && m_btnToggleInspector && m_btnToggleTray && m_btnToggleAdvanced && m_btnFastViewport && m_labelWorkspaceTitle && m_labelInspectorTitle && m_labelTrayTitle && m_labelToolHint && m_labelSessionStatus;
    }


    void GpuHairViewer::ApplyWorkspaceMode(WorkspaceMode workspace)
    {
        m_workspaceMode = workspace;
        switch (workspace)
        {
        case WorkspaceMode::Groom:
            m_interactionMode = InteractionMode::Edit;
            if (m_debugColorMode == DebugColorMode::Natural)
            {
                m_debugColorMode = DebugColorMode::Editability;
            }
            if (m_maskOverlayMode == MaskOverlayMode::None)
            {
                m_maskOverlayMode = MaskOverlayMode::EditInfluence;
            }
            break;
        case WorkspaceMode::Sim:
            m_interactionMode = InteractionMode::View;
            if (m_maskOverlayMode == MaskOverlayMode::EditInfluenceBeforeAfter)
            {
                m_maskOverlayMode = MaskOverlayMode::None;
            }
            break;
        case WorkspaceMode::Review:
            m_interactionMode = InteractionMode::View;
            if (m_maskOverlayMode == MaskOverlayMode::None)
            {
                m_maskOverlayMode = MaskOverlayMode::EditInfluenceBeforeAfter;
            }
            break;
        case WorkspaceMode::Report:
            m_interactionMode = InteractionMode::View;
            if (m_maskOverlayMode == MaskOverlayMode::None)
            {
                m_maskOverlayMode = MaskOverlayMode::ActiveSetRiskSelection;
            }
            break;
        }
        SyncControlsFromState();
        LayoutUiControls();
    }

    void GpuHairViewer::ApplyGroomToolPreset(GroomTool tool)
    {
        m_groomTool = tool;
        m_workspaceMode = WorkspaceMode::Groom;
        m_interactionMode = InteractionMode::Edit;
        switch (tool)
        {
        case GroomTool::Select:
            m_maskOverlayMode = MaskOverlayMode::EditInfluence;
            break;
        case GroomTool::Comb:
            m_quickEditMode = hairgpu::RegionSelectMode::Ellipse;
            m_quickEditFeather = std::max(m_quickEditFeather, 0.20f);
            m_maskOverlayMode = MaskOverlayMode::EditInfluence;
            break;
        case GroomTool::Grab:
            m_quickEditMode = hairgpu::RegionSelectMode::Ellipse;
            m_maskOverlayMode = MaskOverlayMode::EditInfluence;
            break;
        case GroomTool::Smooth:
            m_quickEditBoundarySmoothing = std::max(m_quickEditBoundarySmoothing, 0.45f);
            m_maskOverlayMode = MaskOverlayMode::EditInfluenceBeforeAfter;
            break;
        case GroomTool::Length:
            m_maskOverlayMode = MaskOverlayMode::EditInfluence;
            break;
        case GroomTool::Volume:
            m_maskOverlayMode = MaskOverlayMode::EditInfluence;
            break;
        case GroomTool::Part:
            m_quickEditMode = hairgpu::RegionSelectMode::Sector;
            m_maskOverlayMode = MaskOverlayMode::Parting;
            break;
        case GroomTool::Freeze:
            m_maskOverlayMode = MaskOverlayMode::EditInfluence;
            break;
        case GroomTool::StrandFix:
            m_maskOverlayMode = MaskOverlayMode::EditInfluence;
            break;
        case GroomTool::Clump:
            m_quickEditMode = hairgpu::RegionSelectMode::Ellipse;
            m_quickEditFeather = std::max(m_quickEditFeather, 0.24f);
            m_maskOverlayMode = MaskOverlayMode::EditInfluenceBeforeAfter;
            break;
        case GroomTool::Curl:
            m_quickEditMode = hairgpu::RegionSelectMode::Ellipse;
            m_quickEditFeather = std::max(m_quickEditFeather, 0.26f);
            m_maskOverlayMode = MaskOverlayMode::EditInfluenceBeforeAfter;
            break;
        case GroomTool::Unclump:
            m_quickEditMode = hairgpu::RegionSelectMode::Ellipse;
            m_maskOverlayMode = MaskOverlayMode::EditInfluenceBeforeAfter;
            break;
        }
        SyncControlsFromState();
        LayoutUiControls();
    }

    const wchar_t* GpuHairViewer::GetWorkspaceDisplayName() const
    {
        switch (m_workspaceMode)
        {
        case WorkspaceMode::Groom: return L"Groom";
        case WorkspaceMode::Sim: return L"Sim";
        case WorkspaceMode::Review: return L"Review";
        case WorkspaceMode::Report: return L"Report";
        default: return L"Groom";
        }
    }

