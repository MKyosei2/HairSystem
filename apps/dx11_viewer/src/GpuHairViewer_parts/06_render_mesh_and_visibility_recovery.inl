// GpuHairViewer.cpp ordered part 06: render_mesh_and_visibility_recovery
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.
// P0_INSTANCED_RUNTIME: render tail contains no full-particle CPU readback path.

        // GPU debug overlays are excluded from formal measurement.
        // They are auxiliary UI diagnostics and must not affect formal timing.
const bool allowGpuDebugLines =
!IsExperimentModeEnabled() &&
m_context &&
m_currentBufferIndex < 2u &&
    m_debugLineTemplateVertexBuffer &&
    m_debugInputLayout &&
    m_debugLineVS &&
    m_debugLinePS;

if (allowGpuDebugLines && m_gpuDebugLinesDirty)
{
    RebuildGpuDebugLineEntries();
}

if (allowGpuDebugLines &&
    !m_gpuDebugLineEntries.empty() &&
    EnsureGpuDebugLineCapacity(m_gpuDebugLineEntries.size()) &&
    m_debugLineEntriesBuffer.buffer &&
    m_debugLineEntriesBuffer.srv &&
    m_positions[m_currentBufferIndex].srv &&
    m_scalpPositions.srv)
{
    constexpr size_t debugEntrySize =
        sizeof(GpuDebugLineEntry);

    const size_t debugEntryCount =
        m_gpuDebugLineEntries.size();

    // Prevent size_t multiplication overflow and UINT truncation.
    const bool uploadSizeValid =
        debugEntryCount <=
        static_cast<size_t>(
            (std::numeric_limits<UINT>::max)()) /
        debugEntrySize;

    if (uploadSizeValid)
    {
        const UINT uploadBytes =
            static_cast<UINT>(
                debugEntryCount * debugEntrySize);

        D3D11_BUFFER_DESC debugBufferDesc = {};
        m_debugLineEntriesBuffer.buffer->GetDesc(
            &debugBufferDesc);

        if (uploadBytes > 0u &&
            uploadBytes <= debugBufferDesc.ByteWidth)
        {
            // Explicitly remove the entry buffer from the previous
            // frame's VS SRV slot before updating it.
            ID3D11ShaderResourceView* nullEntrySrv = nullptr;
            m_context->VSSetShaderResources(
                2u,
                1u,
                &nullEntrySrv);

            // For a D3D11 buffer, left/right specify byte offsets.
            // Update only the valid CPU vector range rather than
            // the entire capacity-sized GPU buffer.
            D3D11_BOX uploadBox = {};
            uploadBox.left = 0u;
            uploadBox.right = uploadBytes;
            uploadBox.top = 0u;
            uploadBox.bottom = 1u;
            uploadBox.front = 0u;
            uploadBox.back = 1u;

            m_context->UpdateSubresource(
                m_debugLineEntriesBuffer.buffer.Get(),
                0u,
                &uploadBox,
                m_gpuDebugLineEntries.data(),
                0u,
                0u);

            const UINT debugStride =
                sizeof(DebugLineTemplateVertex);
            const UINT debugOffset = 0u;

            ID3D11Buffer* debugBuffer =
                m_debugLineTemplateVertexBuffer.Get();

            ID3D11ShaderResourceView* debugSrvs[] = {
                m_positions[m_currentBufferIndex].srv.Get(),
                m_scalpPositions.srv.Get(),
                m_debugLineEntriesBuffer.srv.Get()
            };

            m_context->IASetInputLayout(
                m_debugInputLayout.Get());
            m_context->IASetVertexBuffers(
                0u,
                1u,
                &debugBuffer,
                &debugStride,
                &debugOffset);
            m_context->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            m_context->VSSetShader(
                m_debugLineVS.Get(),
                nullptr,
                0u);
            m_context->GSSetShader(
                nullptr,
                nullptr,
                0u);
            m_context->PSSetShader(
                m_debugLinePS.Get(),
                nullptr,
                0u);
            m_context->VSSetConstantBuffers(
                0u,
                1u,
                &renderCb);
            m_context->PSSetConstantBuffers(
                0u,
                1u,
                &renderCb);
            m_context->VSSetShaderResources(
                0u,
                3u,
                debugSrvs);
            m_context->DrawInstanced(
                2u,
                static_cast<UINT>(debugEntryCount),
                0u,
                0u);

            ID3D11ShaderResourceView*
                nullDebugSrvs[3] = {};
            m_context->VSSetShaderResources(
                0u,
                3u,
                nullDebugSrvs);

            ++m_lastTotalDrawCalls;
        }
        else
        {
            // Do not issue a driver call with an invalid upload size.
            m_gpuDebugLinesDirty = true;
        }
    }
    else
    {
        // The entry count cannot be represented safely by D3D11.
        m_gpuDebugLinesDirty = true;
    }
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

// HAIRGPU_EXPERIMENT_RUNTIME_PATCH_V1: observed Present configuration is evidence, not an assumption.
const UINT presentSyncInterval = m_vsyncEnabled
? std::clamp<UINT>(m_presentSyncInterval, 1u, 4u)
    : 0u;
m_lastObservedPresentSyncInterval = presentSyncInterval;
m_lastPresentResult = m_swapChain->Present(presentSyncInterval, 0u);
if (FAILED(m_lastPresentResult))
{
    m_noveltyTelemetry.fatalFailure = 1u;
    m_noveltyTelemetry.fatalFailureCode = static_cast<uint32_t>(m_lastPresentResult);
    ++m_noveltyTelemetry.fatalRuntimeFailureCount;
    m_running = false;
}
// Window title is refreshed by Run() at the UI statistics cadence.
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

    bool GpuHairViewer::CompileShaderFromFile(
        const wchar_t* filePath,
        const char* entryPoint,
        const char* target,
        ID3DBlob** blobOut,
        std::wstring& errorText)
    {
        if (!filePath || !entryPoint || !target || !blobOut)
        {
            errorText = L"CompileShaderFromFile received an invalid argument.";
            return false;
        }

        *blobOut = nullptr;
        errorText.clear();

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    #if defined(_DEBUG)
        flags &= ~D3DCOMPILE_OPTIMIZATION_LEVEL3;
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #endif

        wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        const std::filesystem::path exeDir =
            std::filesystem::path(modulePath).parent_path();

        std::vector<std::filesystem::path> candidates;
        auto pushCandidate = [&candidates](const std::filesystem::path& value)
        {
            if (value.empty()) return;
            const std::filesystem::path normalized = value.lexically_normal();
            if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end())
                candidates.push_back(normalized);
        };

        pushCandidate(std::filesystem::path(filePath));
        pushCandidate(std::filesystem::path(HAIRGPU_VIEWER_SHADER_DIR) / filePath);
        pushCandidate(exeDir / filePath);
        pushCandidate(exeDir / L"shaders" / filePath);
        pushCandidate(std::filesystem::current_path() / filePath);
        pushCandidate(std::filesystem::current_path() / L"shaders" / filePath);

        std::filesystem::path searchRoot = exeDir;
        for (int i = 0; i < 8; ++i)
        {
            pushCandidate(searchRoot / L"apps" / L"dx11_viewer" / L"shaders" / filePath);
            pushCandidate(searchRoot / L"dx11_viewer" / L"shaders" / filePath);
            const std::filesystem::path parent = searchRoot.parent_path();
            if (parent.empty() || parent == searchRoot) break;
            searchRoot = parent;
        }

        auto readBytes = [](const std::filesystem::path& path, std::string& bytes)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) return false;
            input.seekg(0, std::ios::end);
            const std::streamoff size = input.tellg();
            if (size < 0) return false;
            input.seekg(0, std::ios::beg);
            bytes.resize(static_cast<size_t>(size));
            if (size > 0) input.read(bytes.data(), size);
            return static_cast<bool>(input) || size == 0;
        };

        HRESULT hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        std::filesystem::path compiledPath;
        std::wstringstream tried;
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        for (const auto& candidate : candidates)
        {
            const bool exists = std::filesystem::exists(candidate);
            tried << candidate.c_str() << (exists ? L"  [exists]" : L"  [missing]") << L"\r\n";
            if (!exists) continue;

            std::string source;
            if (!readBytes(candidate, source))
            {
                hr = HRESULT_FROM_WIN32(ERROR_READ_FAULT);
                continue;
            }

            shaderBlob.Reset();
            errorBlob.Reset();
            compiledPath = candidate;
            const std::string sourceName = candidate.string();
            hr = D3DCompile(
                source.data(), source.size(), sourceName.c_str(),
                nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entryPoint, target, flags, 0u, &shaderBlob, &errorBlob);
            if (SUCCEEDED(hr)) break;
        }

        if (FAILED(hr) || !shaderBlob)
        {
            if (errorBlob)
            {
                const std::string message(
                    static_cast<const char*>(errorBlob->GetBufferPointer()),
                    errorBlob->GetBufferSize());
                errorText.assign(message.begin(), message.end());
            }
            else
            {
                std::wstringstream message;
                message << L"Unknown shader compilation error. HRESULT: 0x"
                        << std::hex << std::uppercase << static_cast<unsigned long>(hr);
                errorText = message.str();
            }
            if (!compiledPath.empty())
                errorText += L"\r\nShader source path: " + compiledPath.wstring();
            errorText += L"\r\n\r\nTried paths:\r\n" + tried.str();
            return false;
        }

        *blobOut = shaderBlob.Detach();
        return true;
    }

    bool GpuHairViewer::CompileSegmentGroupShaderFromFile(
        const char* entryPoint,
        bool compactionBindings,
        ID3DBlob** blobOut,
        std::wstring& errorText)
    {
        if (!entryPoint || !blobOut)
        {
            errorText = L"CompileSegmentGroupShaderFromFile received an invalid argument.";
            return false;
        }

        *blobOut = nullptr;
        errorText.clear();
        constexpr wchar_t kShaderFile[] = L"HairSegmentGroupRuntime.hlsl";

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    #if defined(_DEBUG)
        flags &= ~D3DCOMPILE_OPTIMIZATION_LEVEL3;
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #endif

        wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        const std::filesystem::path exeDir =
            std::filesystem::path(modulePath).parent_path();

        std::vector<std::filesystem::path> candidates;
        auto pushCandidate = [&candidates](const std::filesystem::path& value)
        {
            if (value.empty()) return;
            const std::filesystem::path normalized = value.lexically_normal();
            if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end())
                candidates.push_back(normalized);
        };

        pushCandidate(std::filesystem::path(HAIRGPU_VIEWER_SHADER_DIR) / kShaderFile);
        pushCandidate(exeDir / kShaderFile);
        pushCandidate(exeDir / L"shaders" / kShaderFile);
        pushCandidate(std::filesystem::current_path() / kShaderFile);
        pushCandidate(std::filesystem::current_path() / L"shaders" / kShaderFile);

        std::filesystem::path searchRoot = exeDir;
        for (int i = 0; i < 8; ++i)
        {
            pushCandidate(searchRoot / L"apps" / L"dx11_viewer" / L"shaders" / kShaderFile);
            pushCandidate(searchRoot / L"dx11_viewer" / L"shaders" / kShaderFile);
            const std::filesystem::path parent = searchRoot.parent_path();
            if (parent.empty() || parent == searchRoot) break;
            searchRoot = parent;
        }

        auto readBytes = [](const std::filesystem::path& path, std::string& bytes)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) return false;
            input.seekg(0, std::ios::end);
            const std::streamoff size = input.tellg();
            if (size < 0) return false;
            input.seekg(0, std::ios::beg);
            bytes.resize(static_cast<size_t>(size));
            if (size > 0) input.read(bytes.data(), size);
            return static_cast<bool>(input) || size == 0;
        };

        const D3D_SHADER_MACRO macros[] = {
            { "HAIRGPU_COMPACTION_BINDINGS", compactionBindings ? "1" : "0" },
            { nullptr, nullptr }
        };

        HRESULT hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        std::filesystem::path compiledPath;
        std::wstringstream tried;
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        for (const auto& candidate : candidates)
        {
            const bool exists = std::filesystem::exists(candidate);
            tried << candidate.c_str() << (exists ? L"  [exists]" : L"  [missing]") << L"\r\n";
            if (!exists) continue;

            std::string source;
            if (!readBytes(candidate, source))
            {
                hr = HRESULT_FROM_WIN32(ERROR_READ_FAULT);
                continue;
            }

            shaderBlob.Reset();
            errorBlob.Reset();
            compiledPath = candidate;
            const std::string sourceName = candidate.string();
            hr = D3DCompile(
                source.data(), source.size(), sourceName.c_str(),
                macros, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entryPoint, "cs_5_0", flags, 0u, &shaderBlob, &errorBlob);
            if (SUCCEEDED(hr)) break;
        }

        if (FAILED(hr) || !shaderBlob)
        {
            if (errorBlob)
            {
                const std::string message(
                    static_cast<const char*>(errorBlob->GetBufferPointer()),
                    errorBlob->GetBufferSize());
                errorText.assign(message.begin(), message.end());
            }
            else
            {
                std::wstringstream message;
                message << L"HairSegmentGroupRuntime.hlsl compilation failed. HRESULT: 0x"
                        << std::hex << std::uppercase << static_cast<unsigned long>(hr);
                errorText = message.str();
            }
            if (!compiledPath.empty())
                errorText += L"\r\nShader source path: " + compiledPath.wstring();
            errorText += L"\r\nHAIRGPU_COMPACTION_BINDINGS=";
            errorText += compactionBindings ? L"1" : L"0";
            errorText += L"\r\n\r\nTried paths:\r\n" + tried.str();
            return false;
        }

        *blobOut = shaderBlob.Detach();
        return true;
    }

    GpuHairViewer::StructuredBuffer GpuHairViewer::CreateStructuredBuffer(
        UINT elementSize,
        UINT elementCount,
        const void* initialData,
        bool unorderedAccess)
    {
        StructuredBuffer result;
        if (!m_device || elementSize == 0u || elementCount == 0u) return result;

        const uint64_t byteWidth =
            static_cast<uint64_t>(elementSize) * static_cast<uint64_t>(elementCount);
        if (byteWidth > static_cast<uint64_t>((std::numeric_limits<UINT>::max)()))
            return result;

        D3D11_BUFFER_DESC desc = {};
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
            (unorderedAccess ? D3D11_BIND_UNORDERED_ACCESS : 0u);
        desc.ByteWidth = static_cast<UINT>(byteWidth);
        desc.StructureByteStride = elementSize;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.Usage = D3D11_USAGE_DEFAULT;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = initialData;
        if (FAILED(m_device->CreateBuffer(&desc, initialData ? &data : nullptr, &result.buffer)))
            return {};

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0u;
        srvDesc.Buffer.NumElements = elementCount;
        if (FAILED(m_device->CreateShaderResourceView(result.buffer.Get(), &srvDesc, &result.srv)))
            return {};

        if (unorderedAccess)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.FirstElement = 0u;
            uavDesc.Buffer.NumElements = elementCount;
            if (FAILED(m_device->CreateUnorderedAccessView(result.buffer.Get(), &uavDesc, &result.uav)))
                return {};
        }
        return result;
    }

    RECT GpuHairViewer::GetRenderViewportRect() const
    {
        RECT rect = {
            0,
            0,
            static_cast<LONG>(std::max<UINT>(
                m_width > static_cast<UINT>(kUiPanelWidth)
                    ? m_width - static_cast<UINT>(kUiPanelWidth)
                    : m_width,
                1u)),
            static_cast<LONG>(std::max<UINT>(m_height, 1u))
        };
        return rect;
    }
