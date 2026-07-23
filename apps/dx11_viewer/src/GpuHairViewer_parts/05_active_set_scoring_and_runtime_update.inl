// GpuHairViewer.cpp ordered part 05: active_set_scoring_and_runtime_update
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

    void GpuHairViewer::UpdateCamera()
    {
        const float cosPitch = std::cos(m_cameraPitch);
        const float sinPitch = std::sin(m_cameraPitch);
        const float cosYaw = std::cos(m_cameraYaw);
        const float sinYaw = std::sin(m_cameraYaw);

        const Vec3 offset(
            cosPitch * sinYaw * m_cameraDistance,
            sinPitch * m_cameraDistance,
            cosPitch * cosYaw * m_cameraDistance);

        m_cameraPosition = m_cameraTarget + offset;
    }

    void GpuHairViewer::RecomputeAssetBounds()
    {
        bool initialized = false;
        Vec3 minP(0.0f, 0.0f, 0.0f);
        Vec3 maxP(0.0f, 0.0f, 0.0f);

        auto accumulatePoint = [&](const Vec3& p)
        {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            {
                return;
            }
            if (!initialized)
            {
                minP = p;
                maxP = p;
                initialized = true;
                return;
            }
            minP.x = std::min(minP.x, p.x);
            minP.y = std::min(minP.y, p.y);
            minP.z = std::min(minP.z, p.z);
            maxP.x = std::max(maxP.x, p.x);
            maxP.y = std::max(maxP.y, p.y);
            maxP.z = std::max(maxP.z, p.z);
        };

        if (!m_initialParticles.empty())
        {
            for (const GpuParticle& p : m_initialParticles)
            {
                accumulatePoint(Vec3(p.x, p.y, p.z));
            }
        }
        else
        {
            for (const Vec3& p : m_asset.restPoints)
            {
                accumulatePoint(p);
            }
        }

        if (!initialized)
        {
            minP = Vec3(-0.5f, 1.0f, -0.5f);
            maxP = Vec3(0.5f, 2.0f, 0.5f);
        }

        m_assetBoundsMin = minP;
        m_assetBoundsMax = maxP;
        m_assetBoundsCenter = (minP + maxP) * 0.5f;
        const Vec3 extents = maxP - minP;
        const float halfDiagonal = 0.5f * std::sqrt(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);
        m_assetBoundsRadius = std::max(halfDiagonal, 0.20f);
    }

    void GpuHairViewer::AutoFrameCameraToAsset(bool resetAngles)
    {
        RecomputeAssetBounds();

        if (resetAngles)
        {
            m_cameraYaw = 0.0f;
            m_cameraPitch = 0.05f;
        }

        m_cameraTarget = m_assetBoundsCenter;

        RECT renderRect = GetRenderViewportRect();
        const float renderWidth = static_cast<float>(std::max<LONG>(renderRect.right - renderRect.left, 1));
        const float renderHeight = static_cast<float>(std::max<LONG>(renderRect.bottom - renderRect.top, 1));
        const float aspectRatio = std::max(renderWidth / std::max(renderHeight, 1.0f), 0.6f);
        constexpr float kFovY = 60.0f * 3.1415926535f / 180.0f;
        const float fovX = 2.0f * std::atan(std::tan(kFovY * 0.5f) * aspectRatio);
        const float limitingHalfFov = std::max(0.30f, std::min(kFovY, fovX) * 0.5f);
        const float desiredDistance = (m_assetBoundsRadius / std::tan(limitingHalfFov)) * 1.35f;
        m_cameraDistance = Clamp(desiredDistance, 0.80f, 18.0f);
        UpdateCamera();
    }

    void GpuHairViewer::QueueVisibilityRecovery()
    {
        m_visibilityRecoveryPending = true;
        m_framesSinceVisibilityCheck = 0u;
    }

        bool GpuHairViewer::ShouldRunVisibilityRecoveryThisFrame() const
    {
        return !IsExperimentModeEnabled() &&
            m_cpuReadbackMode == CpuReadbackMode::FullParticlesDebug;
    }


    bool GpuHairViewer::RestoreGpuParticleStateFromInitialPose(bool resetCamera)
    {
        if (m_initialParticles.empty() || !m_positions[0].buffer || !m_positions[1].buffer ||
            !m_previousPositions[0].buffer || !m_previousPositions[1].buffer)
        {
            return false;
        }

        m_currentBufferIndex = 0u;
        m_context->UpdateSubresource(m_positions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_positions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        if (resetCamera)
        {
            AutoFrameCameraToAsset(true);
        }
        return true;
    }

    bool GpuHairViewer::RunVisibilityRecoveryPass(float aspectRatio, bool allowCameraRecovery, std::vector<GpuParticle>* outParticles)
    {
        std::vector<GpuParticle> particles;
        const bool readbackSucceeded = ReadBackCurrentParticlePositions(particles);
        const bool readbackValid = readbackSucceeded &&
            ParticleArrayLooksValid(reinterpret_cast<const float*>(particles.data()), particles.size(), m_asset);

        m_lastVisibilityReadbackValid = readbackValid;
        m_lastVisibilityUsedInitialPoseFallback = false;

        if (!readbackValid)
        {
            ++m_visibilityReadbackFailureCount;
            m_lastVisibilityUsedInitialPoseFallback = RestoreGpuParticleStateFromInitialPose(false);
            particles = m_initialParticles;
        }

        const bool haveParticles = !particles.empty();
        m_lastReadbackParticleCount = static_cast<uint32_t>(particles.size());
        m_lastEstimatedVisibleParticles = EstimateVisibleParticleCount(particles, aspectRatio);
        const uint32_t minimumExpectedVisibleParticles = std::max<uint32_t>(32u, static_cast<uint32_t>(std::max<size_t>(particles.size() / 160u, 1u)));
        m_lastVisibilitySuspect = !readbackValid || (m_lastEstimatedVisibleParticles < minimumExpectedVisibleParticles);

        if (allowCameraRecovery && m_lastVisibilitySuspect)
        {
            ++m_visibilityRecoveryCount;
            AutoFrameCameraToAsset(true);
            m_renderMode = RenderMode::StrandRibbon;
            m_widthScale = std::max(m_widthScale, 0.78f);

            if (!readbackValid)
            {
                m_lastVisibilityUsedInitialPoseFallback = RestoreGpuParticleStateFromInitialPose(false) || m_lastVisibilityUsedInitialPoseFallback;
            }

            m_lastEstimatedVisibleParticles = EstimateVisibleParticleCount(particles, aspectRatio);
            m_lastVisibilitySuspect = !readbackValid || (m_lastEstimatedVisibleParticles < minimumExpectedVisibleParticles);
        }

        m_visibilityRecoveryPending = m_lastVisibilitySuspect;
        m_framesSinceVisibilityCheck = 0u;
        m_consecutiveInvisibleFrames = m_lastVisibilitySuspect ? (m_consecutiveInvisibleFrames + 1u) : 0u;

        if (outParticles != nullptr)
        {
            *outParticles = particles;
        }

        return haveParticles;
    }

    uint32_t GpuHairViewer::EstimateVisibleParticleCount(const std::vector<GpuParticle>& particles, float aspectRatio) const
    {
        if (particles.empty())
        {
            return 0u;
        }

        const size_t stride = std::max<size_t>(particles.size() / 2048u, 1u);
        uint32_t visibleSamples = 0u;
        uint32_t totalSamples = 0u;
        for (size_t i = 0; i < particles.size(); i += stride)
        {
            const GpuParticle& p = particles[i];
            Vec3 ndc;
            if (!ProjectWorldToNdc(Vec3(p.x, p.y, p.z), m_cameraPosition, m_cameraTarget, aspectRatio, ndc))
            {
                continue;
            }
            ++totalSamples;
            if (std::fabs(ndc.x) <= 1.10f && std::fabs(ndc.y) <= 1.10f && ndc.z >= 0.0f && ndc.z <= 1.0f)
            {
                ++visibleSamples;
            }
        }

        if (totalSamples == 0u)
        {
            return 0u;
        }

        const double visibleRatio = static_cast<double>(visibleSamples) / static_cast<double>(totalSamples);
        const double estimated = visibleRatio * static_cast<double>(particles.size());
        return static_cast<uint32_t>(std::max(0.0, estimated));
    }

    void GpuHairViewer::Render()
    {
        if (!m_swapChain || !m_rtv || !m_dsv)
        {
            return;
        }

        const RECT renderRect = GetRenderViewportRect();
        const float renderWidth = static_cast<float>(std::max<LONG>(renderRect.right - renderRect.left, 1));
        const float renderHeight = static_cast<float>(std::max<LONG>(renderRect.bottom - renderRect.top, 1));

        RenderConstants rc = {};
        const Mat4 view = LookAtRH(m_cameraPosition, m_cameraTarget, Vec3(0.0f, 1.0f, 0.0f));
        const Mat4 projection = PerspectiveFovRH(60.0f * 3.1415926535f / 180.0f, renderWidth / renderHeight, 0.01f, 20.0f);
        const Mat4 viewProjection = Multiply(view, projection);
        FillRenderConstants(rc, viewProjection);
        D3D11_MAPPED_SUBRESOURCE mappedRender = {};
        if (SUCCEEDED(m_context->Map(m_renderConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedRender)))
        {
            std::memcpy(mappedRender.pData, &rc, sizeof(rc));
            m_context->Unmap(m_renderConstants.Get(), 0);
        }

        const float clearColor[4] = { 0.10f, 0.12f, 0.16f, 1.0f };
        m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
        m_context->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = static_cast<float>(renderRect.left);
        vp.TopLeftY = static_cast<float>(renderRect.top);
        vp.Width = renderWidth;
        vp.Height = renderHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);
        m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());
        m_context->RSSetState(m_rasterizerState.Get());
        m_context->OMSetDepthStencilState(m_depthState.Get(), 0);
        const float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);

        ID3D11Buffer* renderCb = m_renderConstants.Get();
        UINT offset = 0;

        const UINT hairStride = sizeof(SegmentRenderVertex);
        ID3D11Buffer* hairVB = m_segmentVertexBuffer.Get();
        ID3D11ShaderResourceView* hairSrvs[] = {
            m_positions[m_currentBufferIndex].srv.Get(),
            m_pointWidths.srv.Get(),
            m_renderStrandTiersBuffer.srv.Get()
        };

        GpuTimingFrame* gpuTimingFrame = (m_activeGpuTimingFrameIndex >= 0 && static_cast<size_t>(m_activeGpuTimingFrameIndex) < m_gpuTimingFrames.size())
            ? &m_gpuTimingFrames[static_cast<size_t>(m_activeGpuTimingFrameIndex)]
            : nullptr;
        m_lastCurrentHairDrawCalls = 0u;
        m_lastCurrentHairVertexCount = 0u;
        m_lastCurrentHairPrimitiveCount = 0u;
        m_lastCurrentHairInstanceCount = 0u;
        m_lastTotalDrawCalls = 0u;

        bool drewCpuHairMesh = false;
        m_lastUsedCpuVisibilityFallback = false;
        std::vector<GpuParticle> cpuMeshParticles;
        const float aspectRatio = renderWidth / renderHeight;
        const wchar_t* visualValidationValue = _wgetenv(L"HAIRGPU_VISUAL_VALIDATION");
        const bool visualValidationRequested = visualValidationValue &&
            std::wstring(visualValidationValue) == L"1";
        const bool runVisibilityRecovery =
            ShouldRunVisibilityRecoveryThisFrame() ||
            (visualValidationRequested && !IsExperimentModeEnabled());
        const bool haveReadbackParticles = runVisibilityRecovery
            ? RunVisibilityRecoveryPass(aspectRatio, true, &cpuMeshParticles)
            : false;

        if (!runVisibilityRecovery)
        {
            ++m_framesSinceVisibilityCheck;
        }

        if (haveReadbackParticles)
        {
            UpdateActiveSetRuntimeFromParticles(cpuMeshParticles);
        }

        const bool shouldDrawCpuFallback = !cpuMeshParticles.empty() &&
            m_cpuReadbackMode == CpuReadbackMode::FullParticlesDebug &&
            !IsExperimentModeEnabled();

        if (shouldDrawCpuFallback)
        {
            std::vector<hairgpu::Vec3> correctedPoints;
            correctedPoints.reserve(cpuMeshParticles.size());
            for (const GpuParticle& particle : cpuMeshParticles)
            {
                correctedPoints.push_back(hairgpu::Vec3(particle.x, particle.y, particle.z));
            }
            m_sparseCorrectiveReport = {};
            if (!correctedPoints.empty())
            {
                std::string correctiveError;
                if (hairgpu::ApplySparseCorrectivePhysics(m_asset,
                                                         m_activeSetPlan,
                                                         m_colliders,
                                                         correctedPoints,
                                                         m_sparseCorrectiveSettings,
                                                         &m_sparseCorrectiveReport,
                                                         correctiveError))
                {
                    for (size_t i = 0; i < std::min(correctedPoints.size(), cpuMeshParticles.size()); ++i)
                    {
                        cpuMeshParticles[i].x = correctedPoints[i].x;
                        cpuMeshParticles[i].y = correctedPoints[i].y;
                        cpuMeshParticles[i].z = correctedPoints[i].z;
                    }
                }
            }
            BuildHairMeshVertices(cpuMeshParticles, aspectRatio);
            if (m_hairMeshVertices.empty() && !cpuMeshParticles.empty())
            {
                AutoFrameCameraToAsset(true);
                BuildHairMeshVertices(cpuMeshParticles, aspectRatio);
            }
            if (!m_hairMeshVertices.empty() && EnsureHairMeshVertexCapacity(m_hairMeshVertices.size()) && m_hairMeshVertexBuffer && m_hairMeshInputLayout && m_hairMeshVS && m_hairMeshPS)
            {
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if (SUCCEEDED(m_context->Map(m_hairMeshVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                {
                    std::memcpy(mapped.pData, m_hairMeshVertices.data(), m_hairMeshVertices.size() * sizeof(HairMeshVertex));
                    m_context->Unmap(m_hairMeshVertexBuffer.Get(), 0);

                    const UINT cpuMeshStride = sizeof(HairMeshVertex);
                    ID3D11Buffer* cpuMeshVB = m_hairMeshVertexBuffer.Get();
                    m_context->IASetInputLayout(m_hairMeshInputLayout.Get());
                    m_context->IASetVertexBuffers(0, 1, &cpuMeshVB, &cpuMeshStride, &offset);
                    m_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
                    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    m_context->VSSetConstantBuffers(0, 0, nullptr);
                    m_context->PSSetConstantBuffers(0, 0, nullptr);
                    m_context->VSSetShader(m_hairMeshVS.Get(), nullptr, 0);
                    m_context->GSSetShader(nullptr, nullptr, 0);
                    m_context->PSSetShader(m_hairMeshPS.Get(), nullptr, 0);
                    m_context->Draw(static_cast<UINT>(m_hairMeshVertices.size()), 0);
                    drewCpuHairMesh = true;
                    m_lastUsedCpuVisibilityFallback = true;
                    m_lastCurrentHairDrawCalls = 1u;
                    m_lastCurrentHairVertexCount = static_cast<uint64_t>(m_hairMeshVertices.size());
                    m_lastCurrentHairPrimitiveCount = static_cast<uint64_t>(m_hairMeshVertices.size() / 3u);
                    m_lastCurrentHairInstanceCount = 1u;
                    ++m_lastTotalDrawCalls;
                }
            }
        }

        // Interactive preview hides the scalp color, but still needs the head
        // to occlude back-side transparent ribbons. Without this depth-only pass,
        // front and back hair layers blend together as a persistent inner shell.
        if (!drewCpuHairMesh && !m_showScalpMesh && m_depthOnlyBlendState &&
            m_scalpVertexBuffer && m_scalpIndexBuffer && m_scalpIndexCount > 0u)
        {
            const UINT scalpStride = sizeof(ScalpVertex);
            ID3D11Buffer* scalpVB = m_scalpVertexBuffer.Get();
            ID3D11ShaderResourceView* scalpSrv = m_scalpPositions.srv.Get();
            m_context->OMSetDepthStencilState(m_depthState.Get(), 0);
            const float depthBlendFactor[4] = { 0, 0, 0, 0 };
            m_context->OMSetBlendState(m_depthOnlyBlendState.Get(), depthBlendFactor, 0xFFFFFFFFu);
            m_context->IASetInputLayout(m_scalpInputLayout.Get());
            m_context->IASetVertexBuffers(0, 1, &scalpVB, &scalpStride, &offset);
            m_context->IASetIndexBuffer(m_scalpIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetConstantBuffers(0, 1, &renderCb);
            m_context->PSSetConstantBuffers(0, 1, &renderCb);
            m_context->VSSetShaderResources(0, 1, &scalpSrv);
            m_context->VSSetShader(m_scalpVS.Get(), nullptr, 0);
            m_context->GSSetShader(nullptr, nullptr, 0);
            m_context->PSSetShader(m_scalpPS.Get(), nullptr, 0);
            m_context->DrawIndexed(m_scalpIndexCount, 0, 0);
            ID3D11ShaderResourceView* nullScalpSrv[1] = {};
            m_context->VSSetShaderResources(0, 1, nullScalpSrv);
            const float normalBlendFactor[4] = { 0, 0, 0, 0 };
            m_context->OMSetBlendState(m_blendState.Get(), normalBlendFactor, 0xFFFFFFFFu);
            ++m_lastTotalDrawCalls;
        }

        if (!drewCpuHairMesh && m_showScalpMesh && m_scalpVertexBuffer && m_scalpIndexBuffer && m_scalpIndexCount > 0u)
        {
            const UINT scalpStride = sizeof(ScalpVertex);
            ID3D11Buffer* scalpVB = m_scalpVertexBuffer.Get();
            ID3D11ShaderResourceView* scalpSrv = m_scalpPositions.srv.Get();
            m_context->IASetInputLayout(m_scalpInputLayout.Get());
            m_context->IASetVertexBuffers(0, 1, &scalpVB, &scalpStride, &offset);
            m_context->IASetIndexBuffer(m_scalpIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetConstantBuffers(0, 1, &renderCb);
            m_context->PSSetConstantBuffers(0, 1, &renderCb);
            m_context->VSSetShaderResources(0, 1, &scalpSrv);
            m_context->VSSetShader(m_scalpVS.Get(), nullptr, 0);
            m_context->GSSetShader(nullptr, nullptr, 0);
            m_context->PSSetShader(m_scalpPS.Get(), nullptr, 0);
            m_context->DrawIndexed(m_scalpIndexCount, 0, 0);
            ID3D11ShaderResourceView* nullScalpSrv[1] = {};
            m_context->VSSetShaderResources(0, 1, nullScalpSrv);
            ++m_lastTotalDrawCalls;
        }

        if (gpuTimingFrame && gpuTimingFrame->issued)
        {
            m_context->End(gpuTimingFrame->hairDrawStart.Get());
            m_context->Begin(gpuTimingFrame->hairPipelineStats.Get());
        }

        if (!drewCpuHairMesh && m_positions[m_currentBufferIndex].srv && m_pointWidths.srv && m_renderStrandTiersBuffer.srv && !m_asset.restPoints.empty())
        {
            m_context->VSSetConstantBuffers(0, 1, &renderCb);
            m_context->GSSetConstantBuffers(0, 1, &renderCb);
            m_context->PSSetConstantBuffers(0, 1, &renderCb);
            m_context->VSSetShaderResources(0, 3, hairSrvs);

            if (m_renderMode == RenderMode::StrandRibbon && m_ribbonVS && m_ribbonPS)
            {
                m_context->OMSetDepthStencilState(m_hairDepthState.Get(), 0);
                m_context->IASetInputLayout(nullptr);
                m_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
                m_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0u);
                m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                m_context->VSSetShader(m_ribbonVS.Get(), nullptr, 0);
                m_context->GSSetShader(nullptr, nullptr, 0);
                m_context->PSSetShader(m_ribbonPS.Get(), nullptr, 0);
                const UINT verticesPerStrand = m_asset.particlesPerStrand * 2u;
                const UINT instanceCount = static_cast<UINT>(m_asset.strands.size());
                m_context->DrawInstanced(verticesPerStrand, instanceCount, 0u, 0u);
                m_lastCurrentHairDrawCalls = 1u;
                m_lastCurrentHairVertexCount = static_cast<uint64_t>(verticesPerStrand) * instanceCount;
                m_lastCurrentHairPrimitiveCount = static_cast<uint64_t>(m_asset.particlesPerStrand - 1u) * 2ull * instanceCount;
                m_lastCurrentHairInstanceCount = instanceCount;
                m_context->OMSetDepthStencilState(m_depthState.Get(), 0);
            }
            else if (m_particleVS && m_particlePS)
            {
                m_context->IASetInputLayout(nullptr);
                m_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
                m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
                m_context->VSSetShader(m_particleVS.Get(), nullptr, 0);
                m_context->GSSetShader((m_renderMode == RenderMode::ParticlePointSprite) ? m_particlePointGS.Get() : m_particleBillboardGS.Get(), nullptr, 0);
                m_context->PSSetShader(m_particlePS.Get(), nullptr, 0);
                const UINT particleCount = static_cast<UINT>(m_asset.restPoints.size());
                m_context->Draw(particleCount, 0);
                m_lastCurrentHairDrawCalls = 1u;
                m_lastCurrentHairVertexCount = static_cast<uint64_t>(particleCount);
                m_lastCurrentHairPrimitiveCount = static_cast<uint64_t>(particleCount) * 2ull;
                m_lastCurrentHairInstanceCount = 1u;
            }

            ID3D11ShaderResourceView* nullHairSrvs[3] = {};
            m_context->VSSetShaderResources(0, 3, nullHairSrvs);
        }

        if (gpuTimingFrame && gpuTimingFrame->issued)
        {
            m_context->End(gpuTimingFrame->hairPipelineStats.Get());
            m_context->End(gpuTimingFrame->hairDrawEnd.Get());
            m_context->End(gpuTimingFrame->debugDrawStart.Get());
            m_context->Begin(gpuTimingFrame->debugPipelineStats.Get());
        }
