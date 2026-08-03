// GpuHairViewer.cpp ordered part 17:
// Novelty 01 formal evidence, payload lifetime, dirty-range, and baseline layer.
// Included immediately after 16_segment_group_adaptive_payload_complete.inl.

    void GpuHairViewer::ExecuteNovelty01PreClassificationValidation()
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer || !runtimePointer->ready)
        {
            return;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;
        if ((runtime.featureMask &
             hairgpu::novelty01::Feature_PayloadOwnerCheck) == 0u)
        {
            return;
        }

        N01Constants constants = runtime.constants;
        constants.payloadGeneration = runtime.payloadReadIndex;
        m_context->UpdateSubresource(
            runtime.constantBuffer.Get(), 0u, nullptr, &constants, 0u, 0u);
        ID3D11Buffer* cb = runtime.constantBuffer.Get();
        m_context->CSSetConstantBuffers(0u, 1u, &cb);

        ID3D11ShaderResourceView* srvs[5] =
        {
            runtime.cores.srv.Get(),
            runtime.extensions.srv.Get(),
            runtime.payloadOwners[runtime.payloadReadIndex].srv.Get(),
            nullptr,
            nullptr
        };
        ID3D11UnorderedAccessView* uavs[4] =
        {
            runtime.cores.uav.Get(),
            runtime.extensions.uav.Get(),
            nullptr,
            runtime.counters.uav.Get()
        };
        srvs[0] = nullptr;
        srvs[1] = nullptr;
        m_context->CSSetShaderResources(0u, 5u, srvs);
        m_context->CSSetUnorderedAccessViews(0u, 4u, uavs, nullptr);
        m_context->CSSetShader(
            runtime.validatePayloadOwnersCS.Get(), nullptr, 0u);
        m_context->Dispatch(
            N01DispatchGroups(runtime.groupCount, 64u), 1u, 1u);
        N01UnbindCompute(m_context.Get());
    }

    void GpuHairViewer::ExecuteNovelty01PostCompactionLifecycle()
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer || !runtimePointer->ready)
        {
            return;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;
        if ((runtime.featureMask &
             hairgpu::novelty01::Feature_PayloadOwnerCheck) == 0u)
        {
            return;
        }

        const uint32_t oldGeneration = runtime.payloadReadIndex;
        const uint32_t newGeneration = runtime.payloadGeneration;
        N01Constants constants = runtime.constants;
        constants.payloadGeneration = newGeneration;
        m_context->UpdateSubresource(
            runtime.constantBuffer.Get(), 0u, nullptr, &constants, 0u, 0u);
        ID3D11Buffer* cb = runtime.constantBuffer.Get();
        m_context->CSSetConstantBuffers(0u, 1u, &cb);

        const auto bindLifecycle = [&]()
        {
            ID3D11ShaderResourceView* srvs[5] =
            {
                nullptr,
                nullptr,
                runtime.payloadOwners[oldGeneration].srv.Get(),
                nullptr,
                nullptr
            };
            ID3D11UnorderedAccessView* uavs[4] =
            {
                runtime.cores.uav.Get(),
                runtime.extensions.uav.Get(),
                runtime.payloadOwners[newGeneration].uav.Get(),
                runtime.counters.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 5u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 4u, uavs, nullptr);
        };

        bindLifecycle();
        m_context->CSSetShader(
            runtime.retirePayloadOwnersCS.Get(), nullptr, 0u);
        m_context->Dispatch(
            N01DispatchGroups(runtime.payloadCapacity, 64u), 1u, 1u);
        N01UnbindCompute(m_context.Get());

        ID3D11Buffer* cb2 = runtime.constantBuffer.Get();
        m_context->CSSetConstantBuffers(0u, 1u, &cb2);
        bindLifecycle();
        m_context->CSSetShader(
            runtime.writePayloadOwnersCS.Get(), nullptr, 0u);
        m_context->Dispatch(
            N01DispatchGroups(runtime.groupCount, 64u), 1u, 1u);
        N01UnbindCompute(m_context.Get());
    }

    void GpuHairViewer::ExecuteNovelty01DirtyRangePropagation()
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer || !runtimePointer->ready)
        {
            return;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;

        N01Constants constants = runtime.constants;
        m_context->UpdateSubresource(
            runtime.constantBuffer.Get(), 0u, nullptr, &constants, 0u, 0u);
        ID3D11Buffer* cb = runtime.constantBuffer.Get();
        m_context->CSSetConstantBuffers(0u, 1u, &cb);

        {
            ID3D11UnorderedAccessView* uavs[5] =
            {
                runtime.guideDirtyRanges.uav.Get(),
                runtime.followerDirtyRanges.uav.Get(),
                runtime.renderDirtyRanges.uav.Get(),
                runtime.followerFlags.uav.Get(),
                runtime.counters.uav.Get()
            };
            m_context->CSSetUnorderedAccessViews(0u, 5u, uavs, nullptr);
            m_context->CSSetShader(
                runtime.clearGuideDirtyRangesCS.Get(), nullptr, 0u);
            m_context->Dispatch(
                N01DispatchGroups(m_asset.guideStrandCount, 64u), 1u, 1u);
            N01UnbindCompute(m_context.Get());
        }

        {
            ID3D11Buffer* localCb = runtime.constantBuffer.Get();
            m_context->CSSetConstantBuffers(0u, 1u, &localCb);
            ID3D11ShaderResourceView* srvs[4] =
            {
                runtime.cores.srv.Get(),
                m_followBuffer.srv.Get(),
                runtime.guideDirtyRanges.srv.Get(),
                runtime.counters.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[5] =
            {
                runtime.guideDirtyRanges.uav.Get(),
                runtime.followerDirtyRanges.uav.Get(),
                runtime.renderDirtyRanges.uav.Get(),
                runtime.followerFlags.uav.Get(),
                runtime.counters.uav.Get()
            };
            srvs[2] = nullptr;
            m_context->CSSetShaderResources(0u, 4u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 5u, uavs, nullptr);
            m_context->CSSetShader(
                runtime.buildGuideDirtyRangesCS.Get(), nullptr, 0u);
            m_context->Dispatch(
                N01DispatchGroups(runtime.groupCount, 64u), 1u, 1u);
            N01UnbindCompute(m_context.Get());
        }

        {
            ID3D11Buffer* localCb = runtime.constantBuffer.Get();
            m_context->CSSetConstantBuffers(0u, 1u, &localCb);
            ID3D11ShaderResourceView* srvs[4] =
            {
                runtime.cores.srv.Get(),
                m_followBuffer.srv.Get(),
                runtime.guideDirtyRanges.srv.Get(),
                runtime.counters.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[5] =
            {
                nullptr,
                runtime.followerDirtyRanges.uav.Get(),
                runtime.renderDirtyRanges.uav.Get(),
                runtime.followerFlags.uav.Get(),
                runtime.counters.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 4u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 5u, uavs, nullptr);
            m_context->CSSetShader(
                runtime.propagateFollowerRenderRangesCS.Get(), nullptr, 0u);
            m_context->Dispatch(
                N01DispatchGroups(m_asset.followerStrandCount, 64u), 1u, 1u);
            N01UnbindCompute(m_context.Get());
        }

        {
            ID3D11Buffer* localCb = runtime.constantBuffer.Get();
            m_context->CSSetConstantBuffers(0u, 1u, &localCb);
            ID3D11ShaderResourceView* srvs[4] =
            {
                runtime.cores.srv.Get(),
                m_followBuffer.srv.Get(),
                runtime.guideDirtyRanges.srv.Get(),
                runtime.counters.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[5] =
            {
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                runtime.counters.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 4u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 5u, uavs, nullptr);
            m_context->CSSetShader(
                runtime.finalizeGuideDirtyRangeCountersCS.Get(), nullptr, 0u);
            m_context->Dispatch(
                N01DispatchGroups(m_asset.guideStrandCount, 64u), 1u, 1u);
            N01UnbindCompute(m_context.Get());
        }
    }

    void GpuHairViewer::ExecuteNovelty01BuildIndirectArguments()
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer || !runtimePointer->ready ||
            !runtimePointer->indirectDispatchReady)
        {
            return;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;
        if ((runtime.featureMask &
             hairgpu::novelty01::Feature_IndirectDispatch) == 0u)
        {
            return;
        }

        N01Constants constants = runtime.constants;
        m_context->UpdateSubresource(
            runtime.constantBuffer.Get(), 0u, nullptr, &constants, 0u, 0u);
        ID3D11Buffer* cb = runtime.constantBuffer.Get();
        m_context->CSSetConstantBuffers(0u, 1u, &cb);
        ID3D11ShaderResourceView* srvs[4] =
        {
            runtime.counters.srv.Get(),
            runtime.dynamicList.srv.Get(),
            runtime.staticList.srv.Get(),
            runtime.cores.srv.Get()
        };
        ID3D11UnorderedAccessView* uavs[4] =
        {
            runtime.indirectArgumentsUav.Get(),
            runtime.migratingDynamicCores.uav.Get(),
            runtime.migratingStaticCores.uav.Get(),
            runtime.counters.uav.Get()
        };
        m_context->CSSetShaderResources(0u, 4u, srvs);
        m_context->CSSetUnorderedAccessViews(0u, 4u, uavs, nullptr);
        m_context->CSSetShader(
            runtime.buildIndirectArgumentsCS.Get(), nullptr, 0u);
        m_context->Dispatch(1u, 1u, 1u);
        N01UnbindCompute(m_context.Get());
    }

    void GpuHairViewer::ExecuteNovelty01B4MigrationBaseline()
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer || !runtimePointer->ready ||
            (runtimePointer->featureMask &
             hairgpu::novelty01::Feature_FixedGroupIndex) != 0u)
        {
            return;
        }

        Novelty01RuntimeState& runtime = *runtimePointer;
        N01Constants constants = runtime.constants;
        m_context->UpdateSubresource(
            runtime.constantBuffer.Get(), 0u, nullptr, &constants, 0u, 0u);
        ID3D11Buffer* cb = runtime.constantBuffer.Get();
        m_context->CSSetConstantBuffers(0u, 1u, &cb);
        ID3D11ShaderResourceView* srvs[4] =
        {
            runtime.counters.srv.Get(),
            runtime.dynamicList.srv.Get(),
            runtime.staticList.srv.Get(),
            runtime.cores.srv.Get()
        };
        ID3D11UnorderedAccessView* uavs[4] =
        {
            runtime.indirectArgumentsUav.Get(),
            runtime.migratingDynamicCores.uav.Get(),
            runtime.migratingStaticCores.uav.Get(),
            runtime.counters.uav.Get()
        };
        m_context->CSSetShaderResources(0u, 4u, srvs);
        m_context->CSSetUnorderedAccessViews(0u, 4u, uavs, nullptr);
        m_context->CSSetShader(
            runtime.migrateCoreBuffersCS.Get(), nullptr, 0u);
        m_context->Dispatch(
            N01DispatchGroups(runtime.groupCount, 64u), 1u, 1u);
        N01UnbindCompute(m_context.Get());
    }

    bool GpuHairViewer::RunNovelty01GroundTruth(float dt)
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer || !runtimePointer->ready ||
            !runtimePointer->formalGroundTruthEnabled)
        {
            return false;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;
        if (!runtime.groundTruthPositions[0].buffer ||
            !runtime.groundTruthPositions[1].buffer ||
            !runtime.groundTruthPreviousPositions[0].buffer ||
            !runtime.groundTruthPreviousPositions[1].buffer)
        {
            return false;
        }

        const uint32_t proposalInputIndex = m_currentBufferIndex;
        m_context->CopyResource(
            runtime.groundTruthPositions[proposalInputIndex].buffer.Get(),
            m_positions[proposalInputIndex].buffer.Get());
        m_context->CopyResource(
            runtime.groundTruthPreviousPositions[proposalInputIndex].buffer.Get(),
            m_previousPositions[proposalInputIndex].buffer.Get());

        const uint32_t savedCurrentIndex = m_currentBufferIndex;
        const uint32_t savedFollowerPhase = m_runtimeFollowerUpdatePhase;
        const bool savedActiveSet = m_enableActiveSetRuntime;
        const auto savedTelemetry = m_noveltyTelemetry;

        for (uint32_t index = 0u; index < 2u; ++index)
        {
            std::swap(m_positions[index], runtime.groundTruthPositions[index]);
            std::swap(
                m_previousPositions[index],
                runtime.groundTruthPreviousPositions[index]);
        }
        std::swap(
            m_distanceLambdaBuffer,
            runtime.groundTruthDistanceLambdas);
        std::swap(
            m_bendLambdaBuffer,
            runtime.groundTruthBendLambdas);

        m_currentBufferIndex = savedCurrentIndex;
        m_enableActiveSetRuntime = false;

        StepSimulation(dt);
        runtime.groundTruthOutputBufferIndex = m_currentBufferIndex;

        std::swap(
            m_bendLambdaBuffer,
            runtime.groundTruthBendLambdas);
        std::swap(
            m_distanceLambdaBuffer,
            runtime.groundTruthDistanceLambdas);
        for (uint32_t index = 0u; index < 2u; ++index)
        {
            std::swap(
                m_previousPositions[index],
                runtime.groundTruthPreviousPositions[index]);
            std::swap(m_positions[index], runtime.groundTruthPositions[index]);
        }

        m_currentBufferIndex = savedCurrentIndex;
        m_runtimeFollowerUpdatePhase = savedFollowerPhase;
        m_enableActiveSetRuntime = savedActiveSet;
        m_noveltyTelemetry = savedTelemetry;
        return true;
    }

    void GpuHairViewer::CompareNovelty01GroundTruth()
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer || !runtimePointer->formalGroundTruthValidThisFrame)
        {
            return;
        }

        Novelty01RuntimeState& runtime = *runtimePointer;
        N01Constants constants = runtime.constants;
        constants.payloadGeneration = runtime.payloadReadIndex;
        m_context->UpdateSubresource(
            runtime.constantBuffer.Get(), 0u, nullptr, &constants, 0u, 0u);
        ID3D11Buffer* cb = runtime.constantBuffer.Get();
        m_context->CSSetConstantBuffers(0u, 1u, &cb);

        ID3D11ShaderResourceView* srvs[6] =
        {
            m_positions[m_currentBufferIndex].srv.Get(),
            runtime.groundTruthPositions[
                runtime.groundTruthOutputBufferIndex].srv.Get(),
            runtime.usesResampledRestPositions
                ? runtime.proposalRestPositions.srv.Get()
                : m_restPositions.srv.Get(),
            nullptr,
            runtime.extensions.srv.Get(),
            runtime.payloadOwners[runtime.payloadReadIndex].srv.Get()
        };
        ID3D11UnorderedAccessView* uavs[3] =
        {
            runtime.groundTruthMetrics.uav.Get(),
            runtime.cores.uav.Get(),
            runtime.counters.uav.Get()
        };
        m_context->CSSetShaderResources(0u, 6u, srvs);
        m_context->CSSetUnorderedAccessViews(0u, 3u, uavs, nullptr);
        m_context->CSSetShader(
            runtime.compareGroundTruthCS.Get(), nullptr, 0u);
        m_context->Dispatch(
            N01DispatchGroups(runtime.groupCount, 64u), 1u, 1u);
        N01UnbindCompute(m_context.Get());
    }

    void GpuHairViewer::ValidateNovelty01FormalInvariants()
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer || !runtimePointer->ready)
        {
            return;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;

        N01Constants constants = runtime.constants;
        constants.payloadGeneration = runtime.payloadReadIndex;
        m_context->UpdateSubresource(
            runtime.constantBuffer.Get(), 0u, nullptr, &constants, 0u, 0u);
        ID3D11Buffer* cb = runtime.constantBuffer.Get();
        m_context->CSSetConstantBuffers(0u, 1u, &cb);
        ID3D11ShaderResourceView* srvs[6] =
        {
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            runtime.extensions.srv.Get(),
            runtime.payloadOwners[runtime.payloadReadIndex].srv.Get()
        };
        ID3D11UnorderedAccessView* uavs[3] =
        {
            runtime.groundTruthMetrics.uav.Get(),
            runtime.cores.uav.Get(),
            runtime.counters.uav.Get()
        };
        m_context->CSSetShaderResources(0u, 6u, srvs);
        m_context->CSSetUnorderedAccessViews(0u, 3u, uavs, nullptr);
        m_context->CSSetShader(
            runtime.verifyInvariantsCS.Get(), nullptr, 0u);
        m_context->Dispatch(
            N01DispatchGroups(runtime.groupCount, 64u), 1u, 1u);
        N01UnbindCompute(m_context.Get());
    }

    void GpuHairViewer::ShutdownNovelty01Routed()
    {
        const N01RuntimePointer runtime = N01FindRuntime(this);
        if (runtime)
        {
            runtime->FlushFormalTelemetry();
            runtime->formalTelemetryRows.clear();
        }
        Shutdown();
        N01RuntimeInstances().erase(this);
    }

    void GpuHairViewer::WriteNovelty01FormalTelemetry()
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer || !runtimePointer->formalTelemetryEnabled)
        {
            return;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;

        if (runtime.formalTelemetryDirectory.empty())
        {
            runtime.formalTelemetryDirectory =
                GetEnvironmentVariableString(L"HAIRGPU_N01_TELEMETRY_DIR");
            if (runtime.formalTelemetryDirectory.empty())
            {
                runtime.formalTelemetryDirectory =
                    GetEnvironmentVariableString(L"HAIRGPU_TELEMETRY_DIR");
            }
            if (runtime.formalTelemetryDirectory.empty())
            {
                runtime.formalTelemetryDirectory =
                    std::filesystem::current_path() / L"novelty01_telemetry";
            }
            runtime.formalTelemetryRunName =
                GetEnvironmentVariableString(L"HAIRGPU_TELEMETRY_RUN_NAME");
            if (runtime.formalTelemetryRunName.empty())
            {
                runtime.formalTelemetryRunName = L"novelty01";
            }
        }

        Novelty01RuntimeState::FormalTelemetryRow row = {};
        row.frame = m_globalFrameIndex;
        row.sourceFrame = runtime.latestCounterSourceFrame;
        row.variant = static_cast<uint32_t>(runtime.variant);
        row.featureMask = runtime.featureMask;
        row.proposalMillisecondsEwma = runtime.proposalMillisecondsEwma;
        row.fullDynamicMillisecondsEwma = runtime.fullDynamicMillisecondsEwma;
        row.usedFullDynamic = runtime.lastFrameUsedFullDynamic ? 1u : 0u;
        row.counters = runtime.latestCounters.values;
        row.groundTruthSamples = runtime.formalAggregate.sampleCount;
        row.falseStaticSamples = runtime.formalAggregate.falseStaticCount;
        row.falseDynamicSamples = runtime.latestCounters.Get(
            hairgpu::novelty01::Counter_FalseDynamic);
        row.positionRmseMm = runtime.formalAggregate.PositionRmseMm();
        row.positionMaxMm = runtime.formalAggregate.maximumPositionErrorMm;
        row.followerErrorMm = runtime.formalAggregate.followerPositionErrorMm;
        row.renderErrorMm = runtime.formalAggregate.renderGeometryErrorMm;
        runtime.formalTelemetryRows.push_back(row);
    }
