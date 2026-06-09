// GpuHairViewer.cpp ordered part 02: resource_creation_and_shader_loading
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

        if (!CreateSimulationResources())
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure",
                L"CreateSimulationResources() failed.\r\nThe viewer window was created, but GPU simulation buffers were not initialized.");
            Shutdown();
            return false;
        }

        if (!CreateRenderResources())
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure",
                L"CreateRenderResources() failed.\r\nThe viewer window was created, but render resources were not initialized.");
            Shutdown();
            return false;
        }

        if (!LoadShaders())
        {
            if (m_hwnd)
            {
                ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure",
                    L"LoadShaders() failed.\r\nCheck the shader file paths and HLSL compile error dialog.");
            }
            Shutdown();
            return false;
        }

        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        AutoFrameCameraToAsset(true);
        ResetSimulation();
        QueryPerformanceFrequency(&m_qpcFrequency);
        QueryPerformanceCounter(&m_lastTick);
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
        SyncControlsFromState();
        LayoutUiControls();
        UpdateStatsLabel();
        RefreshAssetStatsText();
        UpdateWindowTitle();
        return true;
    }

    int GpuHairViewer::Run()
    {
        MSG msg = {};
        LoadExperimentCaseFromEnvironment();
        m_globalFrameIndex = 0u;
        m_measuredFrameIndex = 0u;
        while (m_running)
        {
            ResolveGpuTimingFrame(m_gpuTimingFrames[m_gpuTimingWriteIndex]);
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    m_running = false;
                    break;
                }

                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            LARGE_INTEGER now = {};
            QueryPerformanceCounter(&now);
            const float dt = static_cast<float>(now.QuadPart - m_lastTick.QuadPart) / static_cast<float>(m_qpcFrequency.QuadPart);
            m_lastTick = now;
            m_lastFrameSeconds = dt;
            const float instantFps = (dt > 1.0e-6f) ? (1.0f / dt) : 0.0f;
            m_smoothedFps = (m_smoothedFps <= 0.0f) ? instantFps : (m_smoothedFps * 0.92f + instantFps * 0.08f);
            m_statsUpdateAccumulator += dt;

            GpuTimingFrame& gpuFrame = m_gpuTimingFrames[m_gpuTimingWriteIndex];
            m_activeGpuTimingFrameIndex = -1;
            if (gpuFrame.disjoint)
            {
                m_context->Begin(gpuFrame.disjoint.Get());
                m_context->End(gpuFrame.frameStart.Get());
                gpuFrame.issued = true;
                m_activeGpuTimingFrameIndex = static_cast<int32_t>(m_gpuTimingWriteIndex);
            }

            m_lastSimulationStepMilliseconds = 0.0f;
            if (!m_paused)
            {
                LARGE_INTEGER simBegin = {};
                LARGE_INTEGER simEnd = {};
                QueryPerformanceCounter(&simBegin);
                m_accumulator += std::min(dt, 0.1f);
                while (m_accumulator >= m_simConfig.fixedTimeStep)
                {
                    StepSimulation(m_simConfig.fixedTimeStep);
                    m_accumulator -= m_simConfig.fixedTimeStep;
                    m_elapsedSeconds += m_simConfig.fixedTimeStep;
                }
                QueryPerformanceCounter(&simEnd);
                m_lastSimulationStepMilliseconds = static_cast<float>(simEnd.QuadPart - simBegin.QuadPart) * 1000.0f / static_cast<float>(m_qpcFrequency.QuadPart);
            }
            else if (gpuFrame.issued)
            {
                m_context->End(gpuFrame.simStart.Get());
                m_context->End(gpuFrame.simEnd.Get());
            }

            PerformanceSample sample = BuildPerformanceSample(dt, instantFps);
            if (ShouldCaptureCurrentFrame())
            {
                sample.measuredFrameIndex = m_measuredFrameIndex++;
                m_performanceSamples.push_back(sample);
                if (m_performanceSamples.size() > 12000)
                {
                    m_performanceSamples.erase(m_performanceSamples.begin(), m_performanceSamples.begin() + static_cast<std::ptrdiff_t>(m_performanceSamples.size() - 12000));
                }
            }

            UpdateCamera();
            Render();
            ++m_globalFrameIndex;
            if (ShouldAutoExitAfterCurrentFrame())
            {
                m_running = false;
            }
            if (!m_gpuTimingFrames.empty())
            {
                m_gpuTimingWriteIndex = (m_gpuTimingWriteIndex + 1u) % static_cast<uint32_t>(m_gpuTimingFrames.size());
            }
            m_activeGpuTimingFrameIndex = -1;
            if (m_statsUpdateAccumulator >= 0.2f)
            {
                UpdateStatsLabel();
                m_statsUpdateAccumulator = 0.0f;
            }
        }

        const std::wstring autoTelemetry = GetEnvironmentVariableString(L"HAIRGPU_AUTO_TELEMETRY");
        if (!autoTelemetry.empty() && autoTelemetry != L"0" && autoTelemetry != L"false" && autoTelemetry != L"FALSE")
        {
            std::filesystem::path telemetryDir = GetEnvironmentVariableString(L"HAIRGPU_TELEMETRY_DIR");
            if (telemetryDir.empty())
            {
                telemetryDir = std::filesystem::current_path();
            }
            std::wstring runName = GetEnvironmentVariableString(L"HAIRGPU_TELEMETRY_RUN_NAME");
            if (runName.empty())
            {
                runName = L"viewer_run";
            }
            std::error_code ec;
            std::filesystem::create_directories(telemetryDir, ec);
            const std::filesystem::path summaryPath = telemetryDir / (runName + L"_summary.json");
            const std::filesystem::path frameCsvPath = telemetryDir / (runName + L"_frames.csv");
            const std::filesystem::path frameJsonlPath = telemetryDir / (runName + L"_frames.jsonl");
            const std::filesystem::path settingsJsonPath = telemetryDir / (runName + L"_settings.json");
            ExportPerformanceCsv(frameCsvPath.wstring());
            ExportPerformanceJsonl(frameJsonlPath.wstring());
            ExportExperimentSettingsJson(settingsJsonPath.wstring());
            ExportRuntimeReport(summaryPath.wstring());
        }
        Shutdown();
        return 0;
    }

    bool GpuHairViewer::CreateWindowAndDevice(HINSTANCE instance, int showCommand)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = kWindowClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            const DWORD err = GetLastError();
            ShowStartupDialog(nullptr, L"Hair GPU Viewer Startup Failure",
                std::wstring(L"RegisterClassExW failed. Win32 error=") + Win32Hex(err));
            return false;
        }

        RECT rect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        m_hwnd = CreateWindowExW(
            0,
            kWindowClassName,
            L"Hair GPU Viewer",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance,
            this);

        if (!m_hwnd)
        {
            const DWORD err = GetLastError();
            ShowStartupDialog(nullptr, L"Hair GPU Viewer Startup Failure",
                std::wstring(L"CreateWindowExW failed. Win32 error=") + Win32Hex(err));
            return false;
        }

        ShowWindow(m_hwnd, showCommand);
        UpdateWindow(m_hwnd);

        DXGI_SWAP_CHAIN_DESC swapDesc = {};
        swapDesc.BufferCount = 2;
        swapDesc.BufferDesc.Width = m_width;
        swapDesc.BufferDesc.Height = m_height;
        swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.OutputWindow = m_hwnd;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.Windowed = TRUE;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT debugFlags = 0;
    #if defined(_DEBUG)
        debugFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        auto tryCreate = [&](D3D_DRIVER_TYPE driverType, UINT flags, HRESULT& outHr) -> bool
        {
            D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
            outHr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                driverType,
                nullptr,
                flags,
                &requested,
                1,
                D3D11_SDK_VERSION,
                &swapDesc,
                &m_swapChain,
                &m_device,
                nullptr,
                &m_context);
            return SUCCEEDED(outHr);
        };

        HRESULT hrHardwareDebug = E_FAIL;
        HRESULT hrHardware = E_FAIL;
        HRESULT hrWarp = E_FAIL;

        if (!(tryCreate(D3D_DRIVER_TYPE_HARDWARE, debugFlags, hrHardwareDebug) ||
              tryCreate(D3D_DRIVER_TYPE_HARDWARE, 0u, hrHardware) ||
              tryCreate(D3D_DRIVER_TYPE_WARP, 0u, hrWarp)))
        {
            std::wstring message =
                std::wstring(L"D3D11CreateDeviceAndSwapChain failed.\r\n\r\n") +
                L"Hardware + debug: " + HrHex(hrHardwareDebug) + L"\r\n" +
                L"Hardware: " + HrHex(hrHardware) + L"\r\n" +
                L"WARP fallback: " + HrHex(hrWarp) + L"\r\n\r\n" +
                L"Try one of these:\r\n" +
                L"- build/run Release\r\n" +
                L"- install Windows Optional Feature: Graphics Tools\r\n" +
                L"- use x64 configuration if available";
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure", message);
            return false;
        }

        Resize(m_width, m_height);
        return true;
    }

    bool GpuHairViewer::CreateSimulationResources()
    {
        if (m_asset.rootBindings.size() != m_asset.strands.size())
        {
            EnsureAssetScalpConformance(m_asset, nullptr);
        }
        m_initialParticles.clear();
        m_followGpuData.clear();
        m_restScalpParticles.clear();
        m_animatedScalpParticles.clear();
        m_scalpTriangles.clear();
        m_rootBindingGpuData.clear();
        m_zeroDistanceLambdas.clear();
        m_zeroBendLambdas.clear();

        m_initialParticles.reserve(m_asset.restPoints.size());
        m_followGpuData.reserve(m_asset.followData.size());
        m_restScalpParticles.reserve(m_asset.scalpMesh.positions.size());
        m_animatedScalpParticles.reserve(m_asset.scalpMesh.positions.size());
        m_scalpTriangles.reserve(m_asset.scalpMesh.triangles.size());
        m_rootBindingGpuData.reserve(m_asset.rootBindings.size());

        for (const Vec3& p : m_asset.restPoints)
        {
            m_initialParticles.push_back({ p.x, p.y, p.z, 1.0f });
        }

        for (const FollowData& f : m_asset.followData)
        {
            m_followGpuData.push_back({ f.parentGuide, f.followWeight, f.clumpWeight, f.noiseWeight });
        }

        m_renderData = BuildSegmentRenderData(m_asset);

        for (const Vec3& p : m_asset.scalpMesh.positions)
        {
            m_restScalpParticles.push_back({ p.x, p.y, p.z, 1.0f });
            m_animatedScalpParticles.push_back({ p.x, p.y, p.z, 1.0f });
        }

        for (const TriangleIndices& tri : m_asset.scalpMesh.triangles)
        {
            m_scalpTriangles.push_back({ tri.a, tri.b, tri.c, 0u });
        }

        for (const RootBinding& binding : m_asset.rootBindings)
        {
            RootBindingGpuData gpu = {};
            gpu.triangleIndex = binding.triangleIndex;
            gpu.barycentric[0] = binding.barycentric.x;
            gpu.barycentric[1] = binding.barycentric.y;
            gpu.barycentric[2] = binding.barycentric.z;
            gpu.rootUv[0] = binding.rootUv.x;
            gpu.rootUv[1] = binding.rootUv.y;
            m_rootBindingGpuData.push_back(gpu);
        }

        const UINT totalParticles = static_cast<UINT>(m_initialParticles.size());
        const UINT guideSegments = m_asset.guideStrandCount * (m_asset.particlesPerStrand > 0 ? (m_asset.particlesPerStrand - 1u) : 0u);
        const UINT guideBends = m_asset.guideStrandCount * (m_asset.particlesPerStrand > 2 ? (m_asset.particlesPerStrand - 2u) : 0u);
        m_zeroDistanceLambdas.resize(std::max<UINT>(guideSegments, 1u), 0.0f);
        m_zeroBendLambdas.resize(std::max<UINT>(guideBends, 1u), 0.0f);

        m_positions[0] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_positions[1] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_previousPositions[0] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_previousPositions[1] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_restPositions = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), false);
        m_pointWidths = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(m_asset.pointWidths.size()), m_asset.pointWidths.data(), false);
        m_followBuffer = CreateStructuredBuffer(sizeof(FollowGpuData), static_cast<UINT>(m_followGpuData.size()), m_followGpuData.data(), false);

        const UINT totalStrands = static_cast<UINT>(std::max<size_t>(m_asset.strands.size(), 1u));
        std::vector<uint32_t> initialRenderTiers(totalStrands, static_cast<uint32_t>(hairgpu::SimTier::Reduced));
        std::vector<uint32_t> initialGuideTiers(std::max<UINT>(m_asset.guideStrandCount, 1u), static_cast<uint32_t>(hairgpu::SimTier::Full));
        std::vector<uint32_t> initialFollowerTiers(std::max<UINT>(m_asset.followerStrandCount, 1u), static_cast<uint32_t>(hairgpu::SimTier::Reduced));
        for (uint32_t strandIndex = 0u; strandIndex < totalStrands; ++strandIndex)
        {
            const bool isGuide = strandIndex < m_asset.guideStrandCount;
            uint32_t tier = isGuide ? static_cast<uint32_t>(hairgpu::SimTier::Full)
                                    : static_cast<uint32_t>(hairgpu::SimTier::Reduced);
            if (strandIndex < m_activeSetPlan.recommendedSimulationTiers.size())
            {
                tier = static_cast<uint32_t>(m_activeSetPlan.recommendedSimulationTiers[strandIndex]);
            }
            initialRenderTiers[strandIndex] = tier;
            if (isGuide)
            {
                initialGuideTiers[strandIndex] = tier;
            }
            else
            {
                initialFollowerTiers[strandIndex - m_asset.guideStrandCount] = tier;
            }
        }
        std::array<uint32_t, 4> initialCompactionCounters = { 0u, 0u, 0u, 0u };
        std::vector<uint32_t> initialCompactedFollowerIndices(std::max<UINT>(m_asset.followerStrandCount, 1u), 0u);
        std::vector<uint32_t> initialCompactedGuideIndices(std::max<UINT>(m_asset.guideStrandCount, 1u), 0u);
        for (uint32_t i = 0u; i < m_asset.followerStrandCount && i < initialCompactedFollowerIndices.size(); ++i)
        {
            initialCompactedFollowerIndices[i] = i;
        }
        for (uint32_t i = 0u; i < m_asset.guideStrandCount && i < initialCompactedGuideIndices.size(); ++i)
        {
            initialCompactedGuideIndices[i] = i;
        }
        m_renderStrandTiersBuffer = CreateStructuredBuffer(sizeof(uint32_t), totalStrands, initialRenderTiers.data(), false);
        m_guideTierAssignmentsBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialGuideTiers.size()), initialGuideTiers.data(), false);
        m_followerTierAssignmentsBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialFollowerTiers.size()), initialFollowerTiers.data(), false);
        m_compactedFollowerIndicesBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialCompactedFollowerIndices.size()), initialCompactedFollowerIndices.data(), true);
        m_compactedFollowerCounterBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialCompactionCounters.size()), initialCompactionCounters.data(), true);
        m_compactedGuideIndicesBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialCompactedGuideIndices.size()), initialCompactedGuideIndices.data(), true);
        m_compactedGuideCounterBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialCompactionCounters.size()), initialCompactionCounters.data(), true);

        m_followDispatchArgsBuffer.Reset();
        m_followDispatchArgsUav.Reset();
        D3D11_BUFFER_DESC indirectArgsDesc = {};
        indirectArgsDesc.ByteWidth = sizeof(UINT) * 4u;
        indirectArgsDesc.Usage = D3D11_USAGE_DEFAULT;
        indirectArgsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        indirectArgsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        if (FAILED(m_device->CreateBuffer(&indirectArgsDesc, nullptr, &m_followDispatchArgsBuffer)))
        {
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC indirectArgsUavDesc = {};
        indirectArgsUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        indirectArgsUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        indirectArgsUavDesc.Buffer.FirstElement = 0;
        indirectArgsUavDesc.Buffer.NumElements = indirectArgsDesc.ByteWidth / sizeof(UINT);
        indirectArgsUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        if (FAILED(m_device->CreateUnorderedAccessView(m_followDispatchArgsBuffer.Get(), &indirectArgsUavDesc, &m_followDispatchArgsUav)))
        {
            return false;
        }

        m_guideDispatchArgsBuffer.Reset();
        m_guideDispatchArgsUav.Reset();
        if (FAILED(m_device->CreateBuffer(&indirectArgsDesc, nullptr, &m_guideDispatchArgsBuffer)))
        {
            return false;
        }
        if (FAILED(m_device->CreateUnorderedAccessView(m_guideDispatchArgsBuffer.Get(), &indirectArgsUavDesc, &m_guideDispatchArgsUav)))
        {
            return false;
        }
        m_guidePointDispatchArgsBuffer.Reset();
        m_guidePointDispatchArgsUav.Reset();
        if (FAILED(m_device->CreateBuffer(&indirectArgsDesc, nullptr, &m_guidePointDispatchArgsBuffer)))
        {
            return false;
        }
        if (FAILED(m_device->CreateUnorderedAccessView(m_guidePointDispatchArgsBuffer.Get(), &indirectArgsUavDesc, &m_guidePointDispatchArgsUav)))
        {
            return false;
        }

        m_scalpPositions = CreateStructuredBuffer(sizeof(GpuParticle), static_cast<UINT>(m_animatedScalpParticles.size()), m_animatedScalpParticles.data(), false);
        m_scalpTrianglesBuffer = CreateStructuredBuffer(sizeof(TriangleGpuData), static_cast<UINT>(m_scalpTriangles.size()), m_scalpTriangles.data(), false);
        m_rootBindingsBuffer = CreateStructuredBuffer(sizeof(RootBindingGpuData), static_cast<UINT>(m_rootBindingGpuData.size()), m_rootBindingGpuData.data(), false);
        std::vector<float> sdfValues = (!m_colliders.sdfGrids.empty() && !m_colliders.sdfGrids.front().values.empty()) ? m_colliders.sdfGrids.front().values : std::vector<float>{ 0.0f };
        m_sdfValuesBuffer = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(sdfValues.size()), sdfValues.data(), false);
        m_distanceLambdaBuffer = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(m_zeroDistanceLambdas.size()), m_zeroDistanceLambdas.data(), true);
        m_bendLambdaBuffer = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(m_zeroBendLambdas.size()), m_zeroBendLambdas.data(), true);

        const UINT gridCellCount = std::max<UINT>(m_simConfig.selfCollisionGridDimX * m_simConfig.selfCollisionGridDimY * m_simConfig.selfCollisionGridDimZ, 1u);
        std::vector<uint32_t> zeroCellCounts(gridCellCount, 0u);
        std::vector<uint32_t> zeroCellIndices(std::max<UINT>(gridCellCount * m_simConfig.selfCollisionCellCapacity, 1u), 0xFFFFFFFFu);
        m_gridCellCountsBuffer = CreateStructuredBuffer(sizeof(uint32_t), gridCellCount, zeroCellCounts.data(), true);
        m_gridCellIndicesBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(zeroCellIndices.size()), zeroCellIndices.data(), true);

        const UINT hashBucketCount = std::max<UINT>(m_simConfig.selfCollisionHashBucketCount, 1u);
        std::vector<uint32_t> zeroHashHeads(hashBucketCount, 0xFFFFFFFFu);
        std::vector<uint32_t> zeroParticleNext(std::max<UINT>(totalParticles, 1u), 0xFFFFFFFFu);
        m_gridHashHeadsBuffer = CreateStructuredBuffer(sizeof(uint32_t), hashBucketCount, zeroHashHeads.data(), true);
        m_gridParticleNextBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(zeroParticleNext.size()), zeroParticleNext.data(), true);

        const UINT totalGuideParticles = std::max<UINT>(m_asset.guideStrandCount * m_asset.particlesPerStrand, 1u);
        m_sortedBroadphasePaddedCount = NextPowerOfTwo(totalGuideParticles);
        std::vector<uint32_t> invalidSortedKeys(m_sortedBroadphasePaddedCount, 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedIndices(m_sortedBroadphasePaddedCount, 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedStarts(gridCellCount, 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedEnds(gridCellCount, 0xFFFFFFFFu);
        m_sortedKeysBuffer = CreateStructuredBuffer(sizeof(uint32_t), m_sortedBroadphasePaddedCount, invalidSortedKeys.data(), true);
        m_sortedParticleIndicesBuffer = CreateStructuredBuffer(sizeof(uint32_t), m_sortedBroadphasePaddedCount, invalidSortedIndices.data(), true);
        m_sortedCellStartsBuffer = CreateStructuredBuffer(sizeof(uint32_t), gridCellCount, invalidSortedStarts.data(), true);
        m_sortedCellEndsBuffer = CreateStructuredBuffer(sizeof(uint32_t), gridCellCount, invalidSortedEnds.data(), true);

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.ByteWidth = static_cast<UINT>(m_renderData.segmentVertices.size() * sizeof(SegmentRenderVertex));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = m_renderData.segmentVertices.data();
        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, &m_segmentVertexBuffer)))
        {
            return false;
        }

        m_strandTubeVertexBuffer.Reset();
        m_strandTubeIndexBuffer.Reset();
        m_strandTubeIndexCount = 0;
        m_strandTubeRingSides = 0u;

        std::vector<ScalpVertex> scalpVertices;
        scalpVertices.reserve(m_asset.scalpMesh.positions.size());
        for (size_t i = 0; i < m_asset.scalpMesh.positions.size(); ++i)
        {
            const Vec3 n = (i < m_asset.scalpMesh.normals.size()) ? m_asset.scalpMesh.normals[i] : Vec3(0.0f, 1.0f, 0.0f);
            ScalpVertex v = {};
            v.scalpIndex = static_cast<uint32_t>(i);
            v.nx = n.x;
            v.ny = n.y;
            v.nz = n.z;
            float r = 0.76f, g = 0.62f, b = 0.52f;
            const Vec3 uvw = ComputeScalpPreviewUv(m_asset.scalpMesh.positions[i]);
            EvaluateScalpCoverageTint(uvw.x, uvw.y, r, g, b);
            v.r = r;
            v.g = g;
            v.b = b;
            scalpVertices.push_back(v);
        }
        if (!scalpVertices.empty())
        {
            D3D11_BUFFER_DESC scalpVbDesc = {};
            scalpVbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            scalpVbDesc.ByteWidth = static_cast<UINT>(scalpVertices.size() * sizeof(ScalpVertex));
            scalpVbDesc.Usage = D3D11_USAGE_DYNAMIC;
            scalpVbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            D3D11_SUBRESOURCE_DATA scalpVbData = {};
            scalpVbData.pSysMem = scalpVertices.data();
            if (FAILED(m_device->CreateBuffer(&scalpVbDesc, &scalpVbData, &m_scalpVertexBuffer)))
            {
                return false;
            }
        }

        std::vector<uint32_t> scalpIndices;
        scalpIndices.reserve(m_asset.scalpMesh.triangles.size() * 3ull);
        for (const TriangleIndices& tri : m_asset.scalpMesh.triangles)
        {
            scalpIndices.push_back(tri.a);
            scalpIndices.push_back(tri.b);
            scalpIndices.push_back(tri.c);
        }
        m_scalpIndexCount = static_cast<uint32_t>(scalpIndices.size());
        if (!scalpIndices.empty())
        {
            D3D11_BUFFER_DESC scalpIbDesc = {};
            scalpIbDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            scalpIbDesc.ByteWidth = static_cast<UINT>(scalpIndices.size() * sizeof(uint32_t));
            scalpIbDesc.Usage = D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA scalpIbData = {};
            scalpIbData.pSysMem = scalpIndices.data();
            if (FAILED(m_device->CreateBuffer(&scalpIbDesc, &scalpIbData, &m_scalpIndexBuffer)))
            {
                return false;
            }
        }

        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        cbDesc.ByteWidth = sizeof(SimConstantsHair);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_simConstantsHair)))
        {
            return false;
        }

        cbDesc.ByteWidth = sizeof(SimConstantsActiveSet);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_simConstantsActiveSet)))
        {
            return false;
        }

        cbDesc.ByteWidth = sizeof(SimConstantsScalp);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_simConstantsScalp)))
        {
            return false;
        }

        cbDesc.ByteWidth = sizeof(BitonicConstants);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_bitonicConstants)))
        {
            return false;
        }

        cbDesc.ByteWidth = sizeof(RenderConstants);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_renderConstants)))
        {
            return false;
        }

        m_gpuDebugLinesDirty = true;
        m_debugLineStats = {};
        return true;
    }

    bool GpuHairViewer::CreateGpuTimingQueries()
    {
        for (GpuTimingFrame& frame : m_gpuTimingFrames)
        {
            frame = {};
            auto createQuery = [this](D3D11_QUERY query, Microsoft::WRL::ComPtr<ID3D11Query>& outQuery) -> bool
            {
                D3D11_QUERY_DESC desc = {};
                desc.Query = query;
                return SUCCEEDED(m_device->CreateQuery(&desc, &outQuery));
            };
            if (!createQuery(D3D11_QUERY_TIMESTAMP_DISJOINT, frame.disjoint) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.frameStart) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.simStart) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.simEnd) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.hairDrawStart) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.hairDrawEnd) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.debugDrawStart) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.debugDrawEnd) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.frameEnd) ||
                !createQuery(D3D11_QUERY_PIPELINE_STATISTICS, frame.hairPipelineStats) ||
                !createQuery(D3D11_QUERY_PIPELINE_STATISTICS, frame.debugPipelineStats))
            {
                return false;
            }
        }
        m_gpuTimingWriteIndex = 0u;
        m_activeGpuTimingFrameIndex = -1;
        return true;
    }

    void GpuHairViewer::ResolveGpuTimingFrame(GpuTimingFrame& frame)
    {
        if (!frame.issued || !frame.disjoint)
        {
            return;
        }

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
        UINT64 frameStart = 0ull;
        UINT64 simStart = 0ull;
        UINT64 simEnd = 0ull;
        UINT64 hairStart = 0ull;
        UINT64 hairEnd = 0ull;
        UINT64 debugStart = 0ull;
        UINT64 debugEnd = 0ull;
        UINT64 frameEnd = 0ull;
        D3D11_QUERY_DATA_PIPELINE_STATISTICS hairStats = {};
        D3D11_QUERY_DATA_PIPELINE_STATISTICS debugStats = {};

        const UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
        if (m_context->GetData(frame.disjoint.Get(), &disjoint, sizeof(disjoint), flags) != S_OK ||
            m_context->GetData(frame.frameStart.Get(), &frameStart, sizeof(frameStart), flags) != S_OK ||
            m_context->GetData(frame.simStart.Get(), &simStart, sizeof(simStart), flags) != S_OK ||
            m_context->GetData(frame.simEnd.Get(), &simEnd, sizeof(simEnd), flags) != S_OK ||
            m_context->GetData(frame.hairDrawStart.Get(), &hairStart, sizeof(hairStart), flags) != S_OK ||
            m_context->GetData(frame.hairDrawEnd.Get(), &hairEnd, sizeof(hairEnd), flags) != S_OK ||
            m_context->GetData(frame.debugDrawStart.Get(), &debugStart, sizeof(debugStart), flags) != S_OK ||
            m_context->GetData(frame.debugDrawEnd.Get(), &debugEnd, sizeof(debugEnd), flags) != S_OK ||
            m_context->GetData(frame.frameEnd.Get(), &frameEnd, sizeof(frameEnd), flags) != S_OK ||
            m_context->GetData(frame.hairPipelineStats.Get(), &hairStats, sizeof(hairStats), flags) != S_OK ||
            m_context->GetData(frame.debugPipelineStats.Get(), &debugStats, sizeof(debugStats), flags) != S_OK)
        {
            return;
        }

        if (!disjoint.Disjoint && disjoint.Frequency > 0ull)
        {
            const double invFreqMs = 1000.0 / static_cast<double>(disjoint.Frequency);
            m_lastGpuFrameMilliseconds = static_cast<float>(static_cast<double>(frameEnd - frameStart) * invFreqMs);
            m_lastGpuSimulationMilliseconds = static_cast<float>(static_cast<double>(simEnd - simStart) * invFreqMs);
            m_lastGpuHairDrawMilliseconds = static_cast<float>(static_cast<double>(hairEnd - hairStart) * invFreqMs);
            m_lastGpuDebugDrawMilliseconds = static_cast<float>(static_cast<double>(debugEnd - debugStart) * invFreqMs);
        }
        m_lastHairPipelineStats = hairStats;
        m_lastDebugPipelineStats = debugStats;
        frame.issued = false;
    }

    bool GpuHairViewer::CreateRenderResources()
    {
        D3D11_RASTERIZER_DESC raster = {};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (FAILED(m_device->CreateRasterizerState(&raster, &m_rasterizerState)))
        {
            return false;
        }

        D3D11_BLEND_DESC blend = {};
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&blend, &m_blendState)))
        {
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC depthDesc = {};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (FAILED(m_device->CreateDepthStencilState(&depthDesc, &m_depthState)))
        {
            return false;
        }

        if (!m_debugLineTemplateVertexBuffer)
        {
            const DebugLineTemplateVertex debugTemplate[2] = { { 0.0f }, { 1.0f } };
            D3D11_BUFFER_DESC debugTemplateDesc = {};
            debugTemplateDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            debugTemplateDesc.ByteWidth = sizeof(debugTemplate);
            debugTemplateDesc.Usage = D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA debugTemplateData = {};
            debugTemplateData.pSysMem = debugTemplate;
            if (FAILED(m_device->CreateBuffer(&debugTemplateDesc, &debugTemplateData, &m_debugLineTemplateVertexBuffer)))
            {
                return false;
            }
        }

        if (!CreateGpuTimingQueries())
        {
            return false;
        }

