// GpuHairViewer.cpp ordered part 03: simulation_reset_and_frame_step
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

UpdateScalpOverlayVertexBuffer();
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

        if (!CompileShaderFromFile(L"HairRibbon.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairRibbon.hlsl", "GSMain", "gs_5_0", &gsBlob, errorText) ||
            !CompileShaderFromFile(L"HairRibbon.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_ribbonVS)) ||
            FAILED(m_device->CreateGeometryShader(gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), nullptr, &m_ribbonGS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ribbonPS)))
        {
            return false;
        }

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
            !CompileShaderFromFile(L"HairSim.hlsl", "DistanceCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_distanceCS)) ||
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

        ComPtr<ID3DBlob> ribbonVsBlob;
        if (!CompileShaderFromFile(L"HairRibbon.hlsl", "VSMain", "vs_5_0", &ribbonVsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        const D3D11_INPUT_ELEMENT_DESC segmentLayout[] = {
            { "PARTICLE_INDEX", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(SegmentRenderVertex, particleIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "STRAND_INDEX", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(SegmentRenderVertex, strandIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(m_device->CreateInputLayout(segmentLayout, 2, ribbonVsBlob->GetBufferPointer(), ribbonVsBlob->GetBufferSize(), &m_segmentInputLayout)))
        {
            return false;
        }

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

        const uint32_t compactedFollowerCount = useActiveDispatch
            ? (std::max<uint32_t>(
                m_activeSetPlan.summary.selectedStrandCount,
                guideDispatchCount) - guideDispatchCount)
            : followerStrandCount;

        const uint32_t followerDispatchCount = useActiveDispatch
            ? compactedFollowerCount
            : followerStrandCount;

        const uint32_t sortedEntryCount =
            std::max<uint32_t>(m_sortedBroadphasePaddedCount, 1u);

        constexpr uint32_t kMaxD3D11DispatchGroupsX = 65535u;
        constexpr uint32_t kHairThreadsPerGroup = 64u;
        constexpr uint32_t kBitonicThreadsPerGroup = 256u;
        constexpr uint32_t kMaxBitonicItems = kMaxD3D11DispatchGroupsX * kBitonicThreadsPerGroup;

        uint32_t effectiveBroadphaseMode = m_selfCollisionBroadphaseMode;
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

        bindCommonSrvs();
        bindPrimaryUavs();
        updateHairConstants(0u);

        dispatchTiled(m_rootBindCS.Get(), totalStrandCount, kHairThreadsPerGroup);
        dispatchTiled(m_integrateCS.Get(), totalParticleCount, kHairThreadsPerGroup);

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

            dispatchTiled(m_followCS.Get(), followerDispatchCount, kHairThreadsPerGroup);
        }

        unbindComputeState();

        m_currentBufferIndex = outputBufferIndex;
        m_runtimeFollowerUpdatePhase += 1u;
    }
    void GpuHairViewer::ResetSimulation()
    {
        m_currentBufferIndex = 0;
        m_context->UpdateSubresource(m_positions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_positions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
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
        std::vector<uint32_t> zeroParticleNext(std::max<UINT>(static_cast<UINT>(m_initialParticles.size()), 1u), 0xFFFFFFFFu);
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
        m_lastSimulationStepMilliseconds = 0.0f;
        m_performanceSamples.clear();
        m_noveltyTelemetry = hairgpu::MakeZeroNoveltyTelemetry();
        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
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

    void GpuHairViewer::StepSingleFrame()
    {
        ResetNoveltyTelemetryForFrame();

        LARGE_INTEGER simBegin = {};
        LARGE_INTEGER simEnd = {};
        QueryPerformanceCounter(&simBegin);

        // Phase1 no-op hook order. These functions intentionally do not change
        // simulation/render output in Phase1; they only establish future extension
        // points and zero-valued telemetry placeholders.
        ExecutePerStrandEditing();
        EvaluateMicrotypeCertificates();
        ExecuteBudgetedPhysics();
        ExecuteGpuResidentRuntime();

        StepSimulation(m_simConfig.fixedTimeStep);

        ExecuteCertificateAwareRendering();

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
        m_noveltyTelemetry.topLevelNoveltyMask = hairgpu::BuildTopLevelNoveltyMask(m_noveltyFlags);
    }

    void GpuHairViewer::EvaluateMicrotypeCertificates()
    {
        if (!m_noveltyFlags.enableMicrotype)
        {
            return;
        }
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
        m_noveltyTelemetry.dispatchIndirectCount = 0u;
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
        constants.followerUpdateStride = useActiveDispatch ? std::max<uint32_t>(1u, (m_activeSetPlan.summary.cachedTierCount > 0u) ? 4u : ((m_activeSetPlan.summary.reducedTierCount > 0u) ? 2u : 1u)) : 1u;
        constants.followerUpdatePhase = useActiveDispatch ? (m_runtimeFollowerUpdatePhase % std::max<uint32_t>(constants.followerUpdateStride, 1u)) : 0u;
        constants.useCompactedGuideDispatch = useActiveDispatch ? 1u : 0u;
        constants.compactedGuideCount = useActiveDispatch ? static_cast<uint32_t>(m_activeSetPlan.selectedGuideStrandIndices.size()) : 0u;
        constants.useCompactedFollowerDispatch = useActiveDispatch ? 1u : 0u;
        constants.compactedFollowerCount = useActiveDispatch ? std::max<uint32_t>(m_activeSetPlan.summary.selectedStrandCount, constants.compactedGuideCount) - constants.compactedGuideCount : 0u;
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
        constants.followerUpdateStride = useActiveDispatch ? std::max<uint32_t>(1u, (m_activeSetPlan.summary.cachedTierCount > 0u) ? 4u : ((m_activeSetPlan.summary.reducedTierCount > 0u) ? 2u : 1u)) : 1u;
        constants.followerUpdatePhase = useActiveDispatch ? (m_runtimeFollowerUpdatePhase % std::max<uint32_t>(constants.followerUpdateStride, 1u)) : 0u;
        constants.useCompactedFollowerDispatch = useActiveDispatch ? 1u : 0u;
        constants.compactedFollowerCount = useActiveDispatch ? std::max<uint32_t>(m_activeSetPlan.summary.selectedStrandCount, static_cast<uint32_t>(m_activeSetPlan.selectedGuideStrandIndices.size())) - static_cast<uint32_t>(m_activeSetPlan.selectedGuideStrandIndices.size()) : 0u;
    }
