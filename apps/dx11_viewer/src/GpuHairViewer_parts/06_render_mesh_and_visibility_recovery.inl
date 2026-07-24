// GpuHairViewer.cpp ordered part 06: render_mesh_and_visibility_recovery
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.
// P0_INSTANCED_RUNTIME: render tail contains no full-particle CPU readback path.

        // GPU debug overlays are excluded from formal measurement.
        // They are auxiliary UI diagnostics and must not affect formal timing.
const bool explicitDebugOverlayRequested =
    (m_workspaceMode == WorkspaceMode::Groom &&
     m_interactionMode == InteractionMode::Edit) ||
    m_showFloorGuide ||
    m_showGridBounds ||
    m_showScalpWireframe ||
    m_showGuideSkeleton ||
    m_showFollowerSkeleton ||
    m_showRootMarkers ||
    m_showFollowLinks ||
    m_showColliderDebug;
const bool allowGpuDebugLines =
    !IsExperimentModeEnabled() &&
    explicitDebugOverlayRequested &&
    m_context &&
    m_currentBufferIndex < 2u &&
    m_debugLineTemplateVertexBuffer &&
    m_debugInputLayout &&
    m_debugLineVS &&
    m_debugLinePS;

if (!allowGpuDebugLines && !m_gpuDebugLineEntries.empty())
{
    m_gpuDebugLineEntries.clear();
    m_debugLineStats = {};
    m_gpuDebugLinesDirty = true;
}

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

// A flip-discard swap chain does not guarantee that the presented back buffer
// remains readable. Capture the dedicated post-measurement render before Present.
if (m_visualCapturePending)
{
    m_visualCaptureSucceeded =
        CaptureBackBufferBmp(m_pendingVisualCapturePath);
    m_visualCapturePending = false;
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
        ss << L"[HAIRGPU V9 FORCE-APPLIED] Hair GPU Viewer"
            << L"  |  sessionFPS=" << std::fixed << std::setprecision(1)
            << m_sessionEffectiveFps
            << L"  rollingFPS=" << m_recentEffectiveFps
            << L"  onePctLow=" << m_recentOnePercentLowFps
            << L"  instantFPS="
            << (m_renderCadenceWindowValid ? m_renderFps : 0.0f)
            << L"  simHz=" << std::setprecision(1)
            << (m_renderCadenceWindowValid ? m_simulationHz : 0.0f)
            << L"  frameMs(last/p95/worst)=" << std::setprecision(2)
            << m_lastFrameIntervalMilliseconds << L"/"
            << m_recentFrameP95Milliseconds << L"/"
            << m_recentFrameWorstMilliseconds
            << L"  stalls100ms=" << m_observedStallCount100Milliseconds
            << L"  cpuPresentMs=" << m_lastCpuFrameWorkMilliseconds
            << L"  gpuFrameMs=" << m_lastGpuFrameMilliseconds
            << L"  gpuThroughputFPS=" << std::setprecision(1)
            << m_gpuThroughputFps
            << L"  droppedSimMs=" << std::setprecision(2)
            << m_droppedSimulationMilliseconds
            << L"  presentSync=" << m_lastObservedPresentSyncInterval
            << L"  evidence=" << GetPerformanceEvidenceClassDisplayName()
            << L"  |  preset=" << m_activePresetName
            << L"  guides=" << m_asset.guideStrandCount
            << L"  followers=" << m_asset.followerStrandCount
            << L"  totalStrands=" << m_asset.strands.size()
            << L"  particles=" << m_asset.restPoints.size()
            << L"  render="
            << (m_renderMode == RenderMode::ParticlePointSprite
                ? L"point"
                : (m_renderMode == RenderMode::ParticleBillboard
                    ? L"billboard"
                    : L"ribbon"))
            << L"  mode="
            << (m_interactionMode == InteractionMode::Edit ? L"edit" : L"view")
            << (m_paused ? L"  [PAUSED]" : L"");
        SetWindowTextW(m_hwnd, ss.str().c_str());
    }

    bool GpuHairViewer::CaptureBackBufferBmp(
        const std::filesystem::path& outputPath)
    {
        if (!m_swapChain || !m_device || !m_context || outputPath.empty())
        {
            return false;
        }

        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC sourceDesc = {};
        backBuffer->GetDesc(&sourceDesc);
        if (sourceDesc.Width == 0u || sourceDesc.Height == 0u)
        {
            return false;
        }
        const bool rgba =
            sourceDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            sourceDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        const bool bgra =
            sourceDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
            sourceDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if (!rgba && !bgra)
        {
            return false;
        }

        ComPtr<ID3D11Texture2D> resolved;
        ID3D11Texture2D* copySource = backBuffer.Get();
        if (sourceDesc.SampleDesc.Count > 1u)
        {
            D3D11_TEXTURE2D_DESC resolveDesc = sourceDesc;
            resolveDesc.SampleDesc.Count = 1u;
            resolveDesc.SampleDesc.Quality = 0u;
            resolveDesc.Usage = D3D11_USAGE_DEFAULT;
            resolveDesc.BindFlags = 0u;
            resolveDesc.CPUAccessFlags = 0u;
            resolveDesc.MiscFlags = 0u;
            if (FAILED(m_device->CreateTexture2D(&resolveDesc, nullptr, &resolved)) || !resolved)
            {
                return false;
            }
            m_context->ResolveSubresource(
                resolved.Get(),
                0u,
                backBuffer.Get(),
                0u,
                sourceDesc.Format);
            copySource = resolved.Get();
            sourceDesc = resolveDesc;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0u;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0u;
        stagingDesc.SampleDesc.Count = 1u;
        stagingDesc.SampleDesc.Quality = 0u;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(m_device->CreateTexture2D(&stagingDesc, nullptr, &staging)) || !staging)
        {
            return false;
        }
        m_context->CopyResource(staging.Get(), copySource);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(staging.Get(), 0u, D3D11_MAP_READ, 0u, &mapped)))
        {
            return false;
        }

        const uint32_t width = stagingDesc.Width;
        const uint32_t height = stagingDesc.Height;
        const uint64_t pixelBytes64 =
            static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
        if (pixelBytes64 > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        {
            m_context->Unmap(staging.Get(), 0u);
            return false;
        }
        std::vector<uint8_t> pixels(static_cast<size_t>(pixelBytes64));
        for (uint32_t outputRow = 0u; outputRow < height; ++outputRow)
        {
            const uint32_t sourceRow = height - 1u - outputRow;
            const uint8_t* source =
                static_cast<const uint8_t*>(mapped.pData) +
                static_cast<size_t>(sourceRow) * mapped.RowPitch;
            uint8_t* destination =
                pixels.data() +
                static_cast<size_t>(outputRow) * static_cast<size_t>(width) * 4u;
            for (uint32_t x = 0u; x < width; ++x)
            {
                const uint8_t* inputPixel = source + static_cast<size_t>(x) * 4u;
                uint8_t* outputPixel = destination + static_cast<size_t>(x) * 4u;
                if (rgba)
                {
                    outputPixel[0] = inputPixel[2];
                    outputPixel[1] = inputPixel[1];
                    outputPixel[2] = inputPixel[0];
                    outputPixel[3] = inputPixel[3];
                }
                else
                {
                    outputPixel[0] = inputPixel[0];
                    outputPixel[1] = inputPixel[1];
                    outputPixel[2] = inputPixel[2];
                    outputPixel[3] = inputPixel[3];
                }
            }
        }
        m_context->Unmap(staging.Get(), 0u);

        BITMAPFILEHEADER fileHeader = {};
        BITMAPINFOHEADER infoHeader = {};
        const uint32_t pixelBytes = static_cast<uint32_t>(pixelBytes64);
        fileHeader.bfType = 0x4D42u;
        fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;
        infoHeader.biSize = sizeof(BITMAPINFOHEADER);
        infoHeader.biWidth = static_cast<LONG>(width);
        infoHeader.biHeight = static_cast<LONG>(height);
        infoHeader.biPlanes = 1u;
        infoHeader.biBitCount = 32u;
        infoHeader.biCompression = BI_RGB;
        infoHeader.biSizeImage = pixelBytes;

        std::error_code error;
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error)
        {
            return false;
        }
        const std::filesystem::path temporary = outputPath.wstring() + L".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return false;
        }
        output.write(
            reinterpret_cast<const char*>(&fileHeader),
            sizeof(fileHeader));
        output.write(
            reinterpret_cast<const char*>(&infoHeader),
            sizeof(infoHeader));
        output.write(
            reinterpret_cast<const char*>(pixels.data()),
            static_cast<std::streamsize>(pixels.size()));
        output.flush();
        if (!output.good())
        {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
        output.close();
        std::filesystem::remove(outputPath, error);
        error.clear();
        std::filesystem::rename(temporary, outputPath, error);
        if (error)
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    }

    void GpuHairViewer::Shutdown()
    {
        if (m_context)
        {
            m_context->ClearState();
        }
    }

