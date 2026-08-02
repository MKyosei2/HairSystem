// GpuHairViewer.cpp ordered part 18:
// consume SG-APT RenderDirtyRange through a persistent GPU render-position cache.
//
// The existing renderer already generates ribbon vertices directly from the
// position SRV. This layer updates a persistent position cache as follows:
//   * all guide points are copied every proposal frame;
//   * only follower point ranges emitted by RenderDirtyRange are copied;
//   * unchanged follower ranges retain the previous frame's positions;
//   * A11/B7/full-dynamic/fallback paths copy the complete position buffer.
//
// This turns RenderDirtyRange from telemetry-only metadata into an actual
// End-to-End render update dependency without CPU readback or CPU full upload.

    namespace
    {
        struct alignas(16) N01RenderCacheConstants
        {
            uint32_t guideCount = 0u;
            uint32_t followerCount = 0u;
            uint32_t particlesPerStrand = 0u;
            uint32_t totalPointCount = 0u;
        };

        bool N01CompileRenderCacheShader(
            ID3D11Device* device,
            const char* entryPoint,
            Microsoft::WRL::ComPtr<ID3D11ComputeShader>& destination,
            std::wstring& errorText)
        {
            if (!device || !entryPoint)
            {
                errorText = L"Invalid Novelty 01 render-cache shader request.";
                return false;
            }

            wchar_t modulePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
            const std::filesystem::path executableDirectory =
                std::filesystem::path(modulePath).parent_path();
            const wchar_t* fileName = L"HairNovelty01RenderDirtyCache.hlsl";

            std::vector<std::filesystem::path> candidates;
            const auto pushCandidate =
                [&candidates](const std::filesystem::path& path)
            {
                if (path.empty())
                {
                    return;
                }
                const std::filesystem::path normalized =
                    path.lexically_normal();
                if (std::find(
                        candidates.begin(),
                        candidates.end(),
                        normalized) == candidates.end())
                {
                    candidates.push_back(normalized);
                }
            };

#if defined(HAIRGPU_VIEWER_SHADER_DIR)
            pushCandidate(
                std::filesystem::path(HAIRGPU_VIEWER_SHADER_DIR) / fileName);
#endif
            pushCandidate(executableDirectory / fileName);
            pushCandidate(executableDirectory / L"shaders" / fileName);
            pushCandidate(std::filesystem::current_path() / fileName);
            pushCandidate(
                std::filesystem::current_path() / L"shaders" / fileName);

            std::filesystem::path searchRoot = executableDirectory;
            for (int depth = 0; depth < 8; ++depth)
            {
                pushCandidate(
                    searchRoot / L"apps" / L"dx11_viewer" /
                    L"shaders" / fileName);
                if (!searchRoot.has_parent_path())
                {
                    break;
                }
                const std::filesystem::path parent =
                    searchRoot.parent_path();
                if (parent == searchRoot)
                {
                    break;
                }
                searchRoot = parent;
            }

            std::wstringstream tried;
            HRESULT lastResult =
                HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            for (const std::filesystem::path& candidate : candidates)
            {
                tried << candidate.c_str() << L"\r\n";
                if (!std::filesystem::exists(candidate))
                {
                    continue;
                }

                Microsoft::WRL::ComPtr<ID3DBlob> byteCode;
                Microsoft::WRL::ComPtr<ID3DBlob> errors;
                lastResult = D3DCompileFromFile(
                    candidate.c_str(),
                    nullptr,
                    D3D_COMPILE_STANDARD_FILE_INCLUDE,
                    entryPoint,
                    "cs_5_0",
                    D3DCOMPILE_ENABLE_STRICTNESS |
                        D3DCOMPILE_OPTIMIZATION_LEVEL3,
                    0u,
                    &byteCode,
                    &errors);
                if (FAILED(lastResult))
                {
                    if (errors && errors->GetBufferPointer())
                    {
                        const std::string message(
                            static_cast<const char*>(
                                errors->GetBufferPointer()),
                            errors->GetBufferSize());
                        errorText = Utf8ToWide(message);
                    }
                    continue;
                }

                lastResult = device->CreateComputeShader(
                    byteCode->GetBufferPointer(),
                    byteCode->GetBufferSize(),
                    nullptr,
                    &destination);
                if (SUCCEEDED(lastResult))
                {
                    return true;
                }
            }

            if (errorText.empty())
            {
                std::wstringstream stream;
                stream
                    << L"Novelty 01 render-cache shader compilation failed. "
                    << L"HRESULT=0x" << std::hex
                    << static_cast<unsigned long>(lastResult)
                    << L"\r\nTried:\r\n" << tried.str();
                errorText = stream.str();
            }
            return false;
        }
    }

    struct GpuHairViewer::Novelty01RenderCacheState
    {
        StructuredBuffer renderPositions;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> copyGuidesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> copyFollowersCS;
        uint32_t totalPointCount = 0u;
        uint32_t observedSimulationResetVersion = 0u;
        uint32_t observedTopologyVersion = 0u;
        bool initialized = false;
        bool ready = false;
    };

    namespace
    {
        using N01RenderCachePointer =
            std::shared_ptr<GpuHairViewer::Novelty01RenderCacheState>;

        std::unordered_map<GpuHairViewer*, N01RenderCachePointer>&
        N01RenderCacheInstances()
        {
            static std::unordered_map<
                GpuHairViewer*,
                N01RenderCachePointer> instances;
            return instances;
        }

        N01RenderCachePointer N01FindRenderCache(GpuHairViewer* viewer)
        {
            auto& instances = N01RenderCacheInstances();
            const auto found = instances.find(viewer);
            return found != instances.end()
                ? found->second
                : N01RenderCachePointer{};
        }
    }

    bool GpuHairViewer::EnsureNovelty01RenderCache()
    {
        if (m_experimentCase.methodId !=
                ExperimentMethodId::P_IntegratedSegmentGroup ||
            !N01EnvironmentEnabled(
                L"HAIRGPU_N01_RENDER_DIRTY_CACHE",
                true) ||
            !m_device ||
            !m_context ||
            m_currentBufferIndex >= 2u)
        {
            return false;
        }

        const N01RuntimePointer runtime = N01FindRuntime(this);
        if (!runtime || !runtime->ready || !runtime->renderDirtyRanges.srv)
        {
            return false;
        }

        const uint64_t expectedPointCount64 =
            static_cast<uint64_t>(m_asset.strands.size()) *
            static_cast<uint64_t>(m_asset.particlesPerStrand);
        if (expectedPointCount64 == 0u ||
            expectedPointCount64 >
                static_cast<uint64_t>((std::numeric_limits<UINT>::max)()))
        {
            SetFatalExperimentFailure(
                1410u,
                L"Novelty 01 render cache has an invalid point count.");
            return false;
        }
        const uint32_t expectedPointCount =
            static_cast<uint32_t>(expectedPointCount64);

        N01RenderCachePointer existing = N01FindRenderCache(this);
        if (existing &&
            existing->ready &&
            existing->totalPointCount == expectedPointCount &&
            existing->observedSimulationResetVersion ==
                m_simulationResetVersion &&
            existing->observedTopologyVersion ==
                m_asset.segmentGroupTopology.topologyVersion)
        {
            return true;
        }

        N01RenderCacheInstances().erase(this);
        std::shared_ptr<Novelty01RenderCacheState> state =
            std::make_shared<Novelty01RenderCacheState>();

        std::vector<GpuParticle> initial(expectedPointCount);
        if (m_initialParticles.size() == expectedPointCount)
        {
            initial = m_initialParticles;
        }
        state->renderPositions = CreateStructuredBuffer(
            sizeof(GpuParticle),
            expectedPointCount,
            initial.data(),
            true);

        D3D11_BUFFER_DESC constantDesc = {};
        constantDesc.ByteWidth = sizeof(N01RenderCacheConstants);
        constantDesc.Usage = D3D11_USAGE_DEFAULT;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (!state->renderPositions.buffer ||
            !state->renderPositions.srv ||
            !state->renderPositions.uav ||
            FAILED(m_device->CreateBuffer(
                &constantDesc,
                nullptr,
                &state->constants)))
        {
            SetFatalExperimentFailure(
                1411u,
                L"Novelty 01 failed to create persistent render-cache resources.");
            return false;
        }

        std::wstring shaderError;
        if (!N01CompileRenderCacheShader(
                m_device.Get(),
                "N01CopyGuideRenderCacheCS",
                state->copyGuidesCS,
                shaderError) ||
            !N01CompileRenderCacheShader(
                m_device.Get(),
                "N01CopyFollowerRenderCacheCS",
                state->copyFollowersCS,
                shaderError))
        {
            SetFatalExperimentFailure(1412u, shaderError);
            return false;
        }

        state->totalPointCount = expectedPointCount;
        state->observedSimulationResetVersion = m_simulationResetVersion;
        state->observedTopologyVersion =
            m_asset.segmentGroupTopology.topologyVersion;
        state->ready = true;
        N01RenderCacheInstances()[this] = state;
        return true;
    }

    void GpuHairViewer::ConfigureNovelty01FormalTelemetryOutput()
    {
        const N01RuntimePointer runtime = N01FindRuntime(this);
        if (!runtime || !runtime->formalTelemetryEnabled)
        {
            return;
        }

        if (runtime->formalTelemetryDirectory.empty())
        {
            std::wstring directory =
                GetEnvironmentVariableString(
                    L"HAIRGPU_N01_TELEMETRY_DIR");
            if (directory.empty())
            {
                directory =
                    GetEnvironmentVariableString(
                        L"HAIRGPU_TELEMETRY_DIR");
            }
            if (!directory.empty())
            {
                runtime->formalTelemetryDirectory =
                    std::filesystem::path(directory);
            }
        }

        if (runtime->formalTelemetryRunName.empty())
        {
            std::wstring runName =
                GetEnvironmentVariableString(
                    L"HAIRGPU_TELEMETRY_RUN_NAME");
            if (runName.empty())
            {
                runName = m_experimentCase.caseId;
            }
            runtime->formalTelemetryRunName = runName;
        }
    }

    void GpuHairViewer::UpdateNovelty01RenderCache()
    {
        if (!EnsureNovelty01RenderCache())
        {
            return;
        }

        const N01RuntimePointer runtime = N01FindRuntime(this);
        const N01RenderCachePointer cache = N01FindRenderCache(this);
        if (!runtime || !cache || !cache->ready)
        {
            return;
        }

        ID3D11ShaderResourceView* nullVsSrvs[4] = {};
        m_context->VSSetShaderResources(0u, 4u, nullVsSrvs);

        const bool renderRangeEnabled =
            (runtime->featureMask &
             hairgpu::novelty01::Feature_RenderDirtyRange) != 0u;
        const bool mustCopyAll =
            !cache->initialized ||
            !renderRangeEnabled ||
            runtime->lastFrameUsedFullDynamic ||
            runtime->latestCounters.Get(
                hairgpu::novelty01::Counter_RenderFallback) != 0u ||
            runtime->latestCounters.Get(
                hairgpu::novelty01::Counter_DependencyFallback) != 0u;

        if (mustCopyAll)
        {
            m_context->CopyResource(
                cache->renderPositions.buffer.Get(),
                m_positions[m_currentBufferIndex].buffer.Get());
            cache->initialized = true;
            return;
        }

        N01RenderCacheConstants constants = {};
        constants.guideCount = m_asset.guideStrandCount;
        constants.followerCount = m_asset.followerStrandCount;
        constants.particlesPerStrand = m_asset.particlesPerStrand;
        constants.totalPointCount = cache->totalPointCount;
        m_context->UpdateSubresource(
            cache->constants.Get(),
            0u,
            nullptr,
            &constants,
            0u,
            0u);

        ID3D11Buffer* constantBuffer = cache->constants.Get();
        m_context->CSSetConstantBuffers(
            0u,
            1u,
            &constantBuffer);

        ID3D11ShaderResourceView* guideSrvs[2] =
        {
            m_positions[m_currentBufferIndex].srv.Get(),
            nullptr
        };
        ID3D11UnorderedAccessView* cacheUav =
            cache->renderPositions.uav.Get();
        m_context->CSSetShaderResources(0u, 2u, guideSrvs);
        m_context->CSSetUnorderedAccessViews(
            0u,
            1u,
            &cacheUav,
            nullptr);
        m_context->CSSetShader(
            cache->copyGuidesCS.Get(),
            nullptr,
            0u);
        const uint32_t guidePointCount =
            m_asset.guideStrandCount *
            m_asset.particlesPerStrand;
        m_context->Dispatch(
            N01DispatchGroups(guidePointCount, 256u),
            1u,
            1u);
        N01UnbindCompute(m_context.Get());

        ID3D11Buffer* followerConstantBuffer =
            cache->constants.Get();
        m_context->CSSetConstantBuffers(
            0u,
            1u,
            &followerConstantBuffer);
        ID3D11ShaderResourceView* followerSrvs[2] =
        {
            m_positions[m_currentBufferIndex].srv.Get(),
            runtime->renderDirtyRanges.srv.Get()
        };
        cacheUav = cache->renderPositions.uav.Get();
        m_context->CSSetShaderResources(
            0u,
            2u,
            followerSrvs);
        m_context->CSSetUnorderedAccessViews(
            0u,
            1u,
            &cacheUav,
            nullptr);
        m_context->CSSetShader(
            cache->copyFollowersCS.Get(),
            nullptr,
            0u);
        m_context->Dispatch(
            m_asset.followerStrandCount,
            1u,
            1u);
        N01UnbindCompute(m_context.Get());
        cache->initialized = true;
    }

    void GpuHairViewer::ReleaseNovelty01RenderCache()
    {
        N01RenderCacheInstances().erase(this);
    }

    void GpuHairViewer::StepNovelty01SingleFrame()
    {
        LARGE_INTEGER begin = {};
        LARGE_INTEGER end = {};
        QueryPerformanceCounter(&begin);

        ExecuteNovelty01ExperimentTick(
            m_simConfig.fixedTimeStep);
        ConfigureNovelty01FormalTelemetryOutput();

        if (m_experimentCase.methodId ==
            ExperimentMethodId::P_IntegratedSegmentGroup)
        {
            BeginGpuPassTiming(GpuPassId::RenderPreparation);
            UpdateNovelty01RenderCache();
            EndGpuPassTiming(GpuPassId::RenderPreparation);
        }

        QueryPerformanceCounter(&end);
        m_elapsedSeconds += m_simConfig.fixedTimeStep;
        m_lastSimulationStepMilliseconds =
            static_cast<float>(end.QuadPart - begin.QuadPart) *
            1000.0f /
            static_cast<float>(m_qpcFrequency.QuadPart);

        PerformanceSample sample = BuildPerformanceSample(
            m_simConfig.fixedTimeStep,
            m_simConfig.fixedTimeStep > 1.0e-6f
                ? 1.0f / m_simConfig.fixedTimeStep
                : 0.0f);
        sample.globalFrameIndex = m_globalFrameIndex++;
        sample.measuredFrameIndex = m_measuredFrameIndex++;
        m_performanceSamples.push_back(sample);
        if (m_performanceSamples.size() > 12000u)
        {
            m_performanceSamples.erase(
                m_performanceSamples.begin(),
                m_performanceSamples.begin() +
                    static_cast<std::ptrdiff_t>(
                        m_performanceSamples.size() -
                        12000u));
        }
        UpdateStatsLabel();
    }

    void GpuHairViewer::Render()
    {
        if (m_experimentCase.methodId !=
            ExperimentMethodId::P_IntegratedSegmentGroup)
        {
            RenderLegacy();
            return;
        }

        const N01RenderCachePointer cache =
            N01FindRenderCache(this);
        if (!cache ||
            !cache->ready ||
            !cache->initialized ||
            !cache->renderPositions.srv)
        {
            RenderLegacy();
            return;
        }

        std::swap(
            m_positions[m_currentBufferIndex],
            cache->renderPositions);
        RenderLegacy();
        std::swap(
            m_positions[m_currentBufferIndex],
            cache->renderPositions);
    }

    void GpuHairViewer::ShutdownNovelty01Routed()
    {
        ReleaseNovelty01RenderCache();
        ShutdownNovelty01RoutedCore();
    }
