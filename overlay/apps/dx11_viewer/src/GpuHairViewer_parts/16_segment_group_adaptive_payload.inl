// GpuHairViewer.cpp optional SG-APT dynamic-payload runtime.
// Included by GpuHairViewer.cpp before shutdown_and_tail.

namespace hairgpu::viewer
{
    bool GpuHairViewer::CreateSgaptPayloadResources()
    {
        if (!m_device || m_segmentGroupCount == 0u)
        {
            return false;
        }

        const uint64_t requestedCapacity =
            (static_cast<uint64_t>(m_segmentGroupCount) *
             hairgpu::kSgaptDefaultPayloadCapacityPercent + 99ull) / 100ull;

        m_sgaptPayloadCapacity =
            static_cast<uint32_t>(
                std::max<uint64_t>(
                    1ull,
                    std::min<uint64_t>(
                        requestedCapacity,
                        hairgpu::kSgaptInvalidPayloadIndex - 1ull)));

        const std::vector<hairgpu::SegmentDynamicPayloadGpuData> zeroPayloads(
            m_sgaptPayloadCapacity);

        m_sgaptDynamicPayloadBuffer =
            CreateStructuredBuffer(
                sizeof(hairgpu::SegmentDynamicPayloadGpuData),
                m_sgaptPayloadCapacity,
                zeroPayloads.data(),
                true);

        const std::array<uint32_t, hairgpu::SgaptPayloadPoolCounter_Count>
            zeroCounters = {};

        m_sgaptPayloadPoolCountersBuffer =
            CreateStructuredBuffer(
                sizeof(uint32_t),
                static_cast<UINT>(zeroCounters.size()),
                zeroCounters.data(),
                true);

        D3D11_BUFFER_DESC constantDesc = {};
        constantDesc.ByteWidth = sizeof(hairgpu::SgaptPayloadConstants);
        constantDesc.Usage = D3D11_USAGE_DEFAULT;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        if (FAILED(
                m_device->CreateBuffer(
                    &constantDesc,
                    nullptr,
                    &m_sgaptPayloadConstants)))
        {
            return false;
        }

        return
            m_sgaptDynamicPayloadBuffer.buffer &&
            m_sgaptDynamicPayloadBuffer.srv &&
            m_sgaptDynamicPayloadBuffer.uav &&
            m_sgaptPayloadPoolCountersBuffer.buffer &&
            m_sgaptPayloadPoolCountersBuffer.uav &&
            m_sgaptPayloadConstants;
    }

    void GpuHairViewer::ResetSgaptPayloadResources()
    {
        if (!m_context)
        {
            return;
        }

        if (m_sgaptPayloadPoolCountersBuffer.buffer)
        {
            const std::array<uint32_t, hairgpu::SgaptPayloadPoolCounter_Count>
                zeroCounters = {};

            m_context->UpdateSubresource(
                m_sgaptPayloadPoolCountersBuffer.buffer.Get(),
                0u,
                nullptr,
                zeroCounters.data(),
                0u,
                0u);
        }

        if (m_sgaptDynamicPayloadBuffer.buffer &&
            m_sgaptPayloadCapacity > 0u)
        {
            const std::vector<hairgpu::SegmentDynamicPayloadGpuData>
                zeroPayloads(m_sgaptPayloadCapacity);

            m_context->UpdateSubresource(
                m_sgaptDynamicPayloadBuffer.buffer.Get(),
                0u,
                nullptr,
                zeroPayloads.data(),
                0u,
                0u);
        }
    }

    bool GpuHairViewer::UpdateSgaptPayloadConstants(float dt)
    {
        if (!m_context ||
            !m_sgaptPayloadConstants ||
            !m_sgaptDynamicPayloadBuffer.buffer ||
            !m_sgaptPayloadPoolCountersBuffer.buffer)
        {
            if (IsExperimentModeEnabled() &&
                m_experimentCase.strictValidationEnabled)
            {
                SetFatalExperimentFailure(
                    1210u,
                    L"SG-APT payload resources are unavailable.");
            }
            return false;
        }

        hairgpu::SgaptPayloadConstants constants = {};
        constants.groupCount = m_segmentGroupCount;
        constants.payloadCapacity = m_sgaptPayloadCapacity;
        constants.frameIndex = m_globalFrameIndex;
        constants.dynamicState =
            static_cast<uint32_t>(
                SegmentGroupRuntimeState::Dynamic);
        constants.deltaTime = std::max(dt, 1.0e-6f);

        m_context->UpdateSubresource(
            m_sgaptPayloadConstants.Get(),
            0u,
            nullptr,
            &constants,
            0u,
            0u);

        return true;
    }

    void GpuHairViewer::ExecuteSgaptPayloadTransitions()
    {
        if (!m_context ||
            !m_sgaptPayloadTransitionCS ||
            m_segmentGroupCount == 0u)
        {
            return;
        }

        ID3D11Buffer* constantBuffer =
            m_sgaptPayloadConstants.Get();

        ID3D11ShaderResourceView* srvs[] =
        {
            m_positions[m_currentBufferIndex].srv.Get(),
            m_previousPositions[m_currentBufferIndex].srv.Get()
        };

        ID3D11UnorderedAccessView* uavs[] =
        {
            m_segmentGroupBuffer.uav.Get(),
            m_sgaptDynamicPayloadBuffer.uav.Get(),
            m_sgaptPayloadPoolCountersBuffer.uav.Get()
        };

        m_context->CSSetShader(
            m_sgaptPayloadTransitionCS.Get(),
            nullptr,
            0u);
        m_context->CSSetConstantBuffers(
            1u,
            1u,
            &constantBuffer);
        m_context->CSSetShaderResources(
            0u,
            static_cast<UINT>(std::size(srvs)),
            srvs);
        m_context->CSSetUnorderedAccessViews(
            0u,
            static_cast<UINT>(std::size(uavs)),
            uavs,
            nullptr);

        m_context->Dispatch(
            std::max<UINT>(
                (m_segmentGroupCount + 63u) / 64u,
                1u),
            1u,
            1u);

        ID3D11ShaderResourceView* nullSrvs[2] =
        {
            nullptr,
            nullptr
        };
        ID3D11UnorderedAccessView* nullUavs[3] =
        {
            nullptr,
            nullptr,
            nullptr
        };
        ID3D11Buffer* nullConstant = nullptr;

        m_context->CSSetShaderResources(
            0u,
            static_cast<UINT>(std::size(nullSrvs)),
            nullSrvs);
        m_context->CSSetUnorderedAccessViews(
            0u,
            static_cast<UINT>(std::size(nullUavs)),
            nullUavs,
            nullptr);
        m_context->CSSetConstantBuffers(
            1u,
            1u,
            &nullConstant);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::ExecuteSgaptPayloadPreSolve()
    {
        if (!m_context ||
            !m_sgaptPayloadPreSolveCS ||
            !m_segmentGroupDynamicDispatchArgsBuffer)
        {
            return;
        }

        ID3D11Buffer* constantBuffer =
            m_sgaptPayloadConstants.Get();

        ID3D11ShaderResourceView* srvs[] =
        {
            m_positions[m_currentBufferIndex].srv.Get(),
            m_previousPositions[m_currentBufferIndex].srv.Get(),
            m_dynamicSegmentGroupIndicesBuffer.srv.Get(),
            m_segmentGroupCounterBuffer.srv.Get()
        };

        ID3D11UnorderedAccessView* uavs[] =
        {
            m_segmentGroupBuffer.uav.Get(),
            m_sgaptDynamicPayloadBuffer.uav.Get(),
            m_sgaptPayloadPoolCountersBuffer.uav.Get(),
            m_previousPositions[m_currentBufferIndex].uav.Get()
        };

        m_context->CSSetShader(
            m_sgaptPayloadPreSolveCS.Get(),
            nullptr,
            0u);
        m_context->CSSetConstantBuffers(
            1u,
            1u,
            &constantBuffer);
        m_context->CSSetShaderResources(
            0u,
            static_cast<UINT>(std::size(srvs)),
            srvs);
        m_context->CSSetUnorderedAccessViews(
            0u,
            static_cast<UINT>(std::size(uavs)),
            uavs,
            nullptr);

        m_context->DispatchIndirect(
            m_segmentGroupDynamicDispatchArgsBuffer.Get(),
            0u);

        ID3D11ShaderResourceView* nullSrvs[4] =
        {
            nullptr,
            nullptr,
            nullptr,
            nullptr
        };
        ID3D11UnorderedAccessView* nullUavs[4] =
        {
            nullptr,
            nullptr,
            nullptr,
            nullptr
        };
        ID3D11Buffer* nullConstant = nullptr;

        m_context->CSSetShaderResources(
            0u,
            static_cast<UINT>(std::size(nullSrvs)),
            nullSrvs);
        m_context->CSSetUnorderedAccessViews(
            0u,
            static_cast<UINT>(std::size(nullUavs)),
            nullUavs,
            nullptr);
        m_context->CSSetConstantBuffers(
            1u,
            1u,
            &nullConstant);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }

    void GpuHairViewer::ExecuteSgaptPayloadPostSolve()
    {
        if (!m_context ||
            !m_sgaptPayloadPostSolveCS ||
            !m_segmentGroupDynamicDispatchArgsBuffer)
        {
            return;
        }

        ID3D11Buffer* constantBuffer =
            m_sgaptPayloadConstants.Get();

        ID3D11ShaderResourceView* srvs[] =
        {
            m_positions[m_currentBufferIndex].srv.Get(),
            m_previousPositions[m_currentBufferIndex].srv.Get(),
            m_dynamicSegmentGroupIndicesBuffer.srv.Get(),
            m_segmentGroupCounterBuffer.srv.Get()
        };

        ID3D11UnorderedAccessView* uavs[] =
        {
            m_segmentGroupBuffer.uav.Get(),
            m_sgaptDynamicPayloadBuffer.uav.Get(),
            m_sgaptPayloadPoolCountersBuffer.uav.Get()
        };

        m_context->CSSetShader(
            m_sgaptPayloadPostSolveCS.Get(),
            nullptr,
            0u);
        m_context->CSSetConstantBuffers(
            1u,
            1u,
            &constantBuffer);
        m_context->CSSetShaderResources(
            0u,
            static_cast<UINT>(std::size(srvs)),
            srvs);
        m_context->CSSetUnorderedAccessViews(
            0u,
            static_cast<UINT>(std::size(uavs)),
            uavs,
            nullptr);

        m_context->DispatchIndirect(
            m_segmentGroupDynamicDispatchArgsBuffer.Get(),
            0u);

        ID3D11ShaderResourceView* nullSrvs[4] =
        {
            nullptr,
            nullptr,
            nullptr,
            nullptr
        };
        ID3D11UnorderedAccessView* nullUavs[3] =
        {
            nullptr,
            nullptr,
            nullptr
        };
        ID3D11Buffer* nullConstant = nullptr;

        m_context->CSSetShaderResources(
            0u,
            static_cast<UINT>(std::size(nullSrvs)),
            nullSrvs);
        m_context->CSSetUnorderedAccessViews(
            0u,
            static_cast<UINT>(std::size(nullUavs)),
            nullUavs,
            nullptr);
        m_context->CSSetConstantBuffers(
            1u,
            1u,
            &nullConstant);
        m_context->CSSetShader(nullptr, nullptr, 0u);
    }
}
