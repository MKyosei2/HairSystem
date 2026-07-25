// GpuHairViewer.cpp ordered part 04: constant_buffers_and_active_set_sync
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

void GpuHairViewer::FillScalpSimConstants(SimConstantsScalp& constants, float dt) const
{
    constants = SimConstantsScalp{};
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
    constants.scalpCenter[0] = m_scalpCenter.x;
    constants.scalpCenter[1] = m_scalpCenter.y;
    constants.scalpCenter[2] = m_scalpCenter.z;
    constants.scalpCenter[3] = 0.0f;
    constants.neckPivot[0] = m_neckPivot.x;
    constants.neckPivot[1] = m_neckPivot.y;
    constants.neckPivot[2] = m_neckPivot.z;
    constants.neckPivot[3] = 0.0f;
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
}

void GpuHairViewer::FillRenderConstants(RenderConstants& constants, const Mat4& viewProjection) const
{
    constants = RenderConstants{};
    for (int i = 0; i < 16; ++i)
    {
        constants.viewProjection[i] = viewProjection.m[i];
    }
    constants.cameraPosition[0] = m_cameraPosition.x;
    constants.cameraPosition[1] = m_cameraPosition.y;
    constants.cameraPosition[2] = m_cameraPosition.z;
    constants.cameraPosition[3] = 1.0f;
    constants.lightDirection[0] = -0.4f;
    constants.lightDirection[1] = 0.9f;
    constants.lightDirection[2] = 0.2f;
    constants.lightDirection[3] = 0.0f;
    constants.widthScale = m_widthScale;
    // Normal Groom/Edit operation must remain a natural hair preview.
    // Editability colors are shown only when the user explicitly selects that
    // debug mode; forcing them here made the normal viewport dark/green.
    const bool editVisualization = !IsExperimentModeEnabled() &&
        (m_debugColorMode == DebugColorMode::Editability);
    constants.debugColorMode = static_cast<uint32_t>(m_debugColorMode);
    constants.guideStrandCount = m_asset.guideStrandCount;
    constants.particlesPerStrand = m_asset.particlesPerStrand;
    constants.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
    constants.tubeRingSides = m_strandTubeRingSides;
    const bool fastEditPreview = m_fastGroomViewport && IsGroomWorkspace();
    const bool heavyInteraction = m_brushStrokeActive || m_gizmoDragging;
    // Keep every follower visible at rest. Temporary 2x decimation is allowed
    // only while the user is actively dragging; the old idle stride of 2
    // permanently removed half the hairstyle and exposed regular bald bands.
    constants.renderFollowerStride = (fastEditPreview && heavyInteraction) ? 2u : 1u;
    // Simulation tiers describe update frequency, not render visibility.
    constants.renderGuideOnlyAboveTier = 0xFFFFFFFFu;
    constants.renderReducedWidthBoost = 1.0f;
    constants.renderHiddenWidthThreshold = 0.0f;
    constants.renderStrandAlpha = 1.0f;
    if (IsExperimentModeEnabled())
    {
        constants.renderFollowerStride = 1u;
        constants.renderGuideOnlyAboveTier = 4u;
        constants.renderReducedWidthBoost = 1.0f;
        constants.renderHiddenWidthThreshold = 0.0f;
        constants.renderStrandAlpha = 1.0f;
    }
    constants.renderPadding0 = 0.0f;
}

void GpuHairViewer::SyncActiveSetGpuBuffers()
{
    const UINT totalStrands = static_cast<UINT>(std::max<size_t>(m_asset.strands.size(), 1u));
    const UINT guideCount = std::max<UINT>(m_asset.guideStrandCount, 1u);
    const UINT followerCount = std::max<UINT>(m_asset.followerStrandCount, 1u);
    std::vector<uint32_t> renderTiers(totalStrands, static_cast<uint32_t>(hairgpu::SimTier::Reduced));
    std::vector<uint32_t> guideTiers(guideCount, static_cast<uint32_t>(hairgpu::SimTier::Full));
    std::vector<uint32_t> followerTiers(followerCount, static_cast<uint32_t>(hairgpu::SimTier::Reduced));

    for (uint32_t strandIndex = 0u; strandIndex < totalStrands; ++strandIndex)
    {
        const bool isGuide = strandIndex < m_asset.guideStrandCount;
        uint32_t tier = isGuide ? static_cast<uint32_t>(hairgpu::SimTier::Full)
            : static_cast<uint32_t>(hairgpu::SimTier::Reduced);
        if (strandIndex < m_activeSetPlan.recommendedSimulationTiers.size())
        {
            tier = static_cast<uint32_t>(m_activeSetPlan.recommendedSimulationTiers[strandIndex]);
        }
        renderTiers[strandIndex] = tier;
        if (isGuide)
        {
            guideTiers[strandIndex] = tier;
        }
        else if ((strandIndex - m_asset.guideStrandCount) < followerTiers.size())
        {
            followerTiers[strandIndex - m_asset.guideStrandCount] = tier;
        }
    }

    // Keep render/simulation tiers and edit categories in independent arrays.
    // The previous code overwrote renderTiers with category values 0..4; the
    // instanced shader then interpreted categories 3/4 as hidden LOD tiers.
    std::vector<uint32_t> editCategories(totalStrands, 1u);
    if (!m_asset.strands.empty() && m_interactionMode == InteractionMode::Edit)
    {
        BuildEditVisualizationCategories(editCategories);
        m_editVisualizationCategories = editCategories;
    }
    else
    {
        m_editVisualizationCategories.clear();
    }

    std::vector<uint32_t> compactedGuideIndices(guideCount, 0u);
    std::vector<uint32_t> compactedFollowerIndices(followerCount, 0u);
    uint32_t compactedGuideCount = 0u;
    uint32_t compactedFollowerCount = 0u;
    const uint32_t runtimeStride = std::max<uint32_t>(1u, (m_activeSetPlan.summary.cachedTierCount > 0u) ? 4u : ((m_activeSetPlan.summary.reducedTierCount > 0u) ? 2u : 1u));
    const uint32_t runtimePhase = (runtimeStride > 1u) ? (m_runtimeFollowerUpdatePhase % runtimeStride) : 0u;

    for (uint32_t guideIndex = 0u; guideIndex < m_asset.guideStrandCount; ++guideIndex)
    {
        if (guideTiers[guideIndex] == static_cast<uint32_t>(hairgpu::SimTier::Full))
        {
            compactedGuideIndices[compactedGuideCount++] = guideIndex;
        }
    }
    for (uint32_t followerIndex = 0u; followerIndex < m_asset.followerStrandCount; ++followerIndex)
    {
        const uint32_t tier = followerTiers[followerIndex];
        bool active = (tier == static_cast<uint32_t>(hairgpu::SimTier::Full));
        if (!active && tier == static_cast<uint32_t>(hairgpu::SimTier::Reduced))
        {
            active = ((followerIndex + runtimePhase) % std::max<uint32_t>(runtimeStride, 2u)) == 0u;
        }
        if (!active && tier == static_cast<uint32_t>(hairgpu::SimTier::Cached))
        {
            active = ((followerIndex + runtimePhase) % std::max<uint32_t>(runtimeStride, 4u)) == 0u;
        }
        if (active)
        {
            compactedFollowerIndices[compactedFollowerCount++] = followerIndex;
        }
    }
    std::array<uint32_t, 4> guideCounters = { compactedGuideCount, 0u, 0u, 0u };
    std::array<uint32_t, 4> followerCounters = { compactedFollowerCount, 0u, 0u, 0u };

    if (m_renderStrandTiersBuffer.buffer)
    {
        m_context->UpdateSubresource(m_renderStrandTiersBuffer.buffer.Get(), 0, nullptr, renderTiers.data(), 0, 0);
    }
    if (m_editVisualizationCategoryBuffer.buffer)
    {
        m_context->UpdateSubresource(
            m_editVisualizationCategoryBuffer.buffer.Get(),
            0,
            nullptr,
            editCategories.data(),
            0,
            0);
    }
    if (m_guideTierAssignmentsBuffer.buffer)
    {
        m_context->UpdateSubresource(m_guideTierAssignmentsBuffer.buffer.Get(), 0, nullptr, guideTiers.data(), 0, 0);
    }
    if (m_followerTierAssignmentsBuffer.buffer)
    {
        m_context->UpdateSubresource(m_followerTierAssignmentsBuffer.buffer.Get(), 0, nullptr, followerTiers.data(), 0, 0);
    }
    if (m_compactedGuideIndicesBuffer.buffer)
    {
        m_context->UpdateSubresource(m_compactedGuideIndicesBuffer.buffer.Get(), 0, nullptr, compactedGuideIndices.data(), 0, 0);
    }
    if (m_compactedGuideCounterBuffer.buffer)
    {
        m_context->UpdateSubresource(m_compactedGuideCounterBuffer.buffer.Get(), 0, nullptr, guideCounters.data(), 0, 0);
    }
    if (m_compactedFollowerIndicesBuffer.buffer)
    {
        m_context->UpdateSubresource(m_compactedFollowerIndicesBuffer.buffer.Get(), 0, nullptr, compactedFollowerIndices.data(), 0, 0);
    }
    if (m_compactedFollowerCounterBuffer.buffer)
    {
        m_context->UpdateSubresource(m_compactedFollowerCounterBuffer.buffer.Get(), 0, nullptr, followerCounters.data(), 0, 0);
    }
}

void GpuHairViewer::RebuildActiveSetFoundation()
{
    m_activeSetClusters = hairgpu::BuildActiveSetClusters(m_asset, m_activeSetClusterSettings);
    m_activeSetPlan = {};
    m_activeSetGuideContactScores.clear();
    m_activeSetGuideUncertainty.clear();
    m_activeSetEvaluationMilliseconds = 0.0f;
    m_sparseCorrectiveReport = {};

    const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
    m_activeSetPreviousGuideRoots.assign(guideCount, hairgpu::Vec3(0.0f, 0.0f, 0.0f));
    for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
    {
        const hairgpu::StrandRange& strand = m_asset.strands[guideIndex];
        if (strand.pointCount > 0u && strand.pointOffset < m_asset.restPoints.size())
        {
            m_activeSetPreviousGuideRoots[guideIndex] = m_asset.restPoints[strand.pointOffset];
        }
    }

    std::wstringstream ss;
    ss << L"Ready (clusters=" << m_activeSetClusters.size() << L")";
    m_activeSetLastStatus = ss.str();
}

std::vector<hairgpu::Vec3> GpuHairViewer::BuildGuideRootPositionsFromParticles(const std::vector<GpuParticle>& particles) const
{
    const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
    std::vector<hairgpu::Vec3> roots(guideCount, hairgpu::Vec3(0.0f, 0.0f, 0.0f));
    for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
    {
        const hairgpu::StrandRange& strand = m_asset.strands[guideIndex];
        if (strand.pointCount > 0u && strand.pointOffset < particles.size())
        {
            const GpuParticle& rootParticle = particles[strand.pointOffset];
            roots[guideIndex] = hairgpu::Vec3(rootParticle.x, rootParticle.y, rootParticle.z);
        }
        else if (strand.pointCount > 0u && strand.pointOffset < m_asset.restPoints.size())
        {
            roots[guideIndex] = m_asset.restPoints[strand.pointOffset];
        }
    }
    return roots;
}


std::vector<float> GpuHairViewer::BuildGuideContactScoresFromParticles(const std::vector<GpuParticle>& particles) const
{
    const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
    std::vector<float> scores(guideCount, 0.0f);
    for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
    {
        const hairgpu::StrandRange& strand = m_asset.strands[guideIndex];
        if (strand.pointCount == 0u)
        {
            continue;
        }

        const uint32_t sampleOffsets[3] = { 0u, strand.pointCount / 2u, strand.pointCount - 1u };
        float bestScore = 0.0f;
        for (uint32_t sampleOffset : sampleOffsets)
        {
            const uint32_t particleIndex = strand.pointOffset + std::min<uint32_t>(sampleOffset, strand.pointCount - 1u);
            if (particleIndex >= particles.size())
            {
                continue;
            }

            const GpuParticle& particle = particles[particleIndex];
            const hairgpu::Vec3 p(particle.x, particle.y, particle.z);
            float proximityScore = hairgpu::Clamp((0.030f - (p.y - m_simConfig.floorY)) / 0.030f, 0.0f, 1.0f);
            for (const hairgpu::SphereCollider& sphere : m_colliders.spheres)
            {
                const float surfaceDistance = DistanceToSphereSurface(p, sphere);
                const float depth = std::max(-surfaceDistance, 0.0f);
                proximityScore = std::max(proximityScore, hairgpu::Clamp(depth / 0.020f + (0.040f - surfaceDistance) / 0.040f, 0.0f, 1.0f));
            }
            for (const hairgpu::CapsuleCollider& capsule : m_colliders.capsules)
            {
                const float surfaceDistance = hairgpu::QuerySignedDistanceToCapsule(capsule, p).signedDistance;
                const float depth = std::max(-surfaceDistance, 0.0f);
                proximityScore = std::max(proximityScore, hairgpu::Clamp(depth / 0.020f + (0.045f - surfaceDistance) / 0.045f, 0.0f, 1.0f));
            }
            for (const hairgpu::PlaneCollider& plane : m_colliders.planes)
            {
                const float signedDistance = hairgpu::Dot(p - plane.point, hairgpu::Normalize(plane.normal));
                const float depth = std::max(-signedDistance, 0.0f);
                proximityScore = std::max(proximityScore, hairgpu::Clamp(depth / 0.020f + (0.035f - signedDistance) / 0.035f, 0.0f, 1.0f));
            }
            for (const hairgpu::AabbCollider& aabb : m_colliders.aabbs)
            {
                const bool inside = p.x >= aabb.minBounds.x && p.x <= aabb.maxBounds.x &&
                    p.y >= aabb.minBounds.y && p.y <= aabb.maxBounds.y &&
                    p.z >= aabb.minBounds.z && p.z <= aabb.maxBounds.z;
                float distance = 0.0f;
                if (inside)
                {
                    const float dx = std::min(p.x - aabb.minBounds.x, aabb.maxBounds.x - p.x);
                    const float dy = std::min(p.y - aabb.minBounds.y, aabb.maxBounds.y - p.y);
                    const float dz = std::min(p.z - aabb.minBounds.z, aabb.maxBounds.z - p.z);
                    distance = -std::min(dx, std::min(dy, dz));
                }
                else
                {
                    const float qx = std::max(std::max(aabb.minBounds.x - p.x, 0.0f), p.x - aabb.maxBounds.x);
                    const float qy = std::max(std::max(aabb.minBounds.y - p.y, 0.0f), p.y - aabb.maxBounds.y);
                    const float qz = std::max(std::max(aabb.minBounds.z - p.z, 0.0f), p.z - aabb.maxBounds.z);
                    distance = std::sqrt(qx * qx + qy * qy + qz * qz);
                }
                const float depth = std::max(-distance, 0.0f);
                proximityScore = std::max(proximityScore, hairgpu::Clamp(depth / 0.020f + (0.035f - distance) / 0.035f, 0.0f, 1.0f));
            }
            for (const hairgpu::SdfGridCollider& grid : m_colliders.sdfGrids)
            {
                if (grid.values.empty() || grid.resolutionX == 0u || grid.resolutionY == 0u || grid.resolutionZ == 0u)
                {
                    continue;
                }
                const hairgpu::Vec3 size = grid.boundsMax - grid.boundsMin;
                const float nx = hairgpu::Clamp((p.x - grid.boundsMin.x) / std::max(size.x, 1.0e-5f), 0.0f, 1.0f);
                const float ny = hairgpu::Clamp((p.y - grid.boundsMin.y) / std::max(size.y, 1.0e-5f), 0.0f, 1.0f);
                const float nz = hairgpu::Clamp((p.z - grid.boundsMin.z) / std::max(size.z, 1.0e-5f), 0.0f, 1.0f);
                const uint32_t ix = std::min<uint32_t>(static_cast<uint32_t>(nx * static_cast<float>(grid.resolutionX - 1u)), grid.resolutionX - 1u);
                const uint32_t iy = std::min<uint32_t>(static_cast<uint32_t>(ny * static_cast<float>(grid.resolutionY - 1u)), grid.resolutionY - 1u);
                const uint32_t iz = std::min<uint32_t>(static_cast<uint32_t>(nz * static_cast<float>(grid.resolutionZ - 1u)), grid.resolutionZ - 1u);
                const size_t linearIndex = (static_cast<size_t>(iz) * grid.resolutionY + iy) * grid.resolutionX + ix;
                if (linearIndex < grid.values.size())
                {
                    const float sdf = grid.values[linearIndex];
                    const float depth = std::max(-sdf, 0.0f);
                    proximityScore = std::max(proximityScore, hairgpu::Clamp(depth / 0.020f + (0.035f - sdf) / 0.035f, 0.0f, 1.0f));
                }
            }
            bestScore = std::max(bestScore, proximityScore);
        }
        scores[guideIndex] = bestScore;
    }
    return scores;
}

void GpuHairViewer::UpdateActiveSetRuntimeFromParticles(const std::vector<GpuParticle>& particles)
{
    if (!m_enableActiveSetRuntime || m_activeSetClusters.empty())
    {
        m_activeSetPlan = {};
        m_activeSetLastStatus = L"Disabled";
        m_activeSetEvaluationMilliseconds = 0.0f;
        SyncActiveSetGpuBuffers();
        if (IsActiveSetRiskOverlayMode())
        {
            UpdateScalpOverlayVertexBuffer();
        }
        return;
    }

    const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
    if (guideCount == 0u)
    {
        m_activeSetPlan = {};
        m_activeSetLastStatus = L"No guides";
        m_activeSetEvaluationMilliseconds = 0.0f;
        SyncActiveSetGpuBuffers();
        if (IsActiveSetRiskOverlayMode())
        {
            UpdateScalpOverlayVertexBuffer();
        }
        return;
    }

    const std::vector<hairgpu::Vec3> currentGuideRoots = BuildGuideRootPositionsFromParticles(particles);
    if (m_activeSetPreviousGuideRoots.size() != currentGuideRoots.size())
    {
        m_activeSetPreviousGuideRoots = currentGuideRoots;
    }
    m_activeSetGuideContactScores = BuildGuideContactScoresFromParticles(particles);
    m_activeSetGuideUncertainty.assign(guideCount, 0.0f);
    std::vector<float> guideEditScores = m_runtimeEditState.guideInfluence;
    if (guideEditScores.size() != guideCount)
    {
        guideEditScores.assign(guideCount, 0.0f);
    }
    std::vector<float> guidePenetrationScores(guideCount, 0.0f);
    std::vector<float> guideBoundaryScores(guideCount, 0.0f);
    std::vector<float> guideTemporalScores(guideCount, 0.0f);
    std::vector<float> guideLeakageScores(guideCount, 0.0f);
    std::vector<float> guideModelErrorScores(guideCount, 0.0f);
    const float safeDt = std::max(m_simConfig.fixedTimeStep, 1.0e-4f);
    for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
    {
        const hairgpu::StrandRange& strand = m_asset.strands[guideIndex];
        const hairgpu::Vec3& currentRoot = currentGuideRoots[guideIndex];
        const hairgpu::Vec3& previousRoot = m_activeSetPreviousGuideRoots[guideIndex];
        const hairgpu::Vec3 restRoot = (strand.pointCount > 0u && strand.pointOffset < m_asset.restPoints.size())
            ? m_asset.restPoints[strand.pointOffset]
            : hairgpu::Vec3(0.0f, 0.0f, 0.0f);
        const float velocityProxy = hairgpu::Clamp(hairgpu::Length(currentRoot - previousRoot) / safeDt / 0.30f, 0.0f, 1.0f);
        const float displacementProxy = hairgpu::Clamp(hairgpu::Length(currentRoot - restRoot) / 0.030f, 0.0f, 1.0f);
        const float contactProxy = (guideIndex < m_activeSetGuideContactScores.size()) ? m_activeSetGuideContactScores[guideIndex] : 0.0f;
        const float editProxy = hairgpu::Clamp(guideEditScores[guideIndex], 0.0f, 1.0f);
        const float boundaryProxy = hairgpu::Clamp(4.0f * editProxy * (1.0f - editProxy), 0.0f, 1.0f);
        const float temporalProxy = hairgpu::Clamp(velocityProxy * 0.65f + contactProxy * 0.20f + boundaryProxy * 0.15f, 0.0f, 1.0f);
        const float leakageProxy = hairgpu::Clamp((1.0f - editProxy) * displacementProxy, 0.0f, 1.0f);
        float penetrationProxy = 0.0f;
        if (strand.pointCount > 0u)
        {
            const uint32_t sampleOffsets[3] = { 0u, strand.pointCount / 2u, strand.pointCount - 1u };
            for (uint32_t sampleOffset : sampleOffsets)
            {
                const uint32_t pointIndex = strand.pointOffset + std::min<uint32_t>(sampleOffset, strand.pointCount - 1u);
                if (pointIndex >= particles.size())
                {
                    continue;
                }
                const hairgpu::Vec3 p(particles[pointIndex].x, particles[pointIndex].y, particles[pointIndex].z);
                penetrationProxy = std::max(penetrationProxy, hairgpu::Clamp((m_simConfig.floorY + 0.005f - p.y) / 0.020f, 0.0f, 1.0f));
                for (const hairgpu::SphereCollider& sphere : m_colliders.spheres)
                {
                    penetrationProxy = std::max(penetrationProxy, hairgpu::Clamp(-DistanceToSphereSurface(p, sphere) / 0.020f, 0.0f, 1.0f));
                }
                for (const hairgpu::CapsuleCollider& capsule : m_colliders.capsules)
                {
                    penetrationProxy = std::max(penetrationProxy, hairgpu::Clamp(-hairgpu::QuerySignedDistanceToCapsule(capsule, p).signedDistance / 0.020f, 0.0f, 1.0f));
                }
                for (const hairgpu::PlaneCollider& plane : m_colliders.planes)
                {
                    const float signedDistance = hairgpu::Dot(p - plane.point, hairgpu::Normalize(plane.normal));
                    penetrationProxy = std::max(penetrationProxy, hairgpu::Clamp(-signedDistance / 0.020f, 0.0f, 1.0f));
                }
                for (const hairgpu::AabbCollider& aabb : m_colliders.aabbs)
                {
                    float signedDistance = 0.0f;
                    const bool inside = p.x >= aabb.minBounds.x && p.x <= aabb.maxBounds.x &&
                        p.y >= aabb.minBounds.y && p.y <= aabb.maxBounds.y &&
                        p.z >= aabb.minBounds.z && p.z <= aabb.maxBounds.z;
                    if (inside)
                    {
                        const float dx = std::min(p.x - aabb.minBounds.x, aabb.maxBounds.x - p.x);
                        const float dy = std::min(p.y - aabb.minBounds.y, aabb.maxBounds.y - p.y);
                        const float dz = std::min(p.z - aabb.minBounds.z, aabb.maxBounds.z - p.z);
                        signedDistance = -std::min(dx, std::min(dy, dz));
                    }
                    else
                    {
                        const float qx = std::max(std::max(aabb.minBounds.x - p.x, 0.0f), p.x - aabb.maxBounds.x);
                        const float qy = std::max(std::max(aabb.minBounds.y - p.y, 0.0f), p.y - aabb.maxBounds.y);
                        const float qz = std::max(std::max(aabb.minBounds.z - p.z, 0.0f), p.z - aabb.maxBounds.z);
                        signedDistance = std::sqrt(qx * qx + qy * qy + qz * qz);
                    }
                    penetrationProxy = std::max(penetrationProxy, hairgpu::Clamp(-signedDistance / 0.020f, 0.0f, 1.0f));
                }
                for (const hairgpu::SdfGridCollider& grid : m_colliders.sdfGrids)
                {
                    if (grid.values.empty() || grid.resolutionX == 0u || grid.resolutionY == 0u || grid.resolutionZ == 0u)
                    {
                        continue;
                    }
                    const hairgpu::Vec3 size = grid.boundsMax - grid.boundsMin;
                    const float nx = hairgpu::Clamp((p.x - grid.boundsMin.x) / std::max(size.x, 1.0e-5f), 0.0f, 1.0f);
                    const float ny = hairgpu::Clamp((p.y - grid.boundsMin.y) / std::max(size.y, 1.0e-5f), 0.0f, 1.0f);
                    const float nz = hairgpu::Clamp((p.z - grid.boundsMin.z) / std::max(size.z, 1.0e-5f), 0.0f, 1.0f);
                    const uint32_t ix = std::min<uint32_t>(static_cast<uint32_t>(nx * static_cast<float>(grid.resolutionX - 1u)), grid.resolutionX - 1u);
                    const uint32_t iy = std::min<uint32_t>(static_cast<uint32_t>(ny * static_cast<float>(grid.resolutionY - 1u)), grid.resolutionY - 1u);
                    const uint32_t iz = std::min<uint32_t>(static_cast<uint32_t>(nz * static_cast<float>(grid.resolutionZ - 1u)), grid.resolutionZ - 1u);
                    const size_t linearIndex = (static_cast<size_t>(iz) * grid.resolutionY + iy) * grid.resolutionX + ix;
                    if (linearIndex < grid.values.size())
                    {
                        penetrationProxy = std::max(penetrationProxy, hairgpu::Clamp(-grid.values[linearIndex] / 0.020f, 0.0f, 1.0f));
                    }
                }
            }
        }
        const float uncertaintyProxy = hairgpu::Clamp(velocityProxy * 0.34f + displacementProxy * 0.20f + contactProxy * 0.18f + boundaryProxy * 0.12f + leakageProxy * 0.10f + penetrationProxy * 0.06f, 0.0f, 1.0f);
        const float modelErrorProxy = hairgpu::Clamp(displacementProxy * 0.55f + temporalProxy * 0.25f + leakageProxy * 0.20f, 0.0f, 1.0f);
        guideBoundaryScores[guideIndex] = boundaryProxy;
        guideTemporalScores[guideIndex] = temporalProxy;
        guideLeakageScores[guideIndex] = leakageProxy;
        guidePenetrationScores[guideIndex] = penetrationProxy;
        guideModelErrorScores[guideIndex] = modelErrorProxy;
        m_activeSetGuideUncertainty[guideIndex] = uncertaintyProxy;
    }

    hairgpu::ActiveSetRuntimeInput runtimeInput = {};
    runtimeInput.currentGuideRoots = currentGuideRoots;
    runtimeInput.previousGuideRoots = m_activeSetPreviousGuideRoots;
    runtimeInput.guideUncertainty = m_activeSetGuideUncertainty;
    runtimeInput.guideContactScores = m_activeSetGuideContactScores;
    runtimeInput.guidePenetrationScores = guidePenetrationScores;
    runtimeInput.guideEditScores = guideEditScores;
    runtimeInput.guideBoundaryScores = guideBoundaryScores;
    runtimeInput.guideTemporalScores = guideTemporalScores;
    runtimeInput.guideLeakageScores = guideLeakageScores;
    runtimeInput.guideModelErrorScores = guideModelErrorScores;
    runtimeInput.cameraPosition = m_cameraPosition;
    runtimeInput.cameraForward = hairgpu::Normalize(m_cameraTarget - m_cameraPosition);
    runtimeInput.fixedTimeStep = safeDt;
    m_activeSetBudget.previousFrameMilliseconds = m_lastSimulationStepMilliseconds;

    LARGE_INTEGER begin = {};
    LARGE_INTEGER end = {};
    QueryPerformanceCounter(&begin);
    hairgpu::ActiveSetPlan plan;
    std::string error;
    const bool ok = hairgpu::EvaluateActiveSetPlan(m_asset,
        m_activeSetClusters,
        runtimeInput,
        m_activeSetScoringSettings,
        m_activeSetBudget,
        plan,
        error);
    QueryPerformanceCounter(&end);
    m_activeSetEvaluationMilliseconds = static_cast<float>(end.QuadPart - begin.QuadPart) * 1000.0f / static_cast<float>(m_qpcFrequency.QuadPart);

    if (ok)
    {
        m_activeSetPlan = std::move(plan);
        if (m_activeSetPlan.summary.recommendedSolverIterations > 0u)
        {
            m_simConfig.solverIterations = std::max<uint32_t>(1u, m_activeSetPlan.summary.recommendedSolverIterations);
        }
        std::wstringstream ss;
        ss << L"OK  clusters=" << m_activeSetPlan.summary.selectedClusterCount
            << L" guides=" << m_activeSetPlan.summary.selectedGuideCount
            << L" strands=" << m_activeSetPlan.summary.selectedStrandCount
            << L"  natGain=" << std::fixed << std::setprecision(2) << m_activeSetPlan.summary.selectedNaturalnessGain
            << L"  editCov=" << std::fixed << std::setprecision(2) << (m_activeSetPlan.summary.editCoverageRatio * 100.0f) << L"%"
            << L"  estFrame=" << std::fixed << std::setprecision(2) << m_activeSetPlan.summary.estimatedFrameMilliseconds << L"ms"
            << L"  estMem=" << std::fixed << std::setprecision(0) << m_activeSetPlan.summary.estimatedMemoryMegabytes << L"MB"
            << L"  solver=" << m_activeSetPlan.summary.recommendedSolverIterations;
        m_activeSetLastStatus = ss.str();
    }
    else
    {
        m_activeSetPlan = std::move(plan);
        m_activeSetLastStatus = Utf8ToWide(error);
    }

    m_activeSetPreviousGuideRoots = currentGuideRoots;
    SyncActiveSetGpuBuffers();
    if (IsActiveSetRiskOverlayMode())
    {
        UpdateScalpOverlayVertexBuffer();
    }
}
