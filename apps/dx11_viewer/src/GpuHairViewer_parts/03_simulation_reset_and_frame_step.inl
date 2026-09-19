// GpuHairViewer.cpp ordered part 03: simulation_reset_and_frame_step
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

UpdateScalpOverlayVertexBuffer();
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
            errorText = L"Invalid segment-group shader compile request.";
            return false;
        }

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    #if defined(_DEBUG)
        // Keep shader symbols for diagnostics, but never disable optimizer passes on
        // the fixed 12.8-million-particle workload.
        flags |= D3DCOMPILE_DEBUG;
    #endif

        wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
        const wchar_t* fileName = L"HairSegmentGroupRuntime.hlsl";

        std::vector<std::filesystem::path> candidates;
        auto pushCandidate = [&candidates](const std::filesystem::path& candidate)
        {
            if (candidate.empty()) return;
            const std::filesystem::path normalized = candidate.lexically_normal();
            if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end())
                candidates.push_back(normalized);
        };
        pushCandidate(std::filesystem::path(fileName));
        pushCandidate(std::filesystem::path(HAIRGPU_VIEWER_SHADER_DIR) / fileName);
        pushCandidate(exeDir / fileName);
        pushCandidate(exeDir / L"shaders" / fileName);
        pushCandidate(std::filesystem::current_path() / fileName);
        pushCandidate(std::filesystem::current_path() / L"shaders" / fileName);
        std::filesystem::path searchRoot = exeDir;
        for (int i = 0; i < 6; ++i)
        {
            pushCandidate(searchRoot / L"apps" / L"dx11_viewer" / L"shaders" / fileName);
            pushCandidate(searchRoot / L"dx11_viewer" / L"shaders" / fileName);
            if (!searchRoot.has_parent_path()) break;
            const std::filesystem::path parent = searchRoot.parent_path();
            if (parent == searchRoot) break;
            searchRoot = parent;
        }

        const char* bindingValue = compactionBindings ? "1" : "0";
        const D3D_SHADER_MACRO macros[] = {
            { "HAIRGPU_COMPACTION_BINDINGS", bindingValue },
            { nullptr, nullptr }
        };

        HRESULT lastResult = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;
        std::wstringstream tried;
        std::filesystem::path compiledPath;
        for (const std::filesystem::path& candidate : candidates)
        {
            const bool exists = std::filesystem::exists(candidate);
            tried << candidate.c_str() << (exists ? L"  [exists]" : L"  [missing]") << L"\r\n";
            if (!exists) continue;

            std::ifstream input(candidate, std::ios::binary);
            if (!input)
            {
                lastResult = HRESULT_FROM_WIN32(ERROR_READ_FAULT);
                continue;
            }
            const std::string source{
                std::istreambuf_iterator<char>{input},
                std::istreambuf_iterator<char>{}};
            shaderBlob.Reset();
            errorBlob.Reset();
            compiledPath = candidate;
            const std::string sourceName = candidate.string();
            lastResult = D3DCompile(
                source.data(), source.size(), sourceName.c_str(), macros, nullptr,
                entryPoint, "cs_5_0", flags, 0u, &shaderBlob, &errorBlob);
            if (SUCCEEDED(lastResult)) break;
        }

        if (FAILED(lastResult))
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
                message << L"Segment-group shader compilation failed. HRESULT=0x"
                        << std::hex << std::uppercase << static_cast<unsigned long>(lastResult);
                errorText = message.str();
            }
            if (!compiledPath.empty())
            {
                errorText += L"\r\nShader source path: ";
                errorText += compiledPath.wstring();
            }
            errorText += L"\r\nTried paths:\r\n";
            errorText += tried.str();
            return false;
        }

        *blobOut = shaderBlob.Detach();
        return true;
    }

    bool GpuHairViewer::LoadShaders()
    {
        std::wstring errorText;
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> gsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> csBlob;

        if (!CompileShaderFromFile(L"HairParticle.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairParticle.hlsl", "GSPointMain", "gs_5_0", &gsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_particleVS)) ||
            FAILED(m_device->CreateGeometryShader(gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), nullptr, &m_particlePointGS)))
        {
            return false;
        }
        if (!CompileShaderFromFile(L"HairParticle.hlsl", "GSBillboardMain", "gs_5_0", &gsBlob, errorText) ||
            !CompileShaderFromFile(L"HairParticle.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        if (FAILED(m_device->CreateGeometryShader(gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), nullptr, &m_particleBillboardGS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_particlePS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"HairStrandInstanced.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairStrandInstanced.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Instanced hair shader compile failed", MB_ICONERROR);
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_ribbonVS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ribbonPS)))
        {
            return false;
        }
        if (!CompileShaderFromFile(L"HairStrandInstanced.hlsl", "PSOitMain", "ps_5_0", &psBlob, errorText) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ribbonOitPS)))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Hair OIT shader compile failed", MB_ICONERROR);
            return false;
        }
        if (!CompileShaderFromFile(L"HairStrandInstanced.hlsl", "VSCompositeMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairStrandInstanced.hlsl", "PSCompositeMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Hair OIT composite shader compile failed", MB_ICONERROR);
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_hairOitCompositeVS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_hairOitCompositePS)))
        {
            return false;
        }
        m_ribbonGS.Reset();

        // Particle-only runtime:
        // keep startup dependencies limited to the shaders that are still used by the viewer.
        if (!CompileShaderFromFile(L"DebugLine.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"DebugLine.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_debugLineVS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_debugLinePS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"ScalpMesh.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"ScalpMesh.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_scalpVS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_scalpPS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"HairCpuMesh.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairCpuMesh.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_hairMeshVS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_hairMeshPS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"HairSim.hlsl", "RootBindCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_rootBindCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "IntegrateCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_integrateCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "ClearConstraintLambdasCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_clearConstraintLambdasCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "DistanceCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_distanceCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "StabilityGuardCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_stabilityGuardCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BendCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_bendCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "CollisionCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_collisionCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "ClearGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_clearGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BuildGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "ClearHashGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_clearHashGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BuildHashGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildHashGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "ClearSortedRangesCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_clearSortedRangesCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BuildSortedEntriesCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildSortedEntriesCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BitonicSortCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_bitonicSortCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BuildSortedRangesCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildSortedRangesCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "SelfCollisionCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_selfCollisionCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "FollowCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_followCS)) ||
            !CompileShaderFromFile(L"HairIndirectArgs.hlsl", "CompactFollowerDispatchCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_compactFollowerDispatchCS)) ||
            !CompileShaderFromFile(L"HairIndirectArgs.hlsl", "BuildFollowDispatchArgsCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildFollowDispatchArgsCS)) ||
            !CompileShaderFromFile(L"HairIndirectArgs.hlsl", "CompactGuideDispatchCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_compactGuideDispatchCS)) ||
            !CompileShaderFromFile(L"HairIndirectArgs.hlsl", "BuildGuideDispatchArgsCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildGuideDispatchArgsCS)) ||
            !CompileShaderFromFile(L"HairIndirectArgs.hlsl", "BuildGuidePointDispatchArgsCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildGuidePointDispatchArgsCS)))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        const bool requireFormalSegmentGroupShaders =
            IsExperimentModeEnabled() &&
            m_experimentCase.strictValidationEnabled &&
            (m_experimentCase.methodId == ExperimentMethodId::B3_SegmentThresholdSleep ||
             m_experimentCase.methodId == ExperimentMethodId::B4_SegmentHysteresisSleep ||
             m_experimentCase.methodId == ExperimentMethodId::B5_SegmentCache ||
             m_experimentCase.methodId == ExperimentMethodId::B6_SegmentGpuCompaction ||
             m_experimentCase.methodId == ExperimentMethodId::B8_ExternalStateArray ||
             m_experimentCase.methodId == ExperimentMethodId::P_IntegratedSegmentGroup);

        auto loadSegmentGroupShader =
            [&](const char* entryPoint, ComPtr<ID3D11ComputeShader>& destination) -> bool
        {
            ComPtr<ID3DBlob> blob;
            std::wstring localError;
            const bool compactionBindings =
                std::strcmp(entryPoint, "SegmentGroupCompactDynamicCS") == 0 ||
                std::strcmp(entryPoint, "SegmentGroupCompactStaticCS") == 0 ||
                std::strcmp(entryPoint, "SegmentGroupBuildDispatchArgsCS") == 0;
            if (!CompileSegmentGroupShaderFromFile(
                    entryPoint,
                    compactionBindings,
                    &blob,
                    localError) ||
                FAILED(m_device->CreateComputeShader(
                    blob->GetBufferPointer(),
                    blob->GetBufferSize(),
                    nullptr,
                    &destination)))
            {
                errorText = localError.empty()
                    ? std::wstring(L"Failed to create segment-group shader: ") +
                        std::wstring(entryPoint, entryPoint + std::strlen(entryPoint))
                    : localError;
                destination.Reset();
                return false;
            }
            return true;
        };

        bool segmentGroupShadersReady = true;
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupBlockClassifyCS", m_segmentGroupBlockClassifyCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupClassifyCS", m_segmentGroupClassifyCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupPredictiveWakeCS", m_segmentGroupPredictiveWakeCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupCompactDynamicCS", m_segmentGroupCompactDynamicCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupCompactStaticCS", m_segmentGroupCompactStaticCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupBuildDispatchArgsCS", m_segmentGroupBuildDispatchArgsCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupStaticTransformCS", m_segmentGroupStaticTransformCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupDynamicSolveCS", m_segmentGroupDynamicSolveCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupVelocityRecoveryCS", m_segmentGroupVelocityRecoveryCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupRepairSolveCS", m_segmentGroupRepairSolveCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupBoundaryRepairCS", m_segmentGroupBoundaryRepairCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupCacheUpdateCS", m_segmentGroupCacheUpdateCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupWatchdogCS", m_segmentGroupWatchdogCS);
        segmentGroupShadersReady &= loadSegmentGroupShader("SegmentGroupTelemetryReduceCS", m_segmentGroupTelemetryReduceCS);
        if (!segmentGroupShadersReady && requireFormalSegmentGroupShaders)
        {
            SetFatalExperimentFailure(1101u, L"Required HairSegmentGroupRuntime.hlsl entry point failed to compile.");
            MessageBoxW(m_hwnd, errorText.c_str(), L"Formal experiment shader compile failed", MB_ICONERROR);
            return false;
        }

        ComPtr<ID3DBlob> debugVsBlob;
        if (!CompileShaderFromFile(L"DebugLine.hlsl", "VSMain", "vs_5_0", &debugVsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        const D3D11_INPUT_ELEMENT_DESC debugLayout[] = {
            { "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(DebugLineTemplateVertex, endpointId), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(m_device->CreateInputLayout(debugLayout, 1, debugVsBlob->GetBufferPointer(), debugVsBlob->GetBufferSize(), &m_debugInputLayout)))
        {
            return false;
        }

        m_segmentInputLayout.Reset();

        ComPtr<ID3DBlob> scalpVsBlob;
        if (!CompileShaderFromFile(L"ScalpMesh.hlsl", "VSMain", "vs_5_0", &scalpVsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        const D3D11_INPUT_ELEMENT_DESC scalpLayout[] = {
            { "TEXCOORD", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(ScalpVertex, scalpIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ScalpVertex, nx), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ScalpVertex, r), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(m_device->CreateInputLayout(scalpLayout, 3, scalpVsBlob->GetBufferPointer(), scalpVsBlob->GetBufferSize(), &m_scalpInputLayout)))
        {
            return false;
        }

        ComPtr<ID3DBlob> hairMeshVsBlob;
        if (!CompileShaderFromFile(L"HairCpuMesh.hlsl", "VSMain", "vs_5_0", &hairMeshVsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        const D3D11_INPUT_ELEMENT_DESC hairMeshLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(HairMeshVertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(HairMeshVertex, r), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(m_device->CreateInputLayout(hairMeshLayout, 2, hairMeshVsBlob->GetBufferPointer(), hairMeshVsBlob->GetBufferSize(), &m_hairMeshInputLayout)))
        {
            return false;
        }

        return true;
    }

    bool GpuHairViewer::UpdateSegmentGroupRuntimeConstants(float dt)
    {
        if (!m_context || !m_segmentGroupRuntimeConstants)
        {
            if (IsExperimentModeEnabled())
            {
                SetFatalExperimentFailure(1102u, L"Segment-group runtime constant buffer is unavailable.");
            }
            return false;
        }

        SegmentGroupRuntimeConstants constants = {};
        constants.groupCount = m_segmentGroupCount;
        constants.blockCount = m_segmentGroupBlockCount;
        constants.frameIndex = m_globalFrameIndex;
        constants.groupsPerBlock = std::max<uint32_t>(m_segmentGroupsPerBlock, 1u);
        constants.repairVerificationHoldFrames = 12u;
        constants.deltaTime = dt;
        constants.motionWakeThreshold = 0.02f;
        constants.collisionWakeThreshold = 0.10f;
        constants.staticMotionThreshold = 0.005f;
        constants.lengthErrorThreshold = 0.02f;
        constants.boundaryErrorThresholdMm = 2.0f;
        constants.velocityRecoveryBlend = 0.75f;
        constants.positionRecoveryBlend = 0.35f;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(
                m_segmentGroupRuntimeConstants.Get(),
                0u,
                D3D11_MAP_WRITE_DISCARD,
                0u,
                &mapped)))
        {
            SetFatalExperimentFailure(1103u, L"Failed to map segment-group runtime constants.");
            return false;
        }
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        m_context->Unmap(m_segmentGroupRuntimeConstants.Get(), 0u);
        ID3D11Buffer* constantBuffers[] = { m_segmentGroupRuntimeConstants.Get() };
        m_context->CSSetConstantBuffers(0u, 1u, constantBuffers);
        return true;
    }

    void GpuHairViewer::StepSimulation(float dt)
    {
        if (!m_context || !m_simConstantsHair || m_asset.strands.empty() || m_asset.particlesPerStrand == 0u)
        {
            return;
        }

        const uint32_t totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
        const uint32_t guideStrandCount = m_asset.guideStrandCount;
        const uint32_t followerStrandCount = m_asset.followerStrandCount;
        const uint32_t particlesPerStrand = m_asset.particlesPerStrand;
        const uint32_t totalParticleCount = static_cast<uint32_t>(std::min<size_t>(
            m_asset.restPoints.size(),
            static_cast<size_t>(totalStrandCount) * static_cast<size_t>(particlesPerStrand)));

        if (totalParticleCount == 0u)
        {
            return;
        }

        if (m_enableActiveSetRuntime)
        {
            SyncActiveSetGpuBuffers();
        }

        UpdateAnimatedScalp(m_elapsedSeconds);

        const uint32_t previousBufferIndex = m_currentBufferIndex;
        const uint32_t outputBufferIndex = 1u - m_currentBufferIndex;

        const uint32_t gridCellCount = std::max<uint32_t>(
            m_simConfig.selfCollisionGridDimX *
            m_simConfig.selfCollisionGridDimY *
            m_simConfig.selfCollisionGridDimZ,
            1u);

        const uint32_t totalGuideParticleCount =
            guideStrandCount * particlesPerStrand;

        const bool useActiveDispatch =
            m_enableActiveSetRuntime &&
            m_activeSetPlan.summary.selectedGuideCount > 0u;

        uint32_t compactedGuidePointCount = 0u;
        if (useActiveDispatch)
        {
            for (uint32_t guideIndex : m_activeSetPlan.selectedGuideStrandIndices)
            {
                if (guideIndex < m_asset.strands.size())
                {
                    const uint32_t pointCount = m_asset.strands[guideIndex].pointCount;
                    if (pointCount > 1u)
                    {
                        compactedGuidePointCount += pointCount - 1u;
                    }
                }
            }
        }

        const uint32_t guideDispatchCount = useActiveDispatch
            ? static_cast<uint32_t>(m_activeSetPlan.selectedGuideStrandIndices.size())
            : guideStrandCount;

        const uint32_t guideParticleDispatchCount = useActiveDispatch
            ? compactedGuidePointCount
            : totalGuideParticleCount;

        // Pre-novelty correctness baseline: all 96,000 followers are rebuilt
        // every simulation tick. Guide compaction may still be evaluated, but
        // follower temporal skipping is not allowed until a coherent cached-pose
        // transform exists and is validated as part of the novelty experiment.
        const uint32_t followerDispatchCount = followerStrandCount;

        const uint32_t sortedEntryCount =
            std::max<uint32_t>(m_sortedBroadphasePaddedCount, 1u);

        constexpr uint32_t kMaxD3D11DispatchGroupsX = 65535u;
        constexpr uint32_t kHairThreadsPerGroup = 64u;
        constexpr uint32_t kBitonicThreadsPerGroup = 256u;
        constexpr uint32_t kMaxBitonicItems = kMaxD3D11DispatchGroupsX * kBitonicThreadsPerGroup;

        // The sorted/bitonic path is comparison-only. Runtime self collision uses the
        // bounded spatial-hash path to avoid hundreds of sort dispatches per iteration.
        uint32_t effectiveBroadphaseMode = 1u;
        uint32_t effectiveSortedEntryCount = sortedEntryCount;

        // BitonicSortCS cannot be tiled by DispatchBaseIndex because partner = index ^ BitonicJ
        // depends on the global sort domain. If sorted broadphase is too large for one D3D11
        // dispatch, fall back to hash broadphase for this frame.
        if (effectiveBroadphaseMode == 3u && sortedEntryCount > kMaxBitonicItems)
        {
            effectiveBroadphaseMode = 1u;
            effectiveSortedEntryCount = 1u;
            m_noveltyTelemetry.overflowCount += 1u;
            m_noveltyTelemetry.fallbackCount += 1u;
        }

        auto updateHairConstants = [&](uint32_t dispatchBaseIndex)
            {
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if (SUCCEEDED(m_context->Map(m_simConstantsHair.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                {
                    SimConstantsHair constants = {};
                    FillHairSimConstants(constants, dt);
                    constants.dispatchBaseIndex = dispatchBaseIndex;
                    // Interactive/F5 runs may recover a catastrophically corrupted guide.
                    // Formal runs must expose the failure instead of silently repairing it.
                    constants.enableCatastrophicRecovery = IsExperimentModeEnabled() ? 0u : 1u;
                    constants.broadphaseMode = effectiveBroadphaseMode;
                    constants.sortedPaddedEntryCount = std::max<uint32_t>(effectiveSortedEntryCount, 1u);
                    std::memcpy(mapped.pData, &constants, sizeof(constants));
                    m_context->Unmap(m_simConstantsHair.Get(), 0);
                }

                ID3D11Buffer* constantBuffers[] = {
                    m_simConstantsHair.Get()
                };
                m_context->CSSetConstantBuffers(0, 1, constantBuffers);
            };

        auto bindCommonSrvs = [&]()
            {
                ID3D11ShaderResourceView* srvs[] = {
                    m_positions[previousBufferIndex].srv.Get(),             // t0 InputPositions
                    m_previousPositions[previousBufferIndex].srv.Get(),     // t1 InputPreviousPositions
                    m_restPositions.srv.Get(),                             // t2 RestPositions
                    m_followBuffer.srv.Get(),                              // t3 FollowStrands
                    m_scalpPositions.srv.Get(),                            // t4 ScalpPositions
                    m_scalpTrianglesBuffer.srv.Get(),                      // t5 ScalpTriangles
                    m_rootBindingsBuffer.srv.Get(),                        // t6 RootBindings
                    m_compactedFollowerIndicesBuffer.srv.Get(),            // t7 CompactedFollowerIndices
                    m_followerTierAssignmentsBuffer.srv.Get(),             // t8 FollowerTierAssignments
                    m_compactedGuideIndicesBuffer.srv.Get(),               // t9 CompactedGuideIndices
                    m_compactedGuideCounterBuffer.srv.Get(),               // t10 GuideCompactionCountersInSim
                    m_compactedFollowerCounterBuffer.srv.Get(),            // t11 FollowerCompactionCountersInSim
                    m_sdfValuesBuffer.srv.Get()                            // t12 SdfValues
                };
                m_context->CSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
            };

        auto bindPrimaryUavs = [&]()
            {
                ID3D11UnorderedAccessView* uavs[] = {
                    m_positions[outputBufferIndex].uav.Get(),              // u0 OutputPositions
                    m_previousPositions[outputBufferIndex].uav.Get(),      // u1 OutputPreviousPositions
                    m_distanceLambdaBuffer.uav.Get(),                      // u2 DistanceLambdas
                    m_bendLambdaBuffer.uav.Get(),                          // u3 BendLambdas
                    m_gridCellCountsBuffer.uav.Get(),                      // u4 GridCellCounts
                    m_gridCellIndicesBuffer.uav.Get(),                     // u5 GridCellIndices
                    m_gridHashHeadsBuffer.uav.Get(),                       // u6 GridHashHeads
                    m_gridParticleNextBuffer.uav.Get()                     // u7 GridParticleNext
                };
                m_context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(uavs)), uavs, nullptr);
            };

        auto bindSortedBroadphaseUavs = [&]()
            {
                ID3D11UnorderedAccessView* uavs[] = {
                    m_positions[outputBufferIndex].uav.Get(),              // u0 OutputPositions
                    m_previousPositions[outputBufferIndex].uav.Get(),      // u1 OutputPreviousPositions
                    m_distanceLambdaBuffer.uav.Get(),                      // u2 DistanceLambdas
                    m_bendLambdaBuffer.uav.Get(),                          // u3 BendLambdas
                    m_sortedKeysBuffer.uav.Get(),                          // u4 GridCellCounts as sorted keys
                    m_sortedParticleIndicesBuffer.uav.Get(),               // u5 GridCellIndices as sorted particle indices
                    m_sortedCellStartsBuffer.uav.Get(),                    // u6 GridHashHeads as sorted cell starts
                    m_sortedCellEndsBuffer.uav.Get()                       // u7 GridParticleNext as sorted cell ends
                };
                m_context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(uavs)), uavs, nullptr);
            };

        auto dispatchTiled = [&](ID3D11ComputeShader* shader, uint32_t itemCount, uint32_t threadsPerGroup)
            {
                if (!shader || itemCount == 0u || threadsPerGroup == 0u)
                {
                    return;
                }

                const uint32_t maxItemsPerDispatch = kMaxD3D11DispatchGroupsX * threadsPerGroup;
                m_context->CSSetShader(shader, nullptr, 0);

                for (uint32_t baseIndex = 0u; baseIndex < itemCount; baseIndex += maxItemsPerDispatch)
                {
                    const uint32_t remaining = itemCount - baseIndex;
                    const uint32_t tileItemCount = std::min<uint32_t>(remaining, maxItemsPerDispatch);
                    const uint32_t groupCount = DispatchGroups(tileItemCount, threadsPerGroup);

                    updateHairConstants(baseIndex);
                    m_context->Dispatch(groupCount, 1u, 1u);
                }

                updateHairConstants(0u);
            };

        auto dispatchUntiledChecked = [&](ID3D11ComputeShader* shader, uint32_t itemCount, uint32_t threadsPerGroup)
            {
                if (!shader || itemCount == 0u || threadsPerGroup == 0u)
                {
                    return;
                }

                const uint32_t groupCount = DispatchGroups(itemCount, threadsPerGroup);
                if (groupCount > kMaxD3D11DispatchGroupsX)
                {
                    m_noveltyTelemetry.overflowCount += 1u;
                    return;
                }

                updateHairConstants(0u);
                m_context->CSSetShader(shader, nullptr, 0);
                m_context->Dispatch(groupCount, 1u, 1u);
            };

        auto unbindComputeState = [&]()
            {
                ID3D11ShaderResourceView* nullSrvs[13] = {};
                ID3D11UnorderedAccessView* nullUavs[8] = {};
                ID3D11Buffer* nullConstantBuffers[2] = {};

                m_context->CSSetShader(nullptr, nullptr, 0);
                m_context->CSSetShaderResources(0, 13, nullSrvs);
                m_context->CSSetUnorderedAccessViews(0, 8, nullUavs, nullptr);
                m_context->CSSetConstantBuffers(0, 2, nullConstantBuffers);
            };

        // IntegrateCS writes every output point; followers are pass-through until FollowCS overwrites active points.
        bindCommonSrvs();
        bindPrimaryUavs();
        updateHairConstants(0u);

        dispatchTiled(m_rootBindCS.Get(), totalStrandCount, kHairThreadsPerGroup);
        dispatchTiled(m_integrateCS.Get(), totalParticleCount, kHairThreadsPerGroup);

        // XPBD multipliers are warm-started only inside one simulation time step.
        // Carrying them across frames accumulates directional bias and was the main
        // cause of the long-run rectangular/slab-shaped guide failure.
        const uint32_t distanceConstraintCount = guideStrandCount *
            ((particlesPerStrand > 1u) ? (particlesPerStrand - 1u) : 0u);
        const uint32_t bendConstraintCount = guideStrandCount *
            ((particlesPerStrand > 2u) ? (particlesPerStrand - 2u) : 0u);
        bindPrimaryUavs();
        dispatchTiled(
            m_clearConstraintLambdasCS.Get(),
            std::max<uint32_t>(distanceConstraintCount, bendConstraintCount),
            kHairThreadsPerGroup);

        const uint32_t solverIterationCount = std::max<uint32_t>(m_simConfig.solverIterations, 1u);

        for (uint32_t iteration = 0u; iteration < solverIterationCount; ++iteration)
        {
            bindPrimaryUavs();

            dispatchTiled(m_distanceCS.Get(), guideDispatchCount, kHairThreadsPerGroup);
            dispatchTiled(m_bendCS.Get(), guideDispatchCount, kHairThreadsPerGroup);
            dispatchTiled(m_collisionCS.Get(), guideParticleDispatchCount, kHairThreadsPerGroup);

            if (m_enableSelfCollision && guideParticleDispatchCount > 0u)
            {
                if (m_enableGridSelfCollision)
                {
                    if (effectiveBroadphaseMode == 0u)
                    {
                        bindPrimaryUavs();
                        dispatchTiled(m_clearGridCS.Get(), gridCellCount, kHairThreadsPerGroup);
                        dispatchTiled(m_buildGridCS.Get(), guideParticleDispatchCount, kHairThreadsPerGroup);
                    }
                    else if (effectiveBroadphaseMode == 3u)
                    {
                        bindSortedBroadphaseUavs();

                        dispatchTiled(m_clearSortedRangesCS.Get(), gridCellCount, kHairThreadsPerGroup);
                        dispatchTiled(m_buildSortedEntriesCS.Get(), effectiveSortedEntryCount, kHairThreadsPerGroup);

                        if (m_bitonicConstants && effectiveSortedEntryCount <= kMaxBitonicItems)
                        {
                            ID3D11Buffer* bitonicConstantBuffers[] = {
                                m_bitonicConstants.Get()
                            };
                            m_context->CSSetConstantBuffers(1, 1, bitonicConstantBuffers);

                            for (uint32_t k = 2u; k <= effectiveSortedEntryCount && k != 0u; k <<= 1u)
                            {
                                for (uint32_t j = k >> 1u; j > 0u; j >>= 1u)
                                {
                                    BitonicConstants bitonicConstants = {};
                                    bitonicConstants.j = j;
                                    bitonicConstants.k = k;
                                    bitonicConstants.entryCount = effectiveSortedEntryCount;
                                    bitonicConstants._padding = 0u;
                                    m_context->UpdateSubresource(m_bitonicConstants.Get(), 0, nullptr, &bitonicConstants, 0, 0);

                                    dispatchUntiledChecked(m_bitonicSortCS.Get(), effectiveSortedEntryCount, kBitonicThreadsPerGroup);
                                }
                            }
                        }

                        dispatchTiled(m_buildSortedRangesCS.Get(), effectiveSortedEntryCount, kHairThreadsPerGroup);
                    }
                    else
                    {
                        bindPrimaryUavs();
                        const uint32_t hashClearCount = std::max<uint32_t>(
                            std::max<uint32_t>(m_simConfig.selfCollisionHashBucketCount, 1u),
                            totalGuideParticleCount);
                        dispatchTiled(m_clearHashGridCS.Get(), hashClearCount, kHairThreadsPerGroup);
                        dispatchTiled(m_buildHashGridCS.Get(), guideParticleDispatchCount, kHairThreadsPerGroup);
                    }
                }

                bindPrimaryUavs();
                dispatchTiled(m_selfCollisionCS.Get(), guideParticleDispatchCount, kHairThreadsPerGroup);
            }

        }

        // Final guide validation happens after collision/self-collision and before
        // one guide is replicated into its 24 followers. This both enforces authored
        // segment length and prevents one poisoned guide from becoming a dense slab.
        bindPrimaryUavs();
        dispatchTiled(m_stabilityGuardCS.Get(), guideDispatchCount, kHairThreadsPerGroup);

        // Followers consume only the final solved guide state. Running this inside every
        // solver iteration produced the same final dependency but repeated 96k x 128 work.
        bindPrimaryUavs();
        const uint32_t followerPointDispatchCount =
            followerDispatchCount * particlesPerStrand;
        constexpr uint32_t kFollowerPointThreadsPerGroup = 256u;
        BeginGpuPassTiming(GpuPassId::FollowerReconstruction);
        dispatchTiled(
            m_followCS.Get(),
            followerPointDispatchCount,
            kFollowerPointThreadsPerGroup);
        EndGpuPassTiming(GpuPassId::FollowerReconstruction);

        unbindComputeState();

        m_currentBufferIndex = outputBufferIndex;
        m_runtimeFollowerUpdatePhase += 1u;
    }
    void GpuHairViewer::ResetSimulation()
    {
        m_currentBufferIndex = 0;
        m_accumulator = 0.0f;
        m_simulationTicksThisFrame = 0u;
        m_droppedSimulationMilliseconds = 0.0f;
        m_renderFps = 0.0f;
        m_simulationHz = 0.0f;
        m_renderCadenceWindowValid = false;
        m_recentFrameIntervalsMilliseconds.fill(0.0f);
        m_recentFrameIntervalCount = 0u;
        m_recentFrameIntervalWriteIndex = 0u;
        m_recentFrameAverageMilliseconds = 0.0f;
        // Session truth is reset only when Run() starts. A user-triggered
        // simulation reset must not erase a freeze that already happened.
        m_recentEffectiveFps = 0.0f;
        m_recentOnePercentLowFps = 0.0f;
        m_recentFrameP95Milliseconds = 0.0f;
        m_recentFrameWorstMilliseconds = 0.0f;
        m_recentFrameStallCount100Milliseconds = 0u;
        m_observedStallCount100Milliseconds = 0ull;
        // Reset the four 204.8 MiB state buffers entirely on the GPU. The immutable rest
        // buffer already contains the exact initial pose, so CPU re-upload is redundant.
        m_context->CopyResource(m_positions[0].buffer.Get(), m_restPositions.buffer.Get());
        m_context->CopyResource(m_positions[1].buffer.Get(), m_restPositions.buffer.Get());
        m_context->CopyResource(m_previousPositions[0].buffer.Get(), m_restPositions.buffer.Get());
        m_context->CopyResource(m_previousPositions[1].buffer.Get(), m_restPositions.buffer.Get());
        if (!m_zeroDistanceLambdas.empty())
        {
            m_context->UpdateSubresource(m_distanceLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroDistanceLambdas.data(), 0, 0);
        }
        if (!m_zeroBendLambdas.empty())
        {
            m_context->UpdateSubresource(m_bendLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroBendLambdas.data(), 0, 0);
        }
        const UINT gridCellCount = std::max<UINT>(m_simConfig.selfCollisionGridDimX * m_simConfig.selfCollisionGridDimY * m_simConfig.selfCollisionGridDimZ, 1u);
        std::vector<uint32_t> zeroCellCounts(gridCellCount, 0u);
        std::vector<uint32_t> zeroCellIndices(std::max<UINT>(gridCellCount * m_simConfig.selfCollisionCellCapacity, 1u), 0xFFFFFFFFu);
        m_context->UpdateSubresource(m_gridCellCountsBuffer.buffer.Get(), 0, nullptr, zeroCellCounts.data(), 0, 0);
        m_context->UpdateSubresource(m_gridCellIndicesBuffer.buffer.Get(), 0, nullptr, zeroCellIndices.data(), 0, 0);
        const UINT hashBucketCount = std::max<UINT>(m_simConfig.selfCollisionHashBucketCount, 1u);
        std::vector<uint32_t> zeroHashHeads(hashBucketCount, 0xFFFFFFFFu);
        const UINT guideParticleCount = std::max<UINT>(
            m_asset.guideStrandCount * m_asset.particlesPerStrand,
            1u);
        std::vector<uint32_t> zeroParticleNext(guideParticleCount, 0xFFFFFFFFu);
        m_context->UpdateSubresource(m_gridHashHeadsBuffer.buffer.Get(), 0, nullptr, zeroHashHeads.data(), 0, 0);
        m_context->UpdateSubresource(m_gridParticleNextBuffer.buffer.Get(), 0, nullptr, zeroParticleNext.data(), 0, 0);
        std::vector<uint32_t> invalidSortedKeys(std::max<UINT>(m_sortedBroadphasePaddedCount, 1u), 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedIndices(std::max<UINT>(m_sortedBroadphasePaddedCount, 1u), 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedStarts(gridCellCount, 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedEnds(gridCellCount, 0xFFFFFFFFu);
        m_context->UpdateSubresource(m_sortedKeysBuffer.buffer.Get(), 0, nullptr, invalidSortedKeys.data(), 0, 0);
        m_context->UpdateSubresource(m_sortedParticleIndicesBuffer.buffer.Get(), 0, nullptr, invalidSortedIndices.data(), 0, 0);
        m_context->UpdateSubresource(m_sortedCellStartsBuffer.buffer.Get(), 0, nullptr, invalidSortedStarts.data(), 0, 0);
        m_context->UpdateSubresource(m_sortedCellEndsBuffer.buffer.Get(), 0, nullptr, invalidSortedEnds.data(), 0, 0);
        UpdateAnimatedScalp(0.0f);
        m_accumulator = 0.0f;
        m_runtimeFollowerUpdatePhase = 0u;
        m_elapsedSeconds = 0.0f;
        m_simulationTickIndex = 0u;
        m_experimentRunStatus = ExperimentRunStatus::InProgress;
        m_experimentFailureCode = 0u;
        m_experimentFailureMessage.clear();
        m_fullCpuGroupUploadCount = 0u;
        m_sameFrameReadbackCount = 0u;
        m_nanStateCount = 0u;
        m_infiniteStateCount = 0u;
        m_syntheticMetricCount = 0u;
        m_unavailableMetricCount = 0u;
        m_gpuTimingValidFrameCount = 0u;
        m_gpuTimingRequiredFrameCount = m_experimentCase.measurementFrames;
        m_scenarioAppliedEventCount = 0u;
        m_scenarioAssertionFailureCount = 0u;
        m_scenarioCollisionCandidateCount = 0u;
        m_scenarioWakeRequestCount = 0u;
        m_scenarioRepairAttemptCount = 0u;
        m_scenarioEditInvalidationCount = 0u;
        m_scenarioStateTransitionCount = 0u;
        m_lastColliderStateHash = 0ull;
        m_scenarioBaseCaptured = false;
        m_gpuTimingResolvedCount = 0u;
        m_gpuTimingValidCount = 0u;
        m_gpuTimingUnresolvedCount = 0u;
        m_counterReadbackResolvedCount = 0u;
        m_counterReadbackUnresolvedCount = 0u;
        m_lastSimulationStepMilliseconds = 0.0f;
        m_performanceSamples.clear();
        m_noveltyTelemetry = hairgpu::MakeZeroNoveltyTelemetry();
        ResetSegmentGroupRuntimeResources();
        RebuildActiveSetFoundation();RecomputeAssetBounds();
        AutoFrameCameraToAsset(true);
        m_lastEstimatedVisibleParticles = 0u;
        m_lastReadbackParticleCount = 0u;
        m_lastVisibilitySuspect = false;
        m_lastUsedCpuVisibilityFallback = false;
        m_lastVisibilityReadbackValid = true;
        m_lastVisibilityUsedInitialPoseFallback = false;
        m_visibilityReadbackFailureCount = 0u;
        m_consecutiveInvisibleFrames = 0u;
        m_visibilityRecoveryCount = 0u;
        m_renderMode = RenderMode::StrandRibbon;
        m_widthScale = std::max(m_widthScale, 0.65f);
        QueueVisibilityRecovery();
        SyncControlsFromState();
        UpdateStatsLabel();
    }

    void GpuHairViewer::ResetSegmentGroupRuntimeResources()
    {
        if (!m_segmentGroupRuntimeReady)
        {
            return;
        }

        ++m_simulationResetVersion;
        ++m_headTransformVersion;
        ++m_scalpVersion;
        ++m_colliderVersion;
        ++m_editVersion;

        if (!BuildSegmentGroupRuntimeTopology())
        {
            m_segmentGroupRuntimeReady = false;
            return;
        }

        if (m_segmentGroupBuffer.buffer &&
            !m_segmentGroupInitialData.empty())
        {
            m_context->UpdateSubresource(
                m_segmentGroupBuffer.buffer.Get(),
                0u,
                nullptr,
                m_segmentGroupInitialData.data(),
                0u,
                0u);
        }

        if (m_segmentGroupBlockBuffer.buffer &&
            !m_segmentGroupBlockInitialData.empty())
        {
            ++m_fullCpuGroupUploadCount;
            ++m_noveltyTelemetry.fullCpuGroupUploadCount;
            m_context->UpdateSubresource(
                m_segmentGroupBlockBuffer.buffer.Get(),
                0u,
                nullptr,
                m_segmentGroupBlockInitialData.data(),
                0u,
                0u);
        }

        if (m_segmentGroupCertificateBuffer.buffer &&
            !m_segmentGroupCertificateInitialData.empty())
        {
            m_context->UpdateSubresource(
                m_segmentGroupCertificateBuffer.buffer.Get(),
                0u,
                nullptr,
                m_segmentGroupCertificateInitialData.data(),
                0u,
                0u);
        }

        const std::array<uint32_t, 4> zeroCounters =
        {
            0u,
            0u,
            0u,
            0u
        };

        if (m_segmentGroupCounterBuffer.buffer)
        {
            m_context->UpdateSubresource(
                m_segmentGroupCounterBuffer.buffer.Get(),
                0u,
                nullptr,
                zeroCounters.data(),
                0u,
                0u);
        }

        SegmentGroupRuntimeCounters runtimeCounters = {};
        runtimeCounters.totalGroupCount =
            m_segmentGroupCount;

        runtimeCounters.fullPositionMemoryMb =
            static_cast<float>(
                static_cast<double>(m_segmentGroupCount) *
                sizeof(GpuParticle) *
                2.0 /
                (1024.0 * 1024.0));

        runtimeCounters.directionCacheMemoryMb =
            static_cast<float>(
                static_cast<double>(m_segmentGroupCount) *
                sizeof(float) *
                4.0 /
                (1024.0 * 1024.0));

        if (m_segmentGroupRuntimeCountersBuffer.buffer)
        {
            m_context->UpdateSubresource(
                m_segmentGroupRuntimeCountersBuffer.buffer.Get(),
                0u,
                nullptr,
                &runtimeCounters,
                0u,
                0u);
        }

        const UINT zeroDispatchArgs[4] =
        {
            0u,
            1u,
            1u,
            0u
        };

        if (m_segmentGroupDynamicDispatchArgsBuffer)
        {
            m_context->UpdateSubresource(
                m_segmentGroupDynamicDispatchArgsBuffer.Get(),
                0u,
                nullptr,
                zeroDispatchArgs,
                0u,
                0u);
        }

        if (m_segmentGroupRepairDispatchArgsBuffer)
        {
            m_context->UpdateSubresource(
                m_segmentGroupRepairDispatchArgsBuffer.Get(),
                0u,
                nullptr,
                zeroDispatchArgs,
                0u,
                0u);
        }

        m_forceAllSegmentGroupsDirty = true;
    }

    void GpuHairViewer::DetectSegmentGroupDirtyEvents()
    {
        if (!m_segmentGroupRuntimeReady ||
            !m_noveltyFlags.enableMicrotype)
        {
            return;
        }

        const uint64_t colliderHash = ComputeColliderStateHash();
        const bool colliderChanged = m_lastColliderStateHash != 0ull && colliderHash != m_lastColliderStateHash;
        m_lastColliderStateHash = colliderHash;
        m_previousSphereColliderCount = static_cast<uint32_t>(m_colliders.spheres.size());
        m_previousCapsuleColliderCount = static_cast<uint32_t>(m_colliders.capsules.size());
        m_previousPlaneColliderCount = static_cast<uint32_t>(m_colliders.planes.size());
        m_previousAabbColliderCount = static_cast<uint32_t>(m_colliders.aabbs.size());
        if (colliderChanged)
        {
            ++m_colliderVersion;
            ++m_noveltyTelemetry.colliderVersionMismatchCount;
            m_forceAllSegmentGroupsDirty = true;
        }

        // The current viewer updates scalp positions every frame when
        // scalp motion is enabled. Until precise transform-difference
        // detection is implemented, treat this as a head/scalp event.
        if (m_enableScalpMotion)
        {
            ++m_headTransformVersion;
            ++m_scalpVersion;
            m_forceAllSegmentGroupsDirty = true;
        }

        // A full dirty upload is acceptable only as a temporary
        // initialization/fallback path. Normal operation must later
        // replace this with GPU dirty propagation.
        if (m_forceAllSegmentGroupsDirty &&
            m_segmentGroupBuffer.buffer &&
            !m_segmentGroupInitialData.empty())
        {
            for (SegmentGroupGpuData& group :
                m_segmentGroupInitialData)
            {
                group.dirtyReason |=
                    SegmentGroupDirty_HeadMoved |
                    SegmentGroupDirty_CacheExpired;
            }

            for (SegmentGroupBlockGpuData& block :
                m_segmentGroupBlockInitialData)
            {
                block.dirtyMask |=
                    SegmentGroupDirty_HeadMoved |
                    SegmentGroupDirty_CacheExpired;
            }

            m_context->UpdateSubresource(
                m_segmentGroupBuffer.buffer.Get(),
                0u,
                nullptr,
                m_segmentGroupInitialData.data(),
                0u,
                0u);

            m_context->UpdateSubresource(
                m_segmentGroupBlockBuffer.buffer.Get(),
                0u,
                nullptr,
                m_segmentGroupBlockInitialData.data(),
                0u,
                0u);

            m_forceAllSegmentGroupsDirty = false;
        }
    }

    void GpuHairViewer::ClassifySegmentGroupBlocks()
    {
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupBlockClassifyCS || m_segmentGroupBlockCount == 0u)
        {
            if (IsExperimentModeEnabled() && m_experimentCase.strictValidationEnabled)
                SetFatalExperimentFailure(1201u, L"SegmentGroupBlockClassifyCS is unavailable.");
            return;
        }
        ID3D11ShaderResourceView* groupSrv = m_segmentGroupBuffer.srv.Get();
        ID3D11UnorderedAccessView* blockUav = m_segmentGroupBlockBuffer.uav.Get();
        m_context->CSSetShader(m_segmentGroupBlockClassifyCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(0u, 1u, &groupSrv);
        m_context->CSSetUnorderedAccessViews(1u, 1u, &blockUav, nullptr);
        m_context->Dispatch(std::max<UINT>((m_segmentGroupBlockCount + 63u) / 64u, 1u), 1u, 1u);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetShaderResources(0u, 1u, &nullSrv);
        m_context->CSSetUnorderedAccessViews(1u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::ClassifyDirtySegmentGroups()
    {
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupClassifyCS || m_segmentGroupCount == 0u)
        {
            if (IsExperimentModeEnabled() && m_experimentCase.strictValidationEnabled)
                SetFatalExperimentFailure(1202u, L"SegmentGroupClassifyCS is unavailable.");
            return;
        }
        ID3D11ShaderResourceView* currentPositionSrv = m_positions[m_currentBufferIndex].srv.Get();
        ID3D11UnorderedAccessView* groupUav = m_segmentGroupBuffer.uav.Get();
        ID3D11UnorderedAccessView* certificateUav = m_segmentGroupCertificateBuffer.uav.Get();
        m_context->CSSetShader(m_segmentGroupClassifyCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(3u, 1u, &currentPositionSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &groupUav, nullptr);
        m_context->CSSetUnorderedAccessViews(2u, 1u, &certificateUav, nullptr);
        m_context->Dispatch(std::max<UINT>((m_segmentGroupCount + 63u) / 64u, 1u), 1u, 1u);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetShaderResources(3u, 1u, &nullSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(2u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::CompactDynamicSegmentGroups()
    {
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupCompactDynamicCS || m_segmentGroupCount == 0u)
        {
            if (IsExperimentModeEnabled() && m_experimentCase.strictValidationEnabled)
                SetFatalExperimentFailure(1203u, L"SegmentGroupCompactDynamicCS is unavailable.");
            return;
        }
        const std::array<uint32_t, 4> zeroCounters = { 0u, 0u, 0u, 0u };
        m_context->UpdateSubresource(m_segmentGroupCounterBuffer.buffer.Get(), 0u, nullptr, zeroCounters.data(), 0u, 0u);
        ID3D11ShaderResourceView* groupSrv = m_segmentGroupBuffer.srv.Get();
        ID3D11UnorderedAccessView* uavs[] = {
            m_dynamicSegmentGroupIndicesBuffer.uav.Get(),
            m_staticTransformedGroupIndicesBuffer.uav.Get(),
            m_repairSegmentGroupIndicesBuffer.uav.Get(),
            m_segmentGroupCounterBuffer.uav.Get()
        };
        m_context->CSSetShader(m_segmentGroupCompactDynamicCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(0u, 1u, &groupSrv);
        m_context->CSSetUnorderedAccessViews(0u, 4u, uavs, nullptr);
        m_context->Dispatch(std::max<UINT>((m_segmentGroupCount + 63u) / 64u, 1u), 1u, 1u);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUavs[4] = { nullptr, nullptr, nullptr, nullptr };
        m_context->CSSetShaderResources(0u, 1u, &nullSrv);
        m_context->CSSetUnorderedAccessViews(0u, 4u, nullUavs, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::CompactStaticSegmentGroups()
    {
        // SegmentGroupCompactDynamicCS emits dynamic, static and repair lists in
        // one deterministic pass. Keeping this explicit method prevents callers
        // from launching a second pass that would double-count static groups.
    }


    void GpuHairViewer::BuildSegmentGroupDispatchArguments()
    {
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupBuildDispatchArgsCS ||
            !m_segmentGroupDynamicDispatchArgsUav || !m_segmentGroupStaticDispatchArgsUav ||
            !m_segmentGroupRepairDispatchArgsUav)
        {
            if (IsExperimentModeEnabled() && m_experimentCase.strictValidationEnabled)
                SetFatalExperimentFailure(1204u, L"Segment-group indirect argument resources are unavailable.");
            return;
        }
        ID3D11UnorderedAccessView* uavs[] = {
            m_segmentGroupCounterBuffer.uav.Get(),
            m_segmentGroupDynamicDispatchArgsUav.Get(),
            m_segmentGroupStaticDispatchArgsUav.Get(),
            m_segmentGroupRepairDispatchArgsUav.Get()
        };
        m_context->CSSetShader(m_segmentGroupBuildDispatchArgsCS.Get(), nullptr, 0u);
        m_context->CSSetUnorderedAccessViews(3u, 4u, uavs, nullptr);
        m_context->Dispatch(1u, 1u, 1u);
        ID3D11UnorderedAccessView* nullUavs[4] = { nullptr, nullptr, nullptr, nullptr };
        m_context->CSSetUnorderedAccessViews(3u, 4u, nullUavs, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
        ++m_noveltyTelemetry.dispatchIndirectCount;
    }

    void GpuHairViewer::TransformStaticSegmentGroups()
    {
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupStaticTransformCS ||
            !m_segmentGroupStaticDispatchArgsBuffer)
        {
            if (IsExperimentModeEnabled() && m_experimentCase.strictValidationEnabled)
                SetFatalExperimentFailure(1205u, L"Static segment-group transform path is unavailable.");
            return;
        }
        ID3D11ShaderResourceView* staticListSrv = m_staticTransformedGroupIndicesBuffer.srv.Get();
        ID3D11ShaderResourceView* counterSrv = m_segmentGroupCounterBuffer.srv.Get();
        ID3D11UnorderedAccessView* groupUav = m_segmentGroupBuffer.uav.Get();
        ID3D11UnorderedAccessView* positionUav = m_positions[m_currentBufferIndex].uav.Get();
        m_context->CSSetShader(m_segmentGroupStaticTransformCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(6u, 1u, &staticListSrv);
        m_context->CSSetShaderResources(8u, 1u, &counterSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &groupUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &positionUav, nullptr);
        m_context->DispatchIndirect(m_segmentGroupStaticDispatchArgsBuffer.Get(), 0u);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetShaderResources(6u, 1u, &nullSrv);
        m_context->CSSetShaderResources(8u, 1u, &nullSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::ExecutePredictiveWake(float dt)
    {
        (void)dt;
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupPredictiveWakeCS || m_segmentGroupCount == 0u)
            return;
        ID3D11UnorderedAccessView* groupUav = m_segmentGroupBuffer.uav.Get();
        ID3D11UnorderedAccessView* wakeUav = m_wakeTelemetryBuffer.uav.Get();
        m_context->CSSetShader(m_segmentGroupPredictiveWakeCS.Get(), nullptr, 0u);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &groupUav, nullptr);
        m_context->CSSetUnorderedAccessViews(5u, 1u, &wakeUav, nullptr);
        m_context->Dispatch(std::max<UINT>((m_segmentGroupCount + 63u) / 64u, 1u), 1u, 1u);
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(5u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::SolveDynamicSegmentGroups(float dt)
    {
        (void)dt;
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupDynamicSolveCS || !m_segmentGroupDynamicDispatchArgsBuffer)
            return;
        ID3D11ShaderResourceView* dynamicListSrv = m_dynamicSegmentGroupIndicesBuffer.srv.Get();
        ID3D11ShaderResourceView* counterSrv = m_segmentGroupCounterBuffer.srv.Get();
        ID3D11UnorderedAccessView* groupUav = m_segmentGroupBuffer.uav.Get();
        ID3D11UnorderedAccessView* positionUav = m_positions[m_currentBufferIndex].uav.Get();
        ID3D11UnorderedAccessView* previousUav = m_previousPositions[m_currentBufferIndex].uav.Get();
        m_context->CSSetShader(m_segmentGroupDynamicSolveCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(5u, 1u, &dynamicListSrv);
        m_context->CSSetShaderResources(8u, 1u, &counterSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &groupUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &positionUav, nullptr);
        m_context->CSSetUnorderedAccessViews(4u, 1u, &previousUav, nullptr);
        m_context->DispatchIndirect(m_segmentGroupDynamicDispatchArgsBuffer.Get(), 0u);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetShaderResources(5u, 1u, &nullSrv);
        m_context->CSSetShaderResources(8u, 1u, &nullSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(4u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::ExecuteVelocityRecovery(float dt)
    {
        (void)dt;
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupVelocityRecoveryCS || !m_segmentGroupDynamicDispatchArgsBuffer)
            return;
        ID3D11ShaderResourceView* dynamicListSrv = m_dynamicSegmentGroupIndicesBuffer.srv.Get();
        ID3D11ShaderResourceView* counterSrv = m_segmentGroupCounterBuffer.srv.Get();
        ID3D11UnorderedAccessView* groupUav = m_segmentGroupBuffer.uav.Get();
        ID3D11UnorderedAccessView* positionUav = m_positions[m_currentBufferIndex].uav.Get();
        ID3D11UnorderedAccessView* previousUav = m_previousPositions[m_currentBufferIndex].uav.Get();
        m_context->CSSetShader(m_segmentGroupVelocityRecoveryCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(5u, 1u, &dynamicListSrv);
        m_context->CSSetShaderResources(8u, 1u, &counterSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &groupUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &positionUav, nullptr);
        m_context->CSSetUnorderedAccessViews(4u, 1u, &previousUav, nullptr);
        m_context->DispatchIndirect(m_segmentGroupDynamicDispatchArgsBuffer.Get(), 0u);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetShaderResources(5u, 1u, &nullSrv);
        m_context->CSSetShaderResources(8u, 1u, &nullSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(4u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::SolveRepairSegmentGroups(float dt)
    {
        (void)dt;
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupRepairSolveCS || !m_segmentGroupRepairDispatchArgsBuffer)
            return;
        ID3D11ShaderResourceView* repairListSrv = m_repairSegmentGroupIndicesBuffer.srv.Get();
        ID3D11ShaderResourceView* counterSrv = m_segmentGroupCounterBuffer.srv.Get();
        ID3D11UnorderedAccessView* groupUav = m_segmentGroupBuffer.uav.Get();
        ID3D11UnorderedAccessView* positionUav = m_positions[m_currentBufferIndex].uav.Get();
        ID3D11UnorderedAccessView* previousUav = m_previousPositions[m_currentBufferIndex].uav.Get();
        ID3D11UnorderedAccessView* trackingUav = m_repairTrackingBuffer.uav.Get();
        m_context->CSSetShader(m_segmentGroupRepairSolveCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(7u, 1u, &repairListSrv);
        m_context->CSSetShaderResources(8u, 1u, &counterSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &groupUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &positionUav, nullptr);
        m_context->CSSetUnorderedAccessViews(4u, 1u, &previousUav, nullptr);
        m_context->CSSetUnorderedAccessViews(6u, 1u, &trackingUav, nullptr);
        m_context->DispatchIndirect(m_segmentGroupRepairDispatchArgsBuffer.Get(), 0u);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetShaderResources(7u, 1u, &nullSrv);
        m_context->CSSetShaderResources(8u, 1u, &nullSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(4u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(6u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::UpdateSegmentGroupCaches()
    {
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupCacheUpdateCS || m_segmentGroupCount == 0u)
            return;
        ID3D11UnorderedAccessView* groupUav = m_segmentGroupBuffer.uav.Get();
        ID3D11UnorderedAccessView* certificateUav = m_segmentGroupCertificateBuffer.uav.Get();
        m_context->CSSetShader(m_segmentGroupCacheUpdateCS.Get(), nullptr, 0u);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &groupUav, nullptr);
        m_context->CSSetUnorderedAccessViews(2u, 1u, &certificateUav, nullptr);
        m_context->Dispatch(std::max<UINT>((m_segmentGroupCount + 63u) / 64u, 1u), 1u, 1u);
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(2u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::RepairSegmentGroupBoundaries()
    {
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupBoundaryRepairCS || !m_segmentGroupRepairDispatchArgsBuffer)
        {
            if (IsExperimentModeEnabled() && m_experimentCase.strictValidationEnabled)
                SetFatalExperimentFailure(1206u, L"Segment-group boundary repair path is unavailable.");
            return;
        }
        ID3D11ShaderResourceView* repairListSrv = m_repairSegmentGroupIndicesBuffer.srv.Get();
        ID3D11ShaderResourceView* counterSrv = m_segmentGroupCounterBuffer.srv.Get();
        ID3D11UnorderedAccessView* groupUav = m_segmentGroupBuffer.uav.Get();
        ID3D11UnorderedAccessView* positionUav = m_positions[m_currentBufferIndex].uav.Get();
        m_context->CSSetShader(m_segmentGroupBoundaryRepairCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(7u, 1u, &repairListSrv);
        m_context->CSSetShaderResources(8u, 1u, &counterSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &groupUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &positionUav, nullptr);
        m_context->DispatchIndirect(m_segmentGroupRepairDispatchArgsBuffer.Get(), 0u);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetShaderResources(7u, 1u, &nullSrv);
        m_context->CSSetShaderResources(8u, 1u, &nullSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::ExecuteSegmentGroupWatchdog()
    {
        if (!m_segmentGroupRuntimeReady || !m_segmentGroupWatchdogCS || m_segmentGroupCount == 0u)
            return;
        if (m_segmentGroupWatchdogIntervalFrames == 0u ||
            (m_globalFrameIndex % m_segmentGroupWatchdogIntervalFrames) != 0u)
            return;
        ID3D11UnorderedAccessView* groupUav = m_segmentGroupBuffer.uav.Get();
        ID3D11UnorderedAccessView* positionUav = m_positions[m_currentBufferIndex].uav.Get();
        m_context->CSSetShader(m_segmentGroupWatchdogCS.Get(), nullptr, 0u);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &groupUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &positionUav, nullptr);
        m_context->Dispatch(std::max<UINT>((m_segmentGroupCount + 63u) / 64u, 1u), 1u, 1u);
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetUnorderedAccessViews(3u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
        ++m_noveltyTelemetry.watchdogCheckCount;
    }

    void GpuHairViewer::ResolveSegmentGroupTelemetry()
    {
        if (!m_segmentGroupRuntimeReady)
        {
            return;
        }

        m_noveltyTelemetry.totalSegmentGroupCount =
            m_segmentGroupCount;

        const float fullPositionMemoryMb =
            static_cast<float>(
                static_cast<double>(m_segmentGroupCount) *
                sizeof(GpuParticle) *
                2.0 /
                (1024.0 * 1024.0));

        const float directionCacheMemoryMb =
            static_cast<float>(
                static_cast<double>(m_segmentGroupCount) *
                sizeof(float) *
                4.0 /
                (1024.0 * 1024.0));

        m_noveltyTelemetry.fullPositionMemoryMb =
            fullPositionMemoryMb;

        m_noveltyTelemetry.directionCacheMemoryMb =
            directionCacheMemoryMb;

        if (fullPositionMemoryMb > 1.0e-6f)
        {
            m_noveltyTelemetry.directionCacheMemorySavingRate =
                1.0f -
                directionCacheMemoryMb /
                fullPositionMemoryMb;
        }
        else
        {
            m_noveltyTelemetry.directionCacheMemorySavingRate =
                0.0f;
        }
    }


    // HAIRGPU_EXPERIMENT_RUNTIME_PATCH_V1: deterministic experiment tick, scenario driver, and delayed
    // counter readback. These stay in the existing INL to avoid a new C++ unit.
    void GpuHairViewer::BeginGpuPassTiming(GpuPassId pass)
    {
        if (m_activeGpuTimingFrameIndex < 0 || !m_context) return;
        GpuTimingFrame& frame = m_gpuTimingFrames[static_cast<size_t>(m_activeGpuTimingFrameIndex)];
        const size_t passIndex = static_cast<size_t>(pass);
        if (!frame.issued || passIndex >= kGpuPassCount || !frame.passStart[passIndex] || frame.passIssued[passIndex] != 0u) return;
        m_context->End(frame.passStart[passIndex].Get());
    }

    void GpuHairViewer::EndGpuPassTiming(GpuPassId pass)
    {
        if (m_activeGpuTimingFrameIndex < 0 || !m_context) return;
        GpuTimingFrame& frame = m_gpuTimingFrames[static_cast<size_t>(m_activeGpuTimingFrameIndex)];
        const size_t passIndex = static_cast<size_t>(pass);
        if (!frame.issued || passIndex >= kGpuPassCount || !frame.passEnd[passIndex] || frame.passIssued[passIndex] != 0u) return;
        m_context->End(frame.passEnd[passIndex].Get());
        frame.passIssued[passIndex] = 1u;
    }

    uint64_t GpuHairViewer::ComputeColliderStateHash() const
    {
        uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](const void* bytes, size_t size)
        {
            const uint8_t* p = static_cast<const uint8_t*>(bytes);
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= p[i];
                hash *= 1099511628211ull;
            }
        };
        for (const auto& value : m_colliders.spheres) mix(&value, sizeof(value));
        for (const auto& value : m_colliders.capsules) mix(&value, sizeof(value));
        for (const auto& value : m_colliders.planes) mix(&value, sizeof(value));
        for (const auto& value : m_colliders.aabbs) mix(&value, sizeof(value));
        return hash;
    }

    void GpuHairViewer::ApplyScenarioForTick(uint32_t tickIndex, float dt)
    {
        if (!m_experimentCase.enabled)
            return;
        if (m_experimentCase.timelineLoaded)
        {
            ApplyScenarioTimelineFrame(tickIndex);
            return;
        }
        if (m_experimentCase.strictValidationEnabled)
        {
            SetFatalExperimentFailure(1301u, L"Formal scenario timeline was not loaded.");
            return;
        }

        // Debug-only exact-ID fallback. Formal measurements never use name
        // substring matching, which previously misclassified S03_Walk as wind.
        const uint32_t eventStart = std::max<uint32_t>(m_experimentCase.warmupFrames, 1u);
        bool applied = false;
        switch (m_experimentCase.scenarioId)
        {
        case ExperimentScenarioId::S01_Idle:
        case ExperimentScenarioId::S15_AllStatic:
            m_enableScalpMotion = false;
            m_simConfig.windStrength = 0.0f;
            applied = tickIndex == 0u;
            break;
        case ExperimentScenarioId::S03_Walk:
            m_enableScalpMotion = true;
            applied = true;
            break;
        case ExperimentScenarioId::S06_Teleport:
            if (tickIndex == eventStart && !m_colliders.spheres.empty())
            {
                m_colliders.spheres.front().center.x += 0.35f;
                m_forceAllSegmentGroupsDirty = true;
                applied = true;
            }
            break;
        case ExperimentScenarioId::S07_ColliderApproach:
            if (!m_colliders.spheres.empty())
            {
                m_colliders.spheres.front().center.z -= 0.001f;
                ++m_noveltyTelemetry.collisionCandidateCount;
                ++m_scenarioCollisionCandidateCount;
                applied = true;
            }
            break;
        case ExperimentScenarioId::S09_StrongWind:
            m_simConfig.windStrength = 1.2f;
            applied = true;
            break;
        case ExperimentScenarioId::S11_LocalEdit:
            if (tickIndex == eventStart)
            {
                const bool thesisEditEvaluation = EnvironmentValueIsTrue(
                    GetEnvironmentVariableString(L"HAIRGPU_THESIS_EDIT_EVAL"));
                if (thesisEditEvaluation)
                {
                    const bool localInvalidationQueued =
                        InjectThesisPracticalLocalEdit();
                    if (m_experimentCase.methodId !=
                            ExperimentMethodId::P_IntegratedSegmentGroup ||
                        !localInvalidationQueued)
                    {
                        ++m_editVersion;
                        m_forceAllSegmentGroupsDirty = true;
                    }
                }
                else
                {
                    ++m_editVersion;
                    m_forceAllSegmentGroupsDirty = true;
                }
                ++m_scenarioEditInvalidationCount;
                applied = true;
            }
            break;
        case ExperimentScenarioId::S13_RepairStorm:
            if (tickIndex == eventStart)
            {
                m_forceAllSegmentGroupsDirty = true;
                ++m_noveltyTelemetry.repairAttemptCount;
                ++m_scenarioRepairAttemptCount;
                applied = true;
            }
            break;
        case ExperimentScenarioId::S14_AllDynamic:
            m_enableScalpMotion = true;
            m_simConfig.windStrength = std::max(m_simConfig.windStrength, 0.8f);
            m_forceAllSegmentGroupsDirty = true;
            applied = true;
            break;
        case ExperimentScenarioId::S16_StateThrashing:
            if ((tickIndex % 60u) == 0u)
            {
                m_enableScalpMotion = !m_enableScalpMotion;
                ++m_noveltyTelemetry.stateTransitionCount;
                ++m_scenarioStateTransitionCount;
                applied = true;
            }
            break;
        default:
            break;
        }
        if (applied)
        {
            ++m_scenarioAppliedEventCount;
            ++m_noveltyTelemetry.scenarioAppliedEventCount;
            m_noveltyTelemetry.availabilityMask |= hairgpu::HairTelemetryAvailability_Scenario;
        }
        (void)dt;
    }

    void GpuHairViewer::StepFullDynamic(float dt)
    {
        BeginGpuPassTiming(GpuPassId::DynamicSolve);
        StepSimulation(dt);
        EndGpuPassTiming(GpuPassId::DynamicSolve);
    }

    void GpuHairViewer::StepStrandSleep(float dt)
    {
        // Strand-level baseline keeps its external sleep bookkeeping but uses
        // the established solver for awake guides.
        ExecuteBudgetedPhysics();
        StepFullDynamic(dt);
    }

    void GpuHairViewer::StepBlockSleep(float dt)
    {
        DetectSegmentGroupDirtyEvents();
        BeginGpuPassTiming(GpuPassId::BlockClassification);
        ClassifySegmentGroupBlocks();
        EndGpuPassTiming(GpuPassId::BlockClassification);
        StepFullDynamic(dt);
    }

    void GpuHairViewer::StepSegmentSleep(float dt)
    {
        if (!UpdateSegmentGroupRuntimeConstants(dt)) return;
        DetectSegmentGroupDirtyEvents();
        BeginGpuPassTiming(GpuPassId::BlockClassification); ClassifySegmentGroupBlocks(); EndGpuPassTiming(GpuPassId::BlockClassification);
        BeginGpuPassTiming(GpuPassId::GroupClassification); ClassifyDirtySegmentGroups(); EndGpuPassTiming(GpuPassId::GroupClassification);
        BeginGpuPassTiming(GpuPassId::Compaction); CompactDynamicSegmentGroups(); BuildSegmentGroupDispatchArguments(); EndGpuPassTiming(GpuPassId::Compaction);
        BeginGpuPassTiming(GpuPassId::StaticTransform); TransformStaticSegmentGroups(); EndGpuPassTiming(GpuPassId::StaticTransform);
        BeginGpuPassTiming(GpuPassId::DynamicSolve); SolveDynamicSegmentGroups(dt); EndGpuPassTiming(GpuPassId::DynamicSolve);
        UpdateSegmentGroupCaches();
        ResolveSegmentGroupTelemetry();
    }

    void GpuHairViewer::StepActiveSetOnly(float dt)
    {
        ExecuteBudgetedPhysics();
        StepFullDynamic(dt);
    }

    void GpuHairViewer::StepExternalStateArray(float dt)
    {
        // Uses the same thresholds and passes as the proposal. The only
        // experimental variable is state storage outside SegmentGroupGpuData.
        StepIntegratedSegmentGroup(dt);
    }

    void GpuHairViewer::StepIntegratedSegmentGroup(float dt)
    {
        if (!UpdateSegmentGroupRuntimeConstants(dt)) return;
        DetectSegmentGroupDirtyEvents();
        BeginGpuPassTiming(GpuPassId::BlockClassification); ClassifySegmentGroupBlocks(); EndGpuPassTiming(GpuPassId::BlockClassification);
        BeginGpuPassTiming(GpuPassId::GroupClassification); ClassifyDirtySegmentGroups(); EndGpuPassTiming(GpuPassId::GroupClassification);
        if (m_experimentCase.appliedFlags.enablePredictiveWake)
        {
            BeginGpuPassTiming(GpuPassId::PredictiveWake); ExecutePredictiveWake(dt); EndGpuPassTiming(GpuPassId::PredictiveWake);
        }
        BeginGpuPassTiming(GpuPassId::Compaction); CompactDynamicSegmentGroups(); BuildSegmentGroupDispatchArguments(); EndGpuPassTiming(GpuPassId::Compaction);
        BeginGpuPassTiming(GpuPassId::StaticTransform); TransformStaticSegmentGroups(); EndGpuPassTiming(GpuPassId::StaticTransform);
        BeginGpuPassTiming(GpuPassId::DynamicSolve); SolveDynamicSegmentGroups(dt); EndGpuPassTiming(GpuPassId::DynamicSolve);
        if (m_experimentCase.appliedFlags.enableVelocityRecovery)
        {
            BeginGpuPassTiming(GpuPassId::VelocityRecovery); ExecuteVelocityRecovery(dt); EndGpuPassTiming(GpuPassId::VelocityRecovery);
        }
        if (m_experimentCase.appliedFlags.enableRepair)
        {
            BeginGpuPassTiming(GpuPassId::RepairSolve); SolveRepairSegmentGroups(dt); EndGpuPassTiming(GpuPassId::RepairSolve);
            BeginGpuPassTiming(GpuPassId::BoundaryRepair); RepairSegmentGroupBoundaries(); EndGpuPassTiming(GpuPassId::BoundaryRepair);
        }
        BeginGpuPassTiming(GpuPassId::CacheUpdate); UpdateSegmentGroupCaches(); EndGpuPassTiming(GpuPassId::CacheUpdate);
        BeginGpuPassTiming(GpuPassId::Watchdog); ExecuteSegmentGroupWatchdog(); EndGpuPassTiming(GpuPassId::Watchdog);
        ResolveSegmentGroupTelemetry();
    }

    void GpuHairViewer::ExecuteExperimentSimulationTick(float dt)
    {
        ResetNoveltyTelemetryForFrame();
        m_noveltyTelemetry.sourceFrameIndex = m_globalFrameIndex;
        ApplyScenarioForTick(m_simulationTickIndex, dt);
        if (m_experimentRunStatus == ExperimentRunStatus::Failed)
            return;

        const bool legacyInteractiveTick = !m_experimentCase.enabled;
        if (legacyInteractiveTick || m_experimentCase.appliedFlags.enablePerStrandEdit) ExecutePerStrandEditing();
        if (legacyInteractiveTick || m_experimentCase.appliedFlags.enableMicrotype) EvaluateMicrotypeCertificates();
        if (legacyInteractiveTick || m_experimentCase.appliedFlags.enableCertificateRender) ExecuteCertificateAwareRendering();

        switch (m_experimentCase.methodId)
        {
        case ExperimentMethodId::B0_FullDynamic: StepFullDynamic(dt); break;
        case ExperimentMethodId::B1_StrandSleep: StepStrandSleep(dt); break;
        case ExperimentMethodId::B2_BlockSleep: StepBlockSleep(dt); break;
        case ExperimentMethodId::B3_SegmentThresholdSleep:
        case ExperimentMethodId::B4_SegmentHysteresisSleep:
        case ExperimentMethodId::B5_SegmentCache:
        case ExperimentMethodId::B6_SegmentGpuCompaction: StepSegmentSleep(dt); break;
        case ExperimentMethodId::B7_ActiveSetOnly: StepActiveSetOnly(dt); break;
        case ExperimentMethodId::B8_ExternalStateArray: StepExternalStateArray(dt); break;
        case ExperimentMethodId::P_IntegratedSegmentGroup: StepIntegratedSegmentGroup(dt); break;
        case ExperimentMethodId::Unknown:
        default:
            if (legacyInteractiveTick) StepFullDynamic(dt);
            else SetFatalExperimentFailure(1302u, L"Unknown experiment method; no simulation path was executed.");
            break;
        }

        if (m_experimentCase.measurementMode != MeasurementMode::Performance)
            QueueCounterReadback(m_globalFrameIndex);
        ++m_simulationTickIndex;
    }

    void GpuHairViewer::QueueCounterReadback(uint32_t sourceFrameIndex)
    {
        if (!m_context || !m_device || !m_segmentGroupRuntimeCountersBuffer.buffer) return;
        CounterReadbackFrame& slot = m_counterReadbackFrames[m_counterReadbackWriteIndex];
        if (slot.issued)
        {
            ++m_counterReadbackUnresolvedCount;
            return;
        }
        if (!slot.staging)
        {
            D3D11_BUFFER_DESC desc = {};
            m_segmentGroupRuntimeCountersBuffer.buffer->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0u;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0u;
            desc.StructureByteStride = 0u;
            if (FAILED(m_device->CreateBuffer(&desc, nullptr, &slot.staging))) return;
        }
        m_context->CopyResource(slot.staging.Get(), m_segmentGroupRuntimeCountersBuffer.buffer.Get());
        slot.sourceFrameIndex = sourceFrameIndex;
        slot.ageFrames = 0u;
        slot.issued = true;
        slot.resolved = false;
        m_counterReadbackWriteIndex = (m_counterReadbackWriteIndex + 1u) % static_cast<uint32_t>(m_counterReadbackFrames.size());
    }

    void GpuHairViewer::ResolveCounterReadback(CounterReadbackFrame& slot)
    {
        if (!slot.issued || !slot.staging || slot.ageFrames < m_experimentCase.gpuTimestampDelayFrames) return;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT hr = m_context->Map(slot.staging.Get(), 0u, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr != S_OK) return;
        const SegmentGroupRuntimeCounters counters = *static_cast<const SegmentGroupRuntimeCounters*>(mapped.pData);
        m_context->Unmap(slot.staging.Get(), 0u);
        for (PerformanceSample& sample : m_performanceSamples)
        {
            if (sample.globalFrameIndex != slot.sourceFrameIndex) continue;
            sample.sourceFrameIndex = slot.sourceFrameIndex;
            sample.totalSegmentGroupCount = counters.totalGroupCount;
            sample.staticCachedGroupCount = counters.staticCachedCount;
            sample.staticTransformedGroupCount = counters.staticTransformedCount;
            sample.dynamicGroupCount = counters.dynamicCount;
            sample.deferredCorrectionGroupCount = counters.deferredCount;
            sample.repairRequiredGroupCount = counters.repairCount;
            sample.fallbackGroupCount = counters.fallbackCount;
            sample.falseStaticCount = counters.falseStaticCount;
            sample.dirtyBlockCount = counters.dirtyBlockCount;
            sample.dirtyGroupCount = counters.dirtyGroupCount;
            sample.cacheHitCount = counters.cacheHitCount;
            sample.cacheMissCount = counters.cacheMissCount;
            sample.cacheInvalidationCount = counters.cacheInvalidationCount;
            sample.watchdogCheckCount = counters.watchdogCheckCount;
            sample.watchdogRepairCount = counters.watchdogRepairCount;
            sample.staticDynamicBoundaryMeanErrorMm = counters.totalGroupCount > 0u ? counters.boundaryErrorSumMm / static_cast<float>(counters.totalGroupCount) : 0.0f;
            sample.staticDynamicBoundaryMaxErrorMm = counters.boundaryErrorMaxMm;
            sample.directionCacheMemoryMb = counters.directionCacheMemoryMb;
            sample.fullPositionMemoryMb = counters.fullPositionMemoryMb;
            sample.availabilityMask |= hairgpu::HairTelemetryAvailability_GpuCounters;
            break;
        }
        slot.issued = false;
        slot.resolved = true;
        slot.ageFrames = 0u;
        ++m_counterReadbackResolvedCount;
    }

    void GpuHairViewer::DrainGpuTimingAndCounterReadbacks()
    {
        if (!m_context) return;
        m_context->Flush();
        for (uint32_t attempt = 0u; attempt < 240u; ++attempt)
        {
            bool pending = false;
            for (GpuTimingFrame& timing : m_gpuTimingFrames)
            {
                if (timing.issued) { ++timing.ageFrames; ResolveGpuTimingFrame(timing); pending |= timing.issued; }
            }
            for (CounterReadbackFrame& counters : m_counterReadbackFrames)
            {
                if (counters.issued) { ++counters.ageFrames; ResolveCounterReadback(counters); pending |= counters.issued; }
            }
            if (!pending) return;
            Sleep(1u);
        }
        for (const GpuTimingFrame& timing : m_gpuTimingFrames) if (timing.issued) ++m_gpuTimingUnresolvedCount;
        for (const CounterReadbackFrame& counters : m_counterReadbackFrames) if (counters.issued) ++m_counterReadbackUnresolvedCount;
    }

    void GpuHairViewer::PatchSampleFromResolvedGpuTiming(uint32_t sourceFrameIndex)
    {
        for (PerformanceSample& sample : m_performanceSamples)
        {
            if (sample.globalFrameIndex == sourceFrameIndex)
            {
                sample.gpuHairMilliseconds = m_lastGpuSimulationMilliseconds + m_lastGpuHairDrawMilliseconds;
                sample.availabilityMask |= hairgpu::HairTelemetryAvailability_GpuTiming;
                return;
            }
        }
    }

    void GpuHairViewer::StepSingleFrame()
    {
        LARGE_INTEGER simBegin = {};
        LARGE_INTEGER simEnd = {};
        QueryPerformanceCounter(&simBegin);
        ExecuteExperimentSimulationTick(m_simConfig.fixedTimeStep);
        QueryPerformanceCounter(&simEnd);
        m_elapsedSeconds += m_simConfig.fixedTimeStep;
        m_lastSimulationStepMilliseconds = static_cast<float>(simEnd.QuadPart - simBegin.QuadPart) * 1000.0f / static_cast<float>(m_qpcFrequency.QuadPart);
        PerformanceSample sample = BuildPerformanceSample(m_simConfig.fixedTimeStep, (m_simConfig.fixedTimeStep > 1.0e-6f) ? (1.0f / m_simConfig.fixedTimeStep) : 0.0f);
        sample.globalFrameIndex = m_globalFrameIndex++;
        sample.measuredFrameIndex = m_measuredFrameIndex++;
        m_performanceSamples.push_back(sample);
        if (m_performanceSamples.size() > 12000)
        {
            m_performanceSamples.erase(m_performanceSamples.begin(), m_performanceSamples.begin() + static_cast<std::ptrdiff_t>(m_performanceSamples.size() - 12000));
        }
        UpdateStatsLabel();
    }

    void GpuHairViewer::ResetNoveltyTelemetryForFrame()
    {
        m_noveltyTelemetry = hairgpu::MakeZeroNoveltyTelemetry();
        m_noveltyTelemetry.frameIndex = m_globalFrameIndex;
        m_noveltyTelemetry.measuredFrameIndex = m_measuredFrameIndex;
        m_noveltyTelemetry.sourceFrameIndex = m_globalFrameIndex;
        m_noveltyTelemetry.scenarioAppliedEventCount = m_scenarioAppliedEventCount;
        m_noveltyTelemetry.scenarioAssertionFailureCount = m_scenarioAssertionFailureCount;
        m_noveltyTelemetry.gpuTimingResolvedFrameCount = m_gpuTimingResolvedCount;
        m_noveltyTelemetry.gpuTimingValidFrameCount = m_gpuTimingValidCount;
        m_noveltyTelemetry.gpuTimingUnresolvedFrameCount = m_gpuTimingUnresolvedCount;
        m_noveltyTelemetry.counterReadbackResolvedFrameCount = m_counterReadbackResolvedCount;
        m_noveltyTelemetry.counterReadbackUnresolvedFrameCount = m_counterReadbackUnresolvedCount;
        m_noveltyTelemetry.topLevelNoveltyMask = hairgpu::BuildTopLevelNoveltyMask(m_noveltyFlags);
    }

    void GpuHairViewer::EvaluateMicrotypeCertificates()
    {
        if (!m_noveltyFlags.enableMicrotype ||
            !m_segmentGroupRuntimeReady)
        {
            return;
        }

        if (m_noveltyFlags.enableMicrotypeDirtyPropagation) DetectSegmentGroupDirtyEvents();
        if (m_noveltyFlags.enableMicrotypeBlockHierarchy)
        {
            BeginGpuPassTiming(GpuPassId::BlockClassification);
            ClassifySegmentGroupBlocks();
            EndGpuPassTiming(GpuPassId::BlockClassification);
        }
        if (m_noveltyFlags.enableMicrotypeCertificate)
        {
            BeginGpuPassTiming(GpuPassId::GroupClassification);
            ClassifyDirtySegmentGroups();
            EndGpuPassTiming(GpuPassId::GroupClassification);
        }
        if (m_experimentCase.appliedFlags.enableGpuCompaction)
        {
            BeginGpuPassTiming(GpuPassId::Compaction);
            CompactDynamicSegmentGroups();
            if (m_noveltyFlags.enableMicrotypeIndirectDispatch) BuildSegmentGroupDispatchArguments();
            EndGpuPassTiming(GpuPassId::Compaction);
        }
        if (m_experimentCase.appliedFlags.enableStaticCache)
        {
            BeginGpuPassTiming(GpuPassId::StaticTransform);
            TransformStaticSegmentGroups();
            EndGpuPassTiming(GpuPassId::StaticTransform);
        }
        if (m_noveltyFlags.enableMicrotypeBoundaryRepair && m_experimentCase.appliedFlags.enableRepair)
        {
            BeginGpuPassTiming(GpuPassId::BoundaryRepair);
            RepairSegmentGroupBoundaries();
            EndGpuPassTiming(GpuPassId::BoundaryRepair);
        }
        if (m_noveltyFlags.enableMicrotypeWatchdog)
        {
            BeginGpuPassTiming(GpuPassId::Watchdog);
            ExecuteSegmentGroupWatchdog();
            EndGpuPassTiming(GpuPassId::Watchdog);
        }
        BeginGpuPassTiming(GpuPassId::TelemetryReduction);
        ResolveSegmentGroupTelemetry();
        EndGpuPassTiming(GpuPassId::TelemetryReduction);

        // These legacy Phase1 fields remain zero until the matching
        // typed decode path is connected.
        m_noveltyTelemetry.microtypeDecodedCount = 0u;
        m_noveltyTelemetry.safeDecodeCount = 0u;
        m_noveltyTelemetry.certificateFailureCount = 0u;
        m_noveltyTelemetry.falseSafeCount = 0u;
        m_noveltyTelemetry.typedRepairRequestCount = 0u;
        m_noveltyTelemetry.floatFallbackRequestCount = 0u;
        m_noveltyTelemetry.rejectPayloadCount = 0u;
    }

    void GpuHairViewer::ExecuteBudgetedPhysics()
    {
        if (!m_noveltyFlags.enableBudgetedPhysics)
        {
            return;
        }
        m_noveltyTelemetry.riskyStrandCount = 0u;
        m_noveltyTelemetry.repairTaskCount = 0u;
        m_noveltyTelemetry.repairCommitCount = 0u;
        m_noveltyTelemetry.repairRejectCount = 0u;
        m_noveltyTelemetry.repairDeferCount = 0u;
        m_noveltyTelemetry.islandCandidateCount = 0u;
        m_noveltyTelemetry.islandCount = 0u;
        m_noveltyTelemetry.islandMemberCount = 0u;
        m_noveltyTelemetry.islandCommitCount = 0u;
        m_noveltyTelemetry.islandRejectCount = 0u;
        m_noveltyTelemetry.physicsFallbackCount = 0u;
        m_noveltyTelemetry.maxPenetrationMm = 0.0f;
    }

    void GpuHairViewer::ExecuteCertificateAwareRendering()
    {
        if (!m_noveltyFlags.enableCertificateRender)
        {
            return;
        }
        m_noveltyTelemetry.renderCertificateCount = 0u;
        m_noveltyTelemetry.renderSkipCount = 0u;
        m_noveltyTelemetry.renderCoarseOpacityCount = 0u;
        m_noveltyTelemetry.renderStrandRasterCount = 0u;
        m_noveltyTelemetry.renderHighQualityShadowCount = 0u;
        m_noveltyTelemetry.renderHighQualitySpecularCount = 0u;
        m_noveltyTelemetry.renderFullQualityCount = 0u;
        m_noveltyTelemetry.renderDroppedFullQualityCount = 0u;
        m_noveltyTelemetry.p95ShadowErrorBound = 0.0f;
        m_noveltyTelemetry.p95SpecularErrorBound = 0.0f;
        m_noveltyTelemetry.temporalFlickerScore = 0.0f;
    }

    void GpuHairViewer::ExecutePerStrandEditing()
    {
        if (!m_noveltyFlags.enablePerStrandEdit)
        {
            return;
        }
        m_noveltyTelemetry.editOperationCount = 0u;
        m_noveltyTelemetry.editRepairTaskCount = 0u;
        m_noveltyTelemetry.editIslandEscalationCount = 0u;
        m_noveltyTelemetry.editCommitCount = 0u;
        m_noveltyTelemetry.editRejectCount = 0u;
        m_noveltyTelemetry.editPreviewFullQualityCount = 0u;
        m_noveltyTelemetry.editLatencyMs = 0.0f;
    }

    void GpuHairViewer::ExecuteGpuResidentRuntime()
    {
        if (!m_noveltyFlags.enableGpuResident)
        {
            return;
        }
        m_noveltyTelemetry.gpuResidentTaskCount = 0u;
        m_noveltyTelemetry.riskyCompactionCount = 0u;
        // Preserve the count produced by BuildSegmentGroupDispatchArguments().
        m_noveltyTelemetry.drawIndirectCount = 0u;
        m_noveltyTelemetry.delayedTelemetryReadCount = 0u;
    }

    Vec3 GpuHairViewer::ApplySampleScalpSkinning(const Vec3& restPosition, const SkinWeight4& weights, float elapsedSeconds) const
    {
        const float motionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
        const float neckYaw = std::sin(elapsedSeconds * 0.83f) * 0.07f * motionBlend;
        const float neckPitch = std::sin(elapsedSeconds * 1.07f) * 0.035f * motionBlend;
        const float headYaw = std::sin(elapsedSeconds * 0.91f) * 0.14f * motionBlend;
        const float headPitch = std::sin(elapsedSeconds * 1.39f) * 0.06f * motionBlend;
        const float bob = std::sin(elapsedSeconds * 1.15f) * 0.012f * motionBlend;

        const Mat4 neck = Multiply(Multiply(Translation(m_neckPivot + Vec3(0.0f, bob * 0.5f, 0.0f)), RotationY(neckYaw)), Multiply(RotationX(neckPitch), Translation(m_neckPivot * -1.0f)));
        const Vec3 headPivotWorld = TransformPoint(neck, m_scalpCenter);
        const Mat4 headLocal = Multiply(Multiply(Translation(headPivotWorld), RotationY(headYaw)), Multiply(RotationX(headPitch), Translation(headPivotWorld * -1.0f)));
        const Mat4 head = Multiply(neck, headLocal);

        Vec3 blended(0.0f, 0.0f, 0.0f);
        for (size_t i = 0; i < 4; ++i)
        {
            const float weight = weights.boneWeights[i];
            if (weight <= 0.0f)
            {
                continue;
            }
            const uint32_t boneIndex = weights.boneIndices[i];
            const Mat4& bone = (boneIndex == 0u) ? neck : head;
            blended += TransformPoint(bone, restPosition) * weight;
        }
        return blended;
    }

    void GpuHairViewer::UpdateAnimatedScalp(float elapsedSeconds)
    {
        if (!m_scalpPositions.buffer || m_restScalpParticles.empty())
        {
            return;
        }

        if (m_animatedScalpParticles.size() != m_restScalpParticles.size())
        {
            m_animatedScalpParticles = m_restScalpParticles;
        }

        const bool hasWeights = m_asset.scalpMesh.skinWeights.size() == m_asset.scalpMesh.positions.size() && !m_asset.scalpMesh.positions.empty();
        const bool hasBoneClip = hasWeights && !m_scalpAnimationClip.Empty() && m_scalpAnimationClip.boneCount > 0u;
        std::vector<Vec3> skinnedPositions;
        if (hasBoneClip)
        {
            if (hairgpu::EvaluateBoneAnimation(m_scalpAnimationClip, elapsedSeconds, true, m_currentScalpBoneMatrices))
            {
                skinnedPositions = hairgpu::ComputeSkinnedScalpPositions(m_asset.scalpMesh, m_currentScalpBoneMatrices);
            }
        }

        for (size_t i = 0; i < m_restScalpParticles.size(); ++i)
        {
            Vec3 p(m_restScalpParticles[i].x, m_restScalpParticles[i].y, m_restScalpParticles[i].z);
            if (!skinnedPositions.empty() && i < skinnedPositions.size())
            {
                p = skinnedPositions[i];
            }
            else if (hasWeights)
            {
                p = ApplySampleScalpSkinning(p, m_asset.scalpMesh.skinWeights[i], elapsedSeconds);
            }
            else if (m_enableScalpMotion)
            {
                const float phase = elapsedSeconds * 0.9f + static_cast<float>(i % 31u) * 0.071f;
                p.y += std::sin(phase) * 0.0025f;
                p.x += std::cos(phase * 0.7f) * 0.0015f;
            }
            m_animatedScalpParticles[i].x = p.x;
            m_animatedScalpParticles[i].y = p.y;
            m_animatedScalpParticles[i].z = p.z;
        }

        m_context->UpdateSubresource(m_scalpPositions.buffer.Get(), 0, nullptr, m_animatedScalpParticles.data(), 0, 0);
    }
    namespace
    {
        template <typename TConstants>
        void FillSharedColliderConstants(TConstants& constants, const hairgpu::ColliderSet& colliders)
        {
            constants.planeColliderCount = static_cast<uint32_t>(std::min<size_t>(colliders.planes.size(), 4u));
            constants.aabbColliderCount = static_cast<uint32_t>(std::min<size_t>(colliders.aabbs.size(), 4u));
            constants.useSdfCollider = colliders.sdfGrids.empty() ? 0u : 1u;
            constants.sdfValueCount = colliders.sdfGrids.empty() ? 0u : static_cast<uint32_t>(colliders.sdfGrids.front().values.size());

            for (size_t i = 0; i < static_cast<size_t>(constants.planeColliderCount); ++i)
            {
                const hairgpu::PlaneCollider& plane = colliders.planes[i];
                const hairgpu::Vec3 n = hairgpu::Normalize(plane.normal);
                const float d = -hairgpu::Dot(n, plane.point);
                constants.planeData[i] = { n.x, n.y, n.z, d };
            }

            for (size_t i = 0; i < static_cast<size_t>(constants.aabbColliderCount); ++i)
            {
                const hairgpu::AabbCollider& aabb = colliders.aabbs[i];
                constants.aabbData[i * 2 + 0] = { aabb.minBounds.x, aabb.minBounds.y, aabb.minBounds.z, 0.0f };
                constants.aabbData[i * 2 + 1] = { aabb.maxBounds.x, aabb.maxBounds.y, aabb.maxBounds.z, 0.0f };
            }

            if (!colliders.sdfGrids.empty())
            {
                const hairgpu::SdfGridCollider& sdf = colliders.sdfGrids.front();
                constants.sdfResolution[0] = sdf.resolutionX;
                constants.sdfResolution[1] = sdf.resolutionY;
                constants.sdfResolution[2] = sdf.resolutionZ;
                constants.sdfResolution[3] = 0u;
                constants.sdfBoundsMin = { sdf.boundsMin.x, sdf.boundsMin.y, sdf.boundsMin.z, 0.0f };
                constants.sdfBoundsMax = { sdf.boundsMax.x, sdf.boundsMax.y, sdf.boundsMax.z, 0.0f };
            }
        }
    }


    void GpuHairViewer::FillHairSimConstants(SimConstantsHair& constants, float dt) const
    {
        constants = SimConstantsHair{};
        constants.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
        constants.guideStrandCount = m_asset.guideStrandCount;
        constants.followerStrandCount = m_asset.followerStrandCount;
        constants.particlesPerStrand = m_asset.particlesPerStrand;
        constants.solverIterations = m_simConfig.solverIterations;
        constants.sphereColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.spheres.size(), 4u));
        constants.capsuleColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.capsules.size(), 4u));
        constants.enableSelfCollision = m_enableSelfCollision ? 1u : 0u;
        FillSharedColliderConstants(constants, m_colliders);
        constants.deltaTime = dt;
        constants.gravity = m_simConfig.gravity;
        constants.damping = m_simConfig.globalDamping;
        constants.segmentLength = m_simConfig.segmentLength;
        constants.bendStiffness = m_simConfig.bendStiffness;
        constants.followStiffness = m_simConfig.followStiffness;
        constants.windStrength = m_simConfig.windStrength;
        constants.floorY = m_simConfig.floorY;
        constants.elapsedTime = m_elapsedSeconds;
        constants.selfCollisionRadius = m_selfCollisionRadius;
        constants.selfCollisionStiffness = m_selfCollisionStiffness;
        constants.scalpMotionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
        constants.stretchCompliance = m_simConfig.stretchCompliance;
        constants.bendCompliance = m_simConfig.bendCompliance;
        constants.rootStiffness = m_simConfig.rootStiffness;
        constants.velocityShockPropagation = m_simConfig.velocityShockPropagation;
        constants.windDirection[0] = m_simConfig.windDirection.x;
        constants.windDirection[1] = m_simConfig.windDirection.y;
        constants.windDirection[2] = m_simConfig.windDirection.z;
        constants.gridCellSize = m_simConfig.selfCollisionCellSize;
        constants.gridDimX = m_simConfig.selfCollisionGridDimX;
        constants.gridDimY = m_simConfig.selfCollisionGridDimY;
        constants.gridDimZ = m_simConfig.selfCollisionGridDimZ;
        constants.gridCellCapacity = m_simConfig.selfCollisionCellCapacity;
        constants.gridOrigin[0] = m_simConfig.selfCollisionGridOrigin.x;
        constants.gridOrigin[1] = m_simConfig.selfCollisionGridOrigin.y;
        constants.gridOrigin[2] = m_simConfig.selfCollisionGridOrigin.z;
        constants.enableGridSelfCollision = m_enableGridSelfCollision ? 1u : 0u;
        constants.looseConstraintStiffness = m_simConfig.looseConstraintStiffness;
        constants.looseConstraintMaxDistance = m_simConfig.looseConstraintMaxDistance;
        constants.supportCollisionRadius = m_simConfig.supportCollisionRadius;
        constants.supportCollisionStiffness = m_simConfig.supportCollisionStiffness;
        constants.tangentialFriction = m_simConfig.tangentialFriction;
        constants.lateralFriction = m_simConfig.lateralFriction;
        constants.freezeBackThreshold = m_simConfig.freezeBackThreshold;
        constants.freezeBlend = m_simConfig.freezeBlend;
        constants.broadphaseMode = m_selfCollisionBroadphaseMode;
        constants.broadphaseHashBucketCount = std::max<uint32_t>(m_simConfig.selfCollisionHashBucketCount, 1u);
        constants.sortedPaddedEntryCount = std::max<uint32_t>(m_sortedBroadphasePaddedCount, 1u);
        constants.enableRuntimeAngularBend = m_enableRuntimeAngularBend ? 1u : 0u;
        constants.angularBendRadians = m_runtimeAngularBendRadians;
        constants.angularBendCompliance = m_runtimeAngularBendCompliance;
        constants.angularBendStiffness = m_runtimeAngularBendStiffness;
        constants.angularBendDamping = m_runtimeAngularBendDamping;
        for (size_t i = 0; i < static_cast<size_t>(constants.sphereColliderCount); ++i)
        {
            const SphereCollider& s = m_colliders.spheres[i];
            constants.sphereData[i] = { s.center.x, s.center.y, s.center.z, s.radius };
        }
        for (size_t i = 0; i < static_cast<size_t>(constants.capsuleColliderCount); ++i)
        {
            const CapsuleCollider& c = m_colliders.capsules[i];
            constants.capsuleData[i * 2 + 0] = { c.pointA.x, c.pointA.y, c.pointA.z, c.radiusA };
            constants.capsuleData[i * 2 + 1] = { c.pointB.x, c.pointB.y, c.pointB.z, c.radiusB };
        }
        const bool useActiveDispatch = m_enableActiveSetRuntime && m_activeSetPlan.summary.selectedGuideCount > 0u;
        uint32_t compactedGuidePointCount = 0u;
        if (useActiveDispatch)
        {
            for (uint32_t guideIndex : m_activeSetPlan.selectedGuideStrandIndices)
            {
                if (guideIndex < m_asset.strands.size())
                {
                    const uint32_t pointCount = m_asset.strands[guideIndex].pointCount;
                    if (pointCount > 1u)
                    {
                        compactedGuidePointCount += pointCount - 1u;
                    }
                }
            }
        }
        constants.followerUpdateStride = 1u;
        constants.followerUpdatePhase = 0u;
        constants.useCompactedGuideDispatch = useActiveDispatch ? 1u : 0u;
        constants.compactedGuideCount = useActiveDispatch ? static_cast<uint32_t>(m_activeSetPlan.selectedGuideStrandIndices.size()) : 0u;
        constants.useCompactedFollowerDispatch = 0u;
        constants.compactedFollowerCount = 0u;
        constants.useCompactedGuidePointDispatch = useActiveDispatch ? 1u : 0u;
        constants.compactedGuidePointCount = compactedGuidePointCount;
    }

    void GpuHairViewer::FillActiveSetSimConstants(SimConstantsActiveSet& constants, float dt) const
    {
        constants = SimConstantsActiveSet{};
        constants.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
        constants.guideStrandCount = m_asset.guideStrandCount;
        constants.followerStrandCount = m_asset.followerStrandCount;
        constants.particlesPerStrand = m_asset.particlesPerStrand;
        constants.solverIterations = m_simConfig.solverIterations;
        constants.sphereColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.spheres.size(), 4u));
        constants.capsuleColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.capsules.size(), 4u));
        constants.enableSelfCollision = m_enableSelfCollision ? 1u : 0u;
        FillSharedColliderConstants(constants, m_colliders);
        constants.deltaTime = dt;
        constants.gravity = m_simConfig.gravity;
        constants.damping = m_simConfig.globalDamping;
        constants.segmentLength = m_simConfig.segmentLength;
        constants.bendStiffness = m_simConfig.bendStiffness;
        constants.followStiffness = m_simConfig.followStiffness;
        constants.windStrength = m_simConfig.windStrength;
        constants.floorY = m_simConfig.floorY;
        constants.elapsedTime = m_elapsedSeconds;
        constants.selfCollisionRadius = m_selfCollisionRadius;
        constants.selfCollisionStiffness = m_selfCollisionStiffness;
        constants.scalpMotionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
        constants.stretchCompliance = m_simConfig.stretchCompliance;
        constants.bendCompliance = m_simConfig.bendCompliance;
        constants.rootStiffness = m_simConfig.rootStiffness;
        constants.velocityShockPropagation = m_simConfig.velocityShockPropagation;
        constants.windDirection[0] = m_simConfig.windDirection.x;
        constants.windDirection[1] = m_simConfig.windDirection.y;
        constants.windDirection[2] = m_simConfig.windDirection.z;
        constants.gridCellSize = m_simConfig.selfCollisionCellSize;
        constants.gridDimX = m_simConfig.selfCollisionGridDimX;
        constants.gridDimY = m_simConfig.selfCollisionGridDimY;
        constants.gridDimZ = m_simConfig.selfCollisionGridDimZ;
        constants.gridCellCapacity = m_simConfig.selfCollisionCellCapacity;
        constants.gridOrigin[0] = m_simConfig.selfCollisionGridOrigin.x;
        constants.gridOrigin[1] = m_simConfig.selfCollisionGridOrigin.y;
        constants.gridOrigin[2] = m_simConfig.selfCollisionGridOrigin.z;
        constants.enableGridSelfCollision = m_enableGridSelfCollision ? 1u : 0u;
        constants.looseConstraintStiffness = m_simConfig.looseConstraintStiffness;
        constants.looseConstraintMaxDistance = m_simConfig.looseConstraintMaxDistance;
        constants.supportCollisionRadius = m_simConfig.supportCollisionRadius;
        constants.supportCollisionStiffness = m_simConfig.supportCollisionStiffness;
        constants.tangentialFriction = m_simConfig.tangentialFriction;
        constants.lateralFriction = m_simConfig.lateralFriction;
        constants.freezeBackThreshold = m_simConfig.freezeBackThreshold;
        constants.freezeBlend = m_simConfig.freezeBlend;
        constants.broadphaseMode = m_selfCollisionBroadphaseMode;
        constants.broadphaseHashBucketCount = std::max<uint32_t>(m_simConfig.selfCollisionHashBucketCount, 1u);
        constants.sortedPaddedEntryCount = std::max<uint32_t>(m_sortedBroadphasePaddedCount, 1u);
        constants.enableRuntimeAngularBend = m_enableRuntimeAngularBend ? 1u : 0u;
        constants.angularBendRadians = m_runtimeAngularBendRadians;
        constants.angularBendCompliance = m_runtimeAngularBendCompliance;
        constants.angularBendStiffness = m_runtimeAngularBendStiffness;
        constants.angularBendDamping = m_runtimeAngularBendDamping;
        for (size_t i = 0; i < static_cast<size_t>(constants.sphereColliderCount); ++i)
        {
            const SphereCollider& s = m_colliders.spheres[i];
            constants.sphereData[i] = { s.center.x, s.center.y, s.center.z, s.radius };
        }
        for (size_t i = 0; i < static_cast<size_t>(constants.capsuleColliderCount); ++i)
        {
            const CapsuleCollider& c = m_colliders.capsules[i];
            constants.capsuleData[i * 2 + 0] = { c.pointA.x, c.pointA.y, c.pointA.z, c.radiusA };
            constants.capsuleData[i * 2 + 1] = { c.pointB.x, c.pointB.y, c.pointB.z, c.radiusB };
        }
        const bool useActiveDispatch = m_enableActiveSetRuntime && m_activeSetPlan.summary.selectedGuideCount > 0u;
        constants.followerUpdateStride = 1u;
        constants.followerUpdatePhase = 0u;
        constants.useCompactedFollowerDispatch = 0u;
        constants.compactedFollowerCount = 0u;
    }
