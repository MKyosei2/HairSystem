// GpuHairViewer.cpp ordered part 02a: required_runtime_foundation
// This file restores foundational definitions that were lost when the original
// monolithic Viewer source was split into ordered INL parts.
// It is included by ../GpuHairViewer.cpp; do not compile it directly.

    bool GpuHairViewer::CreateUiControls()
    {
        // Formal measurement must use the full requested 1920 x 1080 render area.
        // Omitting the editor panel also prevents UI layout and paint work from
        // contaminating performance evidence.
        if (IsExperimentModeEnabled())
        {
            m_uiPanel = nullptr;
            return true;
        }

        const HINSTANCE instance = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE));
        m_uiPanel = CreateWindowExW(
            WS_EX_CONTROLPARENT,
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            kUiPanelWidth,
            1,
            m_hwnd,
            nullptr,
            instance,
            nullptr);

        // The interactive editor can still render when a restrictive desktop
        // policy blocks child-window creation. All control synchronization paths
        // already tolerate null HWND values, so retain a usable viewport.
        return true;
    }

    void GpuHairViewer::ApplyWorkspaceMode(WorkspaceMode workspace)
    {
        m_workspaceMode = workspace;
        m_interactionMode =
            workspace == WorkspaceMode::Groom
            ? InteractionMode::Edit
            : InteractionMode::View;
        SyncControlsFromState();
        LayoutUiControls();
        UpdateStatsLabel();
    }

    void GpuHairViewer::ApplyGroomToolPreset(GroomTool tool)
    {
        m_workspaceMode = WorkspaceMode::Groom;
        m_interactionMode = InteractionMode::Edit;
        m_groomTool = tool;

        switch (tool)
        {
        case GroomTool::Grab:
        case GroomTool::StrandFix:
            m_strandGizmoMode = StrandGizmoMode::TipTranslate;
            break;
        case GroomTool::Select:
        case GroomTool::Comb:
        case GroomTool::Smooth:
        case GroomTool::Length:
        case GroomTool::Volume:
        case GroomTool::Part:
        case GroomTool::Freeze:
        case GroomTool::Clump:
        case GroomTool::Curl:
        case GroomTool::Unclump:
        default:
            break;
        }

        SyncControlsFromState();
        LayoutUiControls();
        RefreshQuickEditFormLabels();
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

    RECT GpuHairViewer::GetRenderViewportRect() const
    {
        RECT viewport = {};
        if (!m_hwnd || !GetClientRect(m_hwnd, &viewport))
        {
            viewport.right = static_cast<LONG>(m_width);
            viewport.bottom = static_cast<LONG>(m_height);
            return viewport;
        }

        if (!IsExperimentModeEnabled() &&
            m_uiPanel &&
            IsWindowVisible(m_uiPanel))
        {
            viewport.right = (std::max)(
                viewport.left,
                viewport.right - static_cast<LONG>(kUiPanelWidth));
        }
        return viewport;
    }

    bool GpuHairViewer::CompileShaderFromFile(
        const wchar_t* filePath,
        const char* entryPoint,
        const char* target,
        ID3DBlob** blobOut,
        std::wstring& errorText)
    {
        if (!filePath || !entryPoint || !target || !blobOut)
        {
            errorText = L"Invalid shader compilation request.";
            return false;
        }
        *blobOut = nullptr;

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS |
                     D3DCOMPILE_OPTIMIZATION_LEVEL3;
    #if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG;
    #endif

        wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        const std::filesystem::path exeDirectory =
            std::filesystem::path(modulePath).parent_path();
        const std::filesystem::path requested(filePath);

        std::vector<std::filesystem::path> candidates;
        auto addCandidate = [&candidates](const std::filesystem::path& candidate)
        {
            if (candidate.empty())
            {
                return;
            }
            const std::filesystem::path normalized =
                candidate.lexically_normal();
            if (std::find(candidates.begin(), candidates.end(), normalized) ==
                candidates.end())
            {
                candidates.push_back(normalized);
            }
        };

        if (requested.is_absolute())
        {
            addCandidate(requested);
        }
        else
        {
            addCandidate(requested);
            addCandidate(std::filesystem::path(HAIRGPU_VIEWER_SHADER_DIR) / requested);
            addCandidate(exeDirectory / requested);
            addCandidate(exeDirectory / L"shaders" / requested);
            addCandidate(std::filesystem::current_path() / requested);
            addCandidate(std::filesystem::current_path() / L"shaders" / requested);

            std::filesystem::path root = exeDirectory;
            for (int depth = 0; depth < 7; ++depth)
            {
                addCandidate(root / L"apps" / L"dx11_viewer" / L"shaders" / requested);
                addCandidate(root / L"dx11_viewer" / L"shaders" / requested);
                if (!root.has_parent_path())
                {
                    break;
                }
                const std::filesystem::path parent = root.parent_path();
                if (parent == root)
                {
                    break;
                }
                root = parent;
            }
        }

        HRESULT lastResult = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;
        std::wstringstream attemptedPaths;
        std::filesystem::path compiledPath;

        for (const std::filesystem::path& candidate : candidates)
        {
            const bool exists = std::filesystem::is_regular_file(candidate);
            attemptedPaths << candidate.wstring()
                           << (exists ? L"  [exists]" : L"  [missing]")
                           << L"\r\n";
            if (!exists)
            {
                continue;
            }

            shaderBlob.Reset();
            errorBlob.Reset();
            lastResult = D3DCompileFromFile(
                candidate.c_str(),
                nullptr,
                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entryPoint,
                target,
                flags,
                0u,
                &shaderBlob,
                &errorBlob);
            compiledPath = candidate;
            if (SUCCEEDED(lastResult))
            {
                break;
            }
        }

        if (FAILED(lastResult) || !shaderBlob)
        {
            std::wstringstream message;
            message << L"Shader compilation failed. HRESULT=0x"
                    << std::hex << std::uppercase
                    << static_cast<unsigned long>(lastResult)
                    << L"\r\nFile: " << filePath
                    << L"\r\nEntry: " << entryPoint
                    << L"\r\nTarget: " << target;
            if (!compiledPath.empty())
            {
                message << L"\r\nResolved path: "
                        << compiledPath.wstring();
            }
            if (errorBlob && errorBlob->GetBufferPointer())
            {
                const std::string compilerMessage(
                    static_cast<const char*>(errorBlob->GetBufferPointer()),
                    errorBlob->GetBufferSize());
                message << L"\r\n\r\nCompiler output:\r\n"
                        << std::wstring(
                            compilerMessage.begin(),
                            compilerMessage.end());
            }
            message << L"\r\n\r\nTried paths:\r\n"
                    << attemptedPaths.str();
            errorText = message.str();
            return false;
        }

        *blobOut = shaderBlob.Detach();
        errorText.clear();
        return true;
    }

    GpuHairViewer::StructuredBuffer GpuHairViewer::CreateStructuredBuffer(
        UINT elementSize,
        UINT elementCount,
        const void* initialData,
        bool unorderedAccess)
    {
        StructuredBuffer result;
        if (!m_device || elementSize == 0u)
        {
            return result;
        }

        const UINT safeElementCount = (std::max)(elementCount, 1u);
        const uint64_t byteWidth64 =
            static_cast<uint64_t>(elementSize) * safeElementCount;
        if (byteWidth64 == 0u ||
            byteWidth64 > static_cast<uint64_t>((std::numeric_limits<UINT>::max)()))
        {
            return result;
        }

        D3D11_BUFFER_DESC bufferDescription = {};
        bufferDescription.ByteWidth = static_cast<UINT>(byteWidth64);
        bufferDescription.Usage = D3D11_USAGE_DEFAULT;
        bufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
            (unorderedAccess ? D3D11_BIND_UNORDERED_ACCESS : 0u);
        bufferDescription.CPUAccessFlags = 0u;
        bufferDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDescription.StructureByteStride = elementSize;

        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = initialData;
        const D3D11_SUBRESOURCE_DATA* initialPointer =
            initialData ? &initial : nullptr;
        if (FAILED(m_device->CreateBuffer(
                &bufferDescription,
                initialPointer,
                &result.buffer)))
        {
            return {};
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription = {};
        srvDescription.Format = DXGI_FORMAT_UNKNOWN;
        srvDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDescription.Buffer.FirstElement = 0u;
        srvDescription.Buffer.NumElements = safeElementCount;
        if (FAILED(m_device->CreateShaderResourceView(
                result.buffer.Get(),
                &srvDescription,
                &result.srv)))
        {
            return {};
        }

        if (unorderedAccess)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDescription = {};
            uavDescription.Format = DXGI_FORMAT_UNKNOWN;
            uavDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDescription.Buffer.FirstElement = 0u;
            uavDescription.Buffer.NumElements = safeElementCount;
            uavDescription.Buffer.Flags = 0u;
            if (FAILED(m_device->CreateUnorderedAccessView(
                    result.buffer.Get(),
                    &uavDescription,
                    &result.uav)))
            {
                return {};
            }
        }

        return result;
    }
