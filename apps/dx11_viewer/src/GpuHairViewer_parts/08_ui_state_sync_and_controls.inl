// GpuHairViewer.cpp ordered part 08: ui_state_sync_and_controls
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

        m_gpuDebugLinesDirty = true;
        SyncControlsFromState();
        if (IsActiveSetRiskOverlayMode())
        {
            std::vector<GpuParticle> particles;
            if (ReadBackCurrentParticlePositions(particles))
            {
                UpdateActiveSetRuntimeFromParticles(particles);
            }
        }
        UpdateScalpOverlayVertexBuffer();
    }

void GpuHairViewer::ApplyPreset(int presetIndex)
    {
        m_renderMode = RenderMode::StrandRibbon;
        m_showScalpMesh = true;
        m_showGuideSkeleton = false;
        m_showFollowerSkeleton = false;
        m_showRootMarkers = false;
        m_showFollowLinks = false;
        m_showFloorGuide = false;
        m_showColliderDebug = false;
        switch (presetIndex)
        {
        default:
        case 0:
            m_activePresetName = L"Stable Benchmark Wig";
            m_builderGuideCount = kFixedGuideCount;
            m_builderFollowersPerGuide = kFixedFollowersPerGuide;
            m_builderParticlesPerStrand = kFixedParticlesPerStrand;
            m_widthScale = 0.82f;
            m_simConfig.gravity = 9.81f;
            m_simConfig.globalDamping = 0.065f;
            m_simConfig.windStrength = 0.0f;
            m_simConfig.solverIterations = kFixedSolverIterations;
            m_simConfig.followStiffness = 0.995f;
            m_selfCollisionRadius = 0.0045f;
            m_selfCollisionStiffness = 0.08f;
            m_simConfig.selfCollisionCellSize = 0.040f;
            m_simConfig.stretchCompliance = 5.0e-8f;
            m_simConfig.bendCompliance = 7.5e-6f;
            m_simConfig.looseConstraintStiffness = 0.0f;
            m_simConfig.supportCollisionRadius = 0.0f;
            m_simConfig.supportCollisionStiffness = 0.0f;
            m_simConfig.freezeBlend = 0.0f;
            break;
        case 1:
            m_activePresetName = L"Calm Studio";
            m_builderGuideCount = kFixedGuideCount;
            m_builderFollowersPerGuide = kFixedFollowersPerGuide;
            m_builderParticlesPerStrand = kFixedParticlesPerStrand;
            m_widthScale = 0.60f;
            m_simConfig.gravity = 18.0f;
            m_simConfig.globalDamping = 0.060f;
            m_simConfig.windStrength = 0.08f;
            m_simConfig.solverIterations = kFixedSolverIterations;
            m_simConfig.followStiffness = 0.985f;
            m_selfCollisionRadius = 0.012f;
            m_selfCollisionStiffness = 0.38f;
            m_simConfig.selfCollisionCellSize = 0.060f;
            m_simConfig.stretchCompliance = 1.0e-7f;
            m_simConfig.bendCompliance = 1.0e-5f;
            break;
        case 2:
            m_activePresetName = L"Windy Demo";
            m_builderGuideCount = kFixedGuideCount;
            m_builderFollowersPerGuide = kFixedFollowersPerGuide;
            m_builderParticlesPerStrand = kFixedParticlesPerStrand;
            m_widthScale = 0.58f;
            m_simConfig.gravity = 18.5f;
            m_simConfig.globalDamping = 0.040f;
            m_simConfig.windStrength = 1.30f;
            m_simConfig.solverIterations = kFixedSolverIterations;
            m_simConfig.followStiffness = 0.965f;
            m_selfCollisionRadius = 0.011f;
            m_selfCollisionStiffness = 0.32f;
            m_simConfig.selfCollisionCellSize = 0.056f;
            m_simConfig.stretchCompliance = 2.5e-7f;
            m_simConfig.bendCompliance = 1.8e-5f;
            break;
        case 3:
            m_activePresetName = L"Heavy Grounded";
            m_builderGuideCount = kFixedGuideCount;
            m_builderFollowersPerGuide = kFixedFollowersPerGuide;
            m_builderParticlesPerStrand = kFixedParticlesPerStrand;
            m_widthScale = 0.62f;
            m_simConfig.gravity = 22.0f;
            m_simConfig.globalDamping = 0.072f;
            m_simConfig.windStrength = 0.10f;
            m_simConfig.solverIterations = kFixedSolverIterations;
            m_simConfig.followStiffness = 0.990f;
            m_selfCollisionRadius = 0.015f;
            m_selfCollisionStiffness = 0.52f;
            m_simConfig.selfCollisionCellSize = 0.068f;
            m_simConfig.stretchCompliance = 8.0e-8f;
            m_simConfig.bendCompliance = 6.0e-6f;
            break;
        case 4:
            m_activePresetName = L"Game-ish LOD";
            m_builderGuideCount = kFixedGuideCount;
            m_builderFollowersPerGuide = kFixedFollowersPerGuide;
            m_builderParticlesPerStrand = kFixedParticlesPerStrand;
            m_widthScale = 0.54f;
            m_simConfig.gravity = 18.0f;
            m_simConfig.globalDamping = 0.050f;
            m_simConfig.windStrength = 0.25f;
            m_simConfig.solverIterations = kFixedSolverIterations;
            m_simConfig.followStiffness = 0.90f;
            m_selfCollisionRadius = 0.010f;
            m_selfCollisionStiffness = 0.28f;
            m_simConfig.selfCollisionCellSize = 0.064f;
            m_simConfig.stretchCompliance = 3.5e-7f;
            m_simConfig.bendCompliance = 2.5e-5f;
            break;
        }
        m_enableSelfCollision = false;
        m_enableGridSelfCollision = false;
        m_enableScalpMotion = false;
        if (presetIndex == 0)
        {
            m_workspaceMode = WorkspaceMode::Sim;
            m_interactionMode = InteractionMode::View;
            m_groomTool = GroomTool::Select;
            m_debugColorMode = DebugColorMode::Natural;
            m_maskOverlayMode = MaskOverlayMode::None;
            m_selectedStrandIndex = -1;
            m_gizmoDragging = false;
            m_brushStrokeActive = false;
            m_cpuReadbackMode = CpuReadbackMode::Off;
            m_gpuDebugLinesDirty = true;
            m_showInspectorPane = false;
            m_showBottomTray = false;
            m_fastGroomViewport = false;
            m_showScalpMesh = true;
            m_showGuideSkeleton = false;
            m_showFollowerSkeleton = false;
            m_showRootMarkers = false;
            m_showFollowLinks = false;
            m_showFloorGuide = false;
            m_showColliderDebug = false;
            m_showGridBounds = false;
            // The pre-novelty baseline must simulate all guides and rebuild
            // all followers. Active-set/tier skipping is evaluated only in its
            // designated comparison methods after correctness is established.
            m_enableActiveSetRuntime = false;
        }
        EnforceFixedWorkload();
        SyncControlsFromState();
        RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
    }

    bool GpuHairViewer::RebuildAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand)
    {
        m_builderGuideCount = guideCount;
        m_builderFollowersPerGuide = followersPerGuide;
        m_builderParticlesPerStrand = std::max<uint32_t>(particlesPerStrand, 4u);
        m_simConfig.segmentLength = SegmentLengthForParticleCount(m_builderParticlesPerStrand);

        ApplySampleMaskOverrides();
        m_asset = BuildSampleHybridHairAsset(guideCount, followersPerGuide, m_builderParticlesPerStrand, m_simConfig.segmentLength);
        const hairgpu::HairAssetValidationReport structuralReport = hairgpu::ValidateHairAsset(m_asset);
        const hairgpu::BenchmarkWigValidationReport shapeReport = hairgpu::ValidateBenchmarkWigShape(m_asset);
        if (!structuralReport.valid || !shapeReport.valid)
        {
            const std::string diagnostic =
                hairgpu::BuildHairAssetValidationSummary(structuralReport, true) + "\n" +
                hairgpu::BuildBenchmarkWigValidationSummary(shapeReport, true);
            m_lastImportStatus = L"Benchmark wig validation failed";
            const std::wstring wideDiagnostic(diagnostic.begin(), diagnostic.end());
            MessageBoxW(
                m_hwnd,
                wideDiagnostic.c_str(),
                L"Invalid Benchmark Wig",
                MB_OK | MB_ICONERROR);
            return false;
        }
        // Avoid double wig styling in viewer; sample/import paths already shape the asset.
        EnsureAssetScalpConformance(m_asset, &m_lastImportStatus);
        m_lastAssetSource = L"Sample Hybrid";
        m_lastImportStatus = L"Procedural sample asset rebuilt";
        m_baseAsset = m_asset;
        m_selectedStrandIndex = -1;
        m_strandUndoStack.clear();
        m_strandRedoStack.clear();
        m_lockedStrands.assign(m_asset.strands.size(), 0u);
        m_viewportHistory.clear();
        hairgpu::HairAsset editedAsset = m_baseAsset;
        if (m_editStackPath.empty())
        {
            m_runtimeEditState = {};
        }
        else
        {
            ApplyEditStackToAsset(editedAsset, nullptr);
        }
        m_asset = std::move(editedAsset);
        if (!CreateSimulationResources())
        {
            MessageBoxW(m_hwnd, L"Failed to rebuild simulation resources after changing the asset.", L"Rebuild Failed", MB_OK | MB_ICONERROR);
            return false;
        }
        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        AutoFrameCameraToAsset(true);
        QueueVisibilityRecovery();
        RefreshAssetStatsText();
        ResetSimulation();
        SyncControlsFromState();
        return true;
    }

    void GpuHairViewer::UpdateStatsLabel()
    {
        std::vector<float> recentFrameIntervals;
        recentFrameIntervals.reserve(m_recentFrameIntervalCount);
        for (size_t index = 0u; index < m_recentFrameIntervalCount; ++index)
        {
            const float interval = m_recentFrameIntervalsMilliseconds[index];
            if (std::isfinite(interval) && interval > 0.0f)
            {
                recentFrameIntervals.push_back(interval);
            }
        }
        if (!recentFrameIntervals.empty())
        {
            const float recentIntervalSum =
                std::accumulate(recentFrameIntervals.begin(), recentFrameIntervals.end(), 0.0f);
            m_recentFrameAverageMilliseconds =
                recentIntervalSum / static_cast<float>(recentFrameIntervals.size());
            // Effective FPS is derived from every completed frame interval in the
            // recent history. Unlike the short 0.5 s cadence window, a multi-second
            // freeze remains visible until the full rolling history has recovered.
            m_recentEffectiveFps =
                recentIntervalSum > 1.0e-6f
                ? (1000.0f * static_cast<float>(recentFrameIntervals.size()) / recentIntervalSum)
                : 0.0f;
            std::sort(recentFrameIntervals.begin(), recentFrameIntervals.end());
            const size_t slowFrameCount = std::max<size_t>(
                1u,
                static_cast<size_t>(std::ceil(
                    static_cast<double>(recentFrameIntervals.size()) * 0.01)));
            const float slowFrameSum = std::accumulate(
                recentFrameIntervals.end() - static_cast<std::ptrdiff_t>(slowFrameCount),
                recentFrameIntervals.end(),
                0.0f);
            const float slowFrameAverage =
                slowFrameSum / static_cast<float>(slowFrameCount);
            m_recentOnePercentLowFps =
                slowFrameAverage > 1.0e-6f ? (1000.0f / slowFrameAverage) : 0.0f;
            const size_t p95Index = std::min(
                recentFrameIntervals.size() - 1u,
                static_cast<size_t>(std::ceil(
                    static_cast<double>(recentFrameIntervals.size()) * 0.95)) - 1u);
            m_recentFrameP95Milliseconds = recentFrameIntervals[p95Index];
            m_recentFrameWorstMilliseconds = recentFrameIntervals.back();
            m_recentFrameStallCount100Milliseconds = static_cast<uint32_t>(
                std::count_if(
                    recentFrameIntervals.begin(),
                    recentFrameIntervals.end(),
                    [](float interval) { return interval >= 100.0f; }));
        }
        else
        {
            m_recentFrameAverageMilliseconds = 0.0f;
            m_recentEffectiveFps = 0.0f;
            m_recentOnePercentLowFps = 0.0f;
            m_recentFrameP95Milliseconds = 0.0f;
            m_recentFrameWorstMilliseconds = 0.0f;
            m_recentFrameStallCount100Milliseconds = 0u;
        }
        const float captureAverageFrameMilliseconds = !m_performanceSamples.empty()
            ? std::accumulate(m_performanceSamples.begin(), m_performanceSamples.end(), 0.0f,
                [](float acc, const PerformanceSample& sample) { return acc + sample.frameMilliseconds; }) /
                static_cast<float>(m_performanceSamples.size())
            : 0.0f;
        const float avgSimMs = !m_performanceSamples.empty()
            ? std::accumulate(m_performanceSamples.begin(), m_performanceSamples.end(), 0.0f,
                [](float acc, const PerformanceSample& s) { return acc + s.simulationMilliseconds; }) / static_cast<float>(m_performanceSamples.size())
            : 0.0f;

        const uint64_t totalParticles = static_cast<uint64_t>(m_asset.restPoints.size());
        const uint64_t guideParticles = static_cast<uint64_t>(m_asset.guideStrandCount) * static_cast<uint64_t>(m_asset.particlesPerStrand);
        const uint64_t followerParticles = totalParticles > guideParticles ? (totalParticles - guideParticles) : 0ull;

        std::wstringstream ss;
        ss << L"Runtime [HAIRGPU V9 FORCE-APPLIED]\n"
           << L"Preset: " << m_activePresetName << L"\n"
           << L"Session FPS (all completed frames): " << std::fixed << std::setprecision(1)
           << m_sessionEffectiveFps << L"\n"
           << L"Rolling FPS (last 600 frames): "
           << m_recentEffectiveFps
           << L"  1% low: " << m_recentOnePercentLowFps << L"\n"
           << L"Instant FPS (0.5 s window): "
           << (m_renderCadenceWindowValid ? m_renderFps : 0.0f)
           << L"  Simulation Hz: "
           << (m_renderCadenceWindowValid ? m_simulationHz : 0.0f) << L"\n"
           << L"Frame ms recent avg/p95/worst: " << std::setprecision(2)
           << m_recentFrameAverageMilliseconds << L" / "
           << m_recentFrameP95Milliseconds << L" / "
           << m_recentFrameWorstMilliseconds << L"\n"
           << L"Stalls >=100 ms recent/total: "
           << m_recentFrameStallCount100Milliseconds << L" / "
           << m_observedStallCount100Milliseconds
           << L"  Last: " << m_lastFrameIntervalMilliseconds << L" ms\n"
           << L"Evidence: " << GetPerformanceEvidenceClassDisplayName() << L"\n"
           << L"CPU submit + Present: " << m_lastCpuFrameWorkMilliseconds << L" ms"
           << L"  PresentSync: " << m_lastObservedPresentSyncInterval << L"\n"
           << L"GPU Frame (delayed): " << m_lastGpuFrameMilliseconds << L" ms"
           << L"  GPU Throughput: " << std::setprecision(1)
           << m_gpuThroughputFps << L" FPS\n"
           << L"Dropped simulation backlog: " << std::setprecision(2)
           << m_droppedSimulationMilliseconds << L" ms\n"
           << L"Sim Step: " << std::setprecision(2) << m_lastSimulationStepMilliseconds << L" ms"
           << L"  AvgSim: " << avgSimMs << L" ms\n"
           << L"GPU Frame: " << m_lastGpuFrameMilliseconds << L" ms"
           << L"  GPU Sim: " << m_lastGpuSimulationMilliseconds << L" ms\n"
           << L"GPU Hair: " << m_lastGpuHairDrawMilliseconds << L" ms"
           << L"  GPU Debug: " << m_lastGpuDebugDrawMilliseconds << L" ms\n"
           << L"Capture Avg Frame: " << captureAverageFrameMilliseconds << L" ms"
           << L"  Draws: " << m_lastTotalDrawCalls << L"\n"
           << L"Mode: " << (m_renderMode == RenderMode::ParticlePointSprite ? L"Particle Point Sprite" : (m_renderMode == RenderMode::ParticleBillboard ? L"Particle Billboard" : L"Strand Ribbon"))
           << L"  Quads: " << totalParticles
           << L"  HairPrims: " << m_lastCurrentHairPrimitiveCount << L"\n"
           << L"Guide particles: " << guideParticles
           << L"  Follower particles: " << followerParticles << L"\n"
           << L"Debug GPU Lines: total=" << m_debugLineStats.totalLineCount
           << L"  world=" << m_debugLineStats.worldLineCount
           << L"  hair=" << m_debugLineStats.hairLineCount
           << L"  scalp=" << m_debugLineStats.scalpLineCount
           << L"  roots=" << m_debugLineStats.rootMarkerCount << L"\n"
           << L"Hair Pipe: VS=" << m_lastHairPipelineStats.VSInvocations
           << L"  GSPrims=" << m_lastHairPipelineStats.GSPrimitives
           << L"  PS=" << m_lastHairPipelineStats.PSInvocations << L"\n"
           << L"Debug Pipe: VS=" << m_lastDebugPipelineStats.VSInvocations
           << L"  Prims=" << m_lastDebugPipelineStats.IAPrimitives
           << L"  PS=" << m_lastDebugPipelineStats.PSInvocations << L"\n"
           << L"Mode: " << (m_interactionMode == InteractionMode::Edit ? L"Edit" : L"View")
           << L"  Colors: " << (m_debugColorMode == DebugColorMode::GuideFollower ? L"Guide/Follower" : (m_debugColorMode == DebugColorMode::WidthHeatmap ? L"Width" : (m_debugColorMode == DebugColorMode::Editability ? L"Editable/Locked" : L"Natural")))
           << L"  Scalp: " << GetMaskOverlayModeDisplayName() << L"\n";
        ss << L"Active Set: " << (m_enableActiveSetRuntime ? L"On" : L"Off")
           << L"  Plan: " << m_activeSetPlan.summary.selectedClusterCount << L"/" << m_activeSetPlan.summary.clusterCount << L" clusters"
           << L"  Guides: " << m_activeSetPlan.summary.selectedGuideCount
           << L"  Eval: " << std::setprecision(2) << m_activeSetEvaluationMilliseconds << L" ms\n";
        ss << L"Active Score: hi=" << m_activeSetPlan.summary.highestScore
           << L"  avg=" << m_activeSetPlan.summary.averageSelectedScore
           << L"  status=" << m_activeSetLastStatus << L"\n";
        ss << L"Budget: scale=" << m_activeSetPlan.summary.budgetScale
           << L"  solver=" << m_activeSetPlan.summary.recommendedSolverIterations
           << L"  tiers F/R/Fz/C=" << m_activeSetPlan.summary.fullTierCount << L"/"
           << m_activeSetPlan.summary.reducedTierCount << L"/"
           << m_activeSetPlan.summary.frozenTierCount << L"/"
           << m_activeSetPlan.summary.cachedTierCount << L"\n";
        ss << L"Tune: maxC=" << m_activeSetBudget.maxActiveClusters
           << L" maxG=" << m_activeSetBudget.maxActiveGuides
           << L" thr=" << m_activeSetBudget.promoteScoreThreshold
           << L" tgt=" << m_activeSetBudget.targetFrameMilliseconds << L" ms"
           << (m_activeSetBudget.enableBudgetAdaptation ? L" adapt" : L" fixed")
           << (m_activeSetBudget.enableHardBudgetClamp ? L" clamp" : L" no-clamp") << L"\n";
        ss << L"Weights: motion=" << m_activeSetScoringSettings.motionWeight
           << L" contact=" << m_activeSetScoringSettings.contactWeight
           << L" uncert=" << m_activeSetScoringSettings.uncertaintyWeight
           << L" edit=" << m_activeSetScoringSettings.editWeight << L"\n";
        ss << L"Corrective: strands=" << m_sparseCorrectiveReport.correctedStrandCount
           << L"  points=" << m_sparseCorrectiveReport.correctedPointCount
           << L"  avgPen=" << m_sparseCorrectiveReport.averagePenetration
           << L"  maxPen=" << m_sparseCorrectiveReport.maxPenetration << L"\n";
        ss << L"GPU runtime: guide solve + follower interpolate\n";
        ss << L"Self Collision: " << (m_enableSelfCollision ? L"Density Grid Repulsion" : L"Off")
           << L"  Angular XPBD: " << (m_enableRuntimeAngularBend ? L"On" : L"Off")
           << L"  Samples: " << m_performanceSamples.size()
           << (m_paused ? L"\n[PAUSED]" : L"");
        SetWindowTextFormat(m_labelStats, ss.str());
    }
    bool GpuHairViewer::SaveSettingsToFile(const std::wstring& filePath) const
    {
        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out)
        {
            return false;
        }
        out << "settingsFormatVersion=2\n";
        out << "preset=" << WideToUtf8(m_activePresetName) << "\n";
        out << "guides=" << m_builderGuideCount << "\n";
        out << "followersPerGuide=" << m_builderFollowersPerGuide << "\n";
        out << "particlesPerStrand=" << m_builderParticlesPerStrand << "\n";
        out << "widthScale=" << m_widthScale << "\n";
        out << "gravity=" << m_simConfig.gravity << "\n";
        out << "damping=" << m_simConfig.globalDamping << "\n";
        out << "windStrength=" << m_simConfig.windStrength << "\n";
        out << "solverIterations=" << m_simConfig.solverIterations << "\n";
        out << "followStiffness=" << m_simConfig.followStiffness << "\n";
        out << "selfCollisionRadius=" << m_selfCollisionRadius << "\n";
        out << "selfCollisionStiffness=" << m_selfCollisionStiffness << "\n";
        out << "gridCellSize=" << m_simConfig.selfCollisionCellSize << "\n";
        out << "broadphaseMode=" << m_selfCollisionBroadphaseMode << "\n";
        out << "hashBucketCount=" << m_simConfig.selfCollisionHashBucketCount << "\n";
        out << "stretchCompliance=" << m_simConfig.stretchCompliance << "\n";
        out << "bendCompliance=" << m_simConfig.bendCompliance << "\n";
        out << "enableSelfCollision=" << (m_enableSelfCollision ? 1 : 0) << "\n";
        out << "enableGridSelfCollision=" << (m_enableGridSelfCollision ? 1 : 0) << "\n";
        out << "enableRuntimeAngularBend=" << (m_enableRuntimeAngularBend ? 1 : 0) << "\n";
        out << "runtimeAngularBendRadians=" << m_runtimeAngularBendRadians << "\n";
        out << "runtimeAngularBendCompliance=" << m_runtimeAngularBendCompliance << "\n";
        out << "runtimeAngularBendStiffness=" << m_runtimeAngularBendStiffness << "\n";
        out << "runtimeAngularBendDamping=" << m_runtimeAngularBendDamping << "\n";
        out << "enableScalpMotion=" << (m_enableScalpMotion ? 1 : 0) << "\n";
        out << "showColliderDebug=" << (m_showColliderDebug ? 1 : 0) << "\n";
        out << "showScalpMesh=" << (m_showScalpMesh ? 1 : 0) << "\n";
        out << "showScalpWireframe=" << (m_showScalpWireframe ? 1 : 0) << "\n";
        out << "showGridBounds=" << (m_showGridBounds ? 1 : 0) << "\n";
        out << "showFloorGuide=" << (m_showFloorGuide ? 1 : 0) << "\n";
        out << "showGuideSkeleton=" << (m_showGuideSkeleton ? 1 : 0) << "\n";
        out << "showFollowerSkeleton=" << (m_showFollowerSkeleton ? 1 : 0) << "\n";
        out << "showRootMarkers=" << (m_showRootMarkers ? 1 : 0) << "\n";
        out << "showFollowLinks=" << (m_showFollowLinks ? 1 : 0) << "\n";
        out << "renderMode=" << (m_renderMode == RenderMode::ParticlePointSprite ? 0 : (m_renderMode == RenderMode::ParticleBillboard ? 1 : 2)) << "\n";
        out << "debugColorMode=" << static_cast<uint32_t>(m_debugColorMode) << "\n";
        out << "maskSourceMode=" << static_cast<uint32_t>(m_maskSourceMode) << "\n";
        out << "maskOverlayMode=" << static_cast<uint32_t>(m_maskOverlayMode) << "\n";
        out << "scalpCoverageStrength=" << m_scalpCoverageStrength << "\n";
        out << "scalpPartLineWidth=" << m_scalpPartLineWidth << "\n";
        out << "scalpTintDarkness=" << m_scalpTintDarkness << "\n";
        out << "scalpHairBlend=" << m_scalpHairBlend << "\n";
        out << "editStackPath=" << WideToUtf8(m_editStackPath) << "\n";
        out << "partingMaskPath=" << WideToUtf8(m_partingMaskPath) << "\n";
        out << "regionMaskPath=" << WideToUtf8(m_regionMaskPath) << "\n";
        out << "inlineEditStackText=" << EscapeSettingsText(WideToUtf8(GetInlineEditStackText())) << "\n";
        out << "selectedInlineLayerIndex=" << std::max(m_selectedInlineLayerIndex, 0) << "\n";
        out << "workspaceMode=" << static_cast<uint32_t>(m_workspaceMode) << "\n";
        out << "groomTool=" << static_cast<uint32_t>(m_groomTool) << "\n";
        out << "bottomTrayTab=" << static_cast<uint32_t>(m_bottomTrayTab) << "\n";
        out << "showInspectorPane=" << (m_showInspectorPane ? 1 : 0) << "\n";
        out << "showBottomTray=" << (m_showBottomTray ? 1 : 0) << "\n";
        out << "showAdvancedInspector=" << (m_showAdvancedInspector ? 1 : 0) << "\n";
        out << "fastGroomViewport=" << (m_fastGroomViewport ? 1 : 0) << "\n";
        out << "activeSetAdaptiveBudget=" << (m_activeSetBudget.enableBudgetAdaptation ? 1 : 0) << "\n";
        out << "activeSetHardClamp=" << (m_activeSetBudget.enableHardBudgetClamp ? 1 : 0) << "\n";
        out << "activeSetMaxClusters=" << m_activeSetBudget.maxActiveClusters << "\n";
        out << "activeSetMaxGuides=" << m_activeSetBudget.maxActiveGuides << "\n";
        out << "activeSetPromoteThreshold=" << m_activeSetBudget.promoteScoreThreshold << "\n";
        out << "activeSetTargetFrameMs=" << m_activeSetBudget.targetFrameMilliseconds << "\n";
        out << "activeSetFixedCostMs=" << m_activeSetBudget.estimatedFixedCostMilliseconds << "\n";
        out << "activeSetClusterCostMs=" << m_activeSetBudget.estimatedClusterCostMilliseconds << "\n";
        out << "activeSetMotionWeight=" << m_activeSetScoringSettings.motionWeight << "\n";
        out << "activeSetContactWeight=" << m_activeSetScoringSettings.contactWeight << "\n";
        out << "activeSetUncertaintyWeight=" << m_activeSetScoringSettings.uncertaintyWeight << "\n";
        out << "activeSetEditWeight=" << m_activeSetScoringSettings.editWeight << "\n";
        return true;
    }

    bool GpuHairViewer::LoadSettingsFromFile(const std::wstring& filePath)
    {
        std::ifstream in{ std::filesystem::path(filePath) };
        if (!in)
        {
            return false;
        }

        std::unordered_map<std::string, std::string> values;
        std::string line;
        while (std::getline(in, line))
        {
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            values[line.substr(0, eq)] = line.substr(eq + 1);
        }

        auto getFloat = [&](const char* key, float current)
        {
            auto it = values.find(key);
            return (it != values.end()) ? std::stof(it->second) : current;
        };
        auto getUInt = [&](const char* key, uint32_t current)
        {
            auto it = values.find(key);
            return (it != values.end()) ? static_cast<uint32_t>(std::stoul(it->second)) : current;
        };
        auto getBool = [&](const char* key, bool current)
        {
            auto it = values.find(key);
            return (it != values.end()) ? (std::stoi(it->second) != 0) : current;
        };

        if (auto it = values.find("preset"); it != values.end())
        {
            m_activePresetName = Utf8ToWide(it->second);
        }
        m_builderGuideCount = getUInt("guides", m_builderGuideCount);
        m_builderFollowersPerGuide = getUInt("followersPerGuide", m_builderFollowersPerGuide);
        m_builderParticlesPerStrand = getUInt("particlesPerStrand", m_builderParticlesPerStrand);
        m_widthScale = getFloat("widthScale", m_widthScale);
        m_simConfig.gravity = getFloat("gravity", m_simConfig.gravity);
        m_simConfig.globalDamping = getFloat("damping", m_simConfig.globalDamping);
        m_simConfig.windStrength = getFloat("windStrength", m_simConfig.windStrength);
        m_simConfig.solverIterations = getUInt("solverIterations", m_simConfig.solverIterations);
        m_simConfig.followStiffness = getFloat("followStiffness", m_simConfig.followStiffness);
        m_selfCollisionRadius = getFloat("selfCollisionRadius", m_selfCollisionRadius);
        m_selfCollisionStiffness = getFloat("selfCollisionStiffness", m_selfCollisionStiffness);
        m_simConfig.selfCollisionCellSize = getFloat("gridCellSize", m_simConfig.selfCollisionCellSize);
        m_selfCollisionBroadphaseMode = 0u;
        m_simConfig.selfCollisionHashBucketCount = std::max<uint32_t>(getUInt("hashBucketCount", m_simConfig.selfCollisionHashBucketCount), 1u);
        m_enableRuntimeAngularBend = getBool("enableRuntimeAngularBend", m_enableRuntimeAngularBend);
        m_runtimeAngularBendRadians = getFloat("runtimeAngularBendRadians", m_runtimeAngularBendRadians);
        m_runtimeAngularBendCompliance = getFloat("runtimeAngularBendCompliance", m_runtimeAngularBendCompliance);
        m_runtimeAngularBendStiffness = getFloat("runtimeAngularBendStiffness", m_runtimeAngularBendStiffness);
        m_runtimeAngularBendDamping = getFloat("runtimeAngularBendDamping", m_runtimeAngularBendDamping);
        m_simConfig.stretchCompliance = getFloat("stretchCompliance", m_simConfig.stretchCompliance);
        m_simConfig.bendCompliance = getFloat("bendCompliance", m_simConfig.bendCompliance);
        m_enableSelfCollision = getBool("enableSelfCollision", m_enableSelfCollision);
        m_enableGridSelfCollision = true;
        m_enableScalpMotion = false;
        m_showColliderDebug = getBool("showColliderDebug", m_showColliderDebug);
        m_showScalpMesh = getBool("showScalpMesh", m_showScalpMesh);
        m_showScalpWireframe = getBool("showScalpWireframe", m_showScalpWireframe);
        m_showGridBounds = getBool("showGridBounds", m_showGridBounds);
        m_showFloorGuide = getBool("showFloorGuide", m_showFloorGuide);
        m_showGuideSkeleton = getBool("showGuideSkeleton", m_showGuideSkeleton);
        m_showFollowerSkeleton = getBool("showFollowerSkeleton", m_showFollowerSkeleton);
        m_showRootMarkers = getBool("showRootMarkers", m_showRootMarkers);
        m_showFollowLinks = getBool("showFollowLinks", m_showFollowLinks);
        {
            const int renderModeValue = static_cast<int>(getUInt(
                "renderMode",
                static_cast<unsigned>(m_renderMode == RenderMode::ParticlePointSprite
                    ? 0
                    : (m_renderMode == RenderMode::ParticleBillboard ? 1 : 2))));
            const bool allowPersistedParticleDebug = EnvironmentValueIsTrue(
                GetEnvironmentVariableString(L"HAIRGPU_ALLOW_PARTICLE_DEBUG_RENDERING"));
            if (IsExperimentModeEnabled() || !allowPersistedParticleDebug || renderModeValue < 0 || renderModeValue > 2)
            {
                // Old settings commonly persisted Point/Billboard mode and made the next
                // startup look like a sparse gold point cloud. Stable/runtime evidence
                // always starts in the production ribbon path.
                m_renderMode = RenderMode::StrandRibbon;
            }
            else if (renderModeValue == 0)
            {
                m_renderMode = RenderMode::ParticlePointSprite;
            }
            else if (renderModeValue == 1)
            {
                m_renderMode = RenderMode::ParticleBillboard;
            }
            else
            {
                m_renderMode = RenderMode::StrandRibbon;
            }
        }
        m_interactionMode = static_cast<InteractionMode>(std::min<uint32_t>(getUInt("interactionMode", static_cast<uint32_t>(m_interactionMode)), 1u));
        const uint32_t settingsFormatVersion = getUInt("settingsFormatVersion", 1u);
        const uint32_t persistedDebugColorMode = std::min<uint32_t>(
            getUInt("debugColorMode", static_cast<uint32_t>(m_debugColorMode)),
            3u);
        // Version 1 could silently force Editability when entering Groom mode.
        // Migrate that stale automatic value back to Natural while preserving an
        // explicit selection written by the corrected version 2 format.
        m_debugColorMode =
            (settingsFormatVersion < 2u && persistedDebugColorMode == static_cast<uint32_t>(DebugColorMode::Editability))
            ? DebugColorMode::Natural
            : static_cast<DebugColorMode>(persistedDebugColorMode);
        if (auto it = values.find("editStackPath"); it != values.end()) m_editStackPath = Utf8ToWide(it->second);
        if (auto it = values.find("partingMaskPath"); it != values.end()) m_partingMaskPath = Utf8ToWide(it->second);
        if (auto it = values.find("regionMaskPath"); it != values.end()) m_regionMaskPath = Utf8ToWide(it->second);
        std::wstring inlineEditStackText = BuildDefaultInlineEditStackText();
        if (auto it = values.find("inlineEditStackText"); it != values.end()) inlineEditStackText = Utf8ToWide(UnescapeSettingsText(it->second));
        if (auto it = values.find("selectedInlineLayerIndex"); it != values.end()) m_selectedInlineLayerIndex = std::max(0, std::stoi(it->second));
        m_workspaceMode = static_cast<WorkspaceMode>(std::min<uint32_t>(getUInt("workspaceMode", static_cast<uint32_t>(m_workspaceMode)), 3u));
        m_groomTool = static_cast<GroomTool>(std::min<uint32_t>(getUInt("groomTool", static_cast<uint32_t>(m_groomTool)), 8u));
        m_bottomTrayTab = static_cast<BottomTrayTab>(std::min<uint32_t>(getUInt("bottomTrayTab", static_cast<uint32_t>(m_bottomTrayTab)), 2u));
        m_showInspectorPane = getBool("showInspectorPane", m_showInspectorPane);
        m_showBottomTray = getBool("showBottomTray", m_showBottomTray);
        m_showAdvancedInspector = getBool("showAdvancedInspector", m_showAdvancedInspector);
        m_fastGroomViewport = getBool("fastGroomViewport", m_fastGroomViewport);
        m_maskSourceMode = static_cast<MaskSourceMode>(std::min<uint32_t>(getUInt("maskSourceMode", static_cast<uint32_t>(m_maskSourceMode)), 2u));
        m_maskOverlayMode = static_cast<MaskOverlayMode>(std::min<uint32_t>(getUInt("maskOverlayMode", static_cast<uint32_t>(m_maskOverlayMode)), 9u));
        m_scalpCoverageStrength = hairgpu::Clamp(getFloat("scalpCoverageStrength", m_scalpCoverageStrength), 0.0f, 2.0f);
        m_scalpPartLineWidth = hairgpu::Clamp(getFloat("scalpPartLineWidth", m_scalpPartLineWidth), 0.008f, 0.120f);
        m_scalpTintDarkness = hairgpu::Clamp(getFloat("scalpTintDarkness", m_scalpTintDarkness), 0.0f, 1.0f);
        m_scalpHairBlend = hairgpu::Clamp(getFloat("scalpHairBlend", m_scalpHairBlend), 0.0f, 1.0f);
        m_activeSetBudget.enableBudgetAdaptation = getBool("activeSetAdaptiveBudget", m_activeSetBudget.enableBudgetAdaptation);
        m_activeSetBudget.enableHardBudgetClamp = getBool("activeSetHardClamp", m_activeSetBudget.enableHardBudgetClamp);
        m_activeSetBudget.maxActiveClusters = std::max<uint32_t>(1u, getUInt("activeSetMaxClusters", m_activeSetBudget.maxActiveClusters));
        m_activeSetBudget.maxActiveGuides = std::max<uint32_t>(m_activeSetBudget.maxActiveClusters, getUInt("activeSetMaxGuides", m_activeSetBudget.maxActiveGuides));
        m_activeSetBudget.promoteScoreThreshold = hairgpu::Clamp(getFloat("activeSetPromoteThreshold", m_activeSetBudget.promoteScoreThreshold), 0.0f, 1.0f);
        m_activeSetBudget.targetFrameMilliseconds = hairgpu::Clamp(getFloat("activeSetTargetFrameMs", m_activeSetBudget.targetFrameMilliseconds), 4.0f, 16.67f);
        m_activeSetBudget.estimatedFixedCostMilliseconds = hairgpu::Clamp(getFloat("activeSetFixedCostMs", m_activeSetBudget.estimatedFixedCostMilliseconds), 0.0f, 16.0f);
        m_activeSetBudget.estimatedClusterCostMilliseconds = hairgpu::Clamp(getFloat("activeSetClusterCostMs", m_activeSetBudget.estimatedClusterCostMilliseconds), 0.05f, 4.0f);
        m_activeSetBudget.frameBudgetMilliseconds = 16.67f;
        m_activeSetScoringSettings.motionWeight = hairgpu::Clamp(getFloat("activeSetMotionWeight", m_activeSetScoringSettings.motionWeight), 0.0f, 1.0f);
        m_activeSetScoringSettings.contactWeight = hairgpu::Clamp(getFloat("activeSetContactWeight", m_activeSetScoringSettings.contactWeight), 0.0f, 1.0f);
        m_activeSetScoringSettings.uncertaintyWeight = hairgpu::Clamp(getFloat("activeSetUncertaintyWeight", m_activeSetScoringSettings.uncertaintyWeight), 0.0f, 1.0f);
        m_activeSetScoringSettings.editWeight = hairgpu::Clamp(getFloat("activeSetEditWeight", m_activeSetScoringSettings.editWeight), 0.0f, 1.0f);
        m_interactionMode = (m_workspaceMode == WorkspaceMode::Groom) ? InteractionMode::Edit : InteractionMode::View;

        SyncControlsFromState();
        SetWindowTextW(m_editInlineEditStack, inlineEditStackText.c_str());
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
        if (!inlineEditStackText.empty())
        {
            const std::filesystem::path tempInlinePath = GetInlineEditStackTempPath();
            WriteUtf8TextFile(tempInlinePath, WideToUtf8(inlineEditStackText));
            if (m_editStackPath.empty() || !std::filesystem::exists(std::filesystem::path(m_editStackPath)))
            {
                m_editStackPath = tempInlinePath.wstring();
            }
        }
        RefreshMaskPreviewControls();
        return RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
    }

    bool GpuHairViewer::BrowseAndSetEditStackPath()
    {
        wchar_t fileName[MAX_PATH] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"Edit Stack CSV (*.csv)\0*.csv\0All Files\0*.*\0";
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&ofn))
        {
            return false;
        }
        m_editStackPath = fileName;
        try
        {
            std::ifstream in(std::filesystem::path(fileName), std::ios::binary);
            std::string inlineText((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!inlineText.empty())
            {
                SetWindowTextW(m_editInlineEditStack, Utf8ToWide(inlineText).c_str());
                LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
            }
        }
        catch (...)
        {
        }
        return ApplyEditStackToCurrentAsset(true);
    }

    bool GpuHairViewer::ClearEditStackAndRestoreAsset()
    {
        m_editStackPath.clear();
        m_lastEditStackStatus = L"No edit stack";
        m_runtimeEditState = {};
        if (m_baseAsset.strands.empty())
        {
            RefreshAssetStatsText();
            SyncControlsFromState();
            return true;
        }
        m_asset = m_baseAsset;
        if (!CreateSimulationResources())
        {
            m_lastEditStackStatus = L"Restore failed";
            RefreshAssetStatsText();
            return false;
        }
        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        QueueVisibilityRecovery();
        RefreshAssetStatsText();
        ResetSimulation();
        SyncControlsFromState();
        UpdateScalpOverlayVertexBuffer();
        return true;
    }

    bool GpuHairViewer::ApplyEditStackToAsset(hairgpu::HairAsset& asset, std::wstring* outStatusText)
    {
        auto setStatus = [&](const std::wstring& status)
        {
            m_lastEditStackStatus = status;
            if (outStatusText)
            {
                *outStatusText = status;
            }
        };

        m_runtimeEditState = {};
        m_runtimeEditState.guideInfluence.assign(std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size())), 0.0f);
        m_runtimeEditState.strandInfluence.assign(asset.strands.size(), 0.0f);

        if (m_editStackPath.empty())
        {
            setStatus(L"No edit stack");
            return true;
        }

        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsv(std::filesystem::path(m_editStackPath), stack, error))
        {
            setStatus(L"Load failed");
            return false;
        }

        hairgpu::HairAsset edited = asset;
        hairgpu::RuntimeEditState state = {};
        if (!hairgpu::ApplyEditStackToAsset(edited, asset, stack, &state, error))
        {
            setStatus(L"Apply failed");
            return false;
        }

        asset = std::move(edited);
        m_runtimeEditState = std::move(state);
        std::wstringstream ss;
        ss << L"Applied " << stack.layers.size() << L" layers";
        setStatus(ss.str());
        return true;
    }

    bool GpuHairViewer::ApplyEditStackToCurrentAsset(bool showMessageOnFailure)
    {
        if (m_baseAsset.strands.empty())
        {
            m_baseAsset = m_asset;
        }
        hairgpu::HairAsset edited = m_baseAsset;
        std::wstring status;
        if (!ApplyEditStackToAsset(edited, &status))
        {
            RefreshAssetStatsText();
            UpdateScalpOverlayVertexBuffer();
            if (showMessageOnFailure)
            {
                const std::wstring message = L"Edit stack apply failed.\r\n\r\n" + status + L"\r\nPath: " + m_editStackPath;
                MessageBoxW(m_hwnd, message.c_str(), L"Edit Stack", MB_OK | MB_ICONERROR);
            }
            return false;
        }
        m_asset = std::move(edited);
        if (!CreateSimulationResources())
        {
            if (showMessageOnFailure)
            {
                MessageBoxW(m_hwnd, L"Edit stack was applied, but GPU resources failed to rebuild.", L"Edit Stack", MB_OK | MB_ICONERROR);
            }
            return false;
        }
        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        QueueVisibilityRecovery();
        RefreshAssetStatsText();
        ResetSimulation();
        SyncControlsFromState();
        UpdateScalpOverlayVertexBuffer();
        return true;
    }

    std::wstring GpuHairViewer::GetInlineEditStackText() const
    {
        return GetWindowTextString(m_editInlineEditStack);
    }

    std::filesystem::path GpuHairViewer::GetInlineEditStackTempPath() const
    {
        try
        {
            return std::filesystem::temp_directory_path() / "hairgpu_viewer_inline_edit_stack.csv";
        }
        catch (...)
        {
            return std::filesystem::path("hairgpu_viewer_inline_edit_stack.csv");
        }
    }

    void GpuHairViewer::ResetInlineEditStackText()
    {
        SetWindowTextW(m_editInlineEditStack, BuildDefaultInlineEditStackText().c_str());
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
    }

    bool GpuHairViewer::SaveInlineEditStackToPath(const std::wstring& filePath, bool applyAfterSave)
    {
        const std::wstring inlineText = GetInlineEditStackText();
        if (!WriteUtf8TextFile(std::filesystem::path(filePath), WideToUtf8(inlineText)))
        {
            return false;
        }

        m_editStackPath = filePath;
        if (applyAfterSave)
        {
            return ApplyEditStackToCurrentAsset(true);
        }

        SyncControlsFromState();
        return true;
    }

    bool GpuHairViewer::ApplyInlineEditStackFromUi(bool showMessageOnFailure)
    {
        const std::wstring inlineText = GetInlineEditStackText();
        const std::filesystem::path tempPath = GetInlineEditStackTempPath();
        if (!WriteUtf8TextFile(tempPath, WideToUtf8(inlineText)))
        {
            if (showMessageOnFailure)
            {
                MessageBoxW(m_hwnd, L"Failed to stage the inline edit stack text.", L"Inline Edit Stack", MB_OK | MB_ICONERROR);
            }
            return false;
        }

        m_editStackPath = tempPath.wstring();
        return ApplyEditStackToCurrentAsset(showMessageOnFailure);
    }

    void GpuHairViewer::RefreshActiveSetTuningLabels()
    {
        if (!m_labelActiveSetMaxClusters)
        {
            return;
        }
        SetWindowTextFormat(m_labelActiveSetMaxClusters, L"Max Active Clusters: " + std::to_wstring(m_activeSetBudget.maxActiveClusters));
        SetWindowTextFormat(m_labelActiveSetMaxGuides, L"Max Active Guides: " + std::to_wstring(m_activeSetBudget.maxActiveGuides));
        SetWindowTextFormat(m_labelActiveSetThreshold, L"Promote Threshold: " + FormatFloat(m_activeSetBudget.promoteScoreThreshold, 2));
        SetWindowTextFormat(m_labelActiveSetTargetFrame, L"Target Frame: " + FormatFloat(m_activeSetBudget.targetFrameMilliseconds, 2) + L" ms");
        SetWindowTextFormat(m_labelActiveSetFixedCost, L"Estimated Fixed Cost: " + FormatFloat(m_activeSetBudget.estimatedFixedCostMilliseconds, 2) + L" ms");
        SetWindowTextFormat(m_labelActiveSetClusterCost, L"Estimated Cluster Cost: " + FormatFloat(m_activeSetBudget.estimatedClusterCostMilliseconds, 2) + L" ms");
        SetWindowTextFormat(m_labelActiveSetMotionWeight, L"Motion Weight: " + FormatFloat(m_activeSetScoringSettings.motionWeight, 2));
        SetWindowTextFormat(m_labelActiveSetContactWeight, L"Contact Weight: " + FormatFloat(m_activeSetScoringSettings.contactWeight, 2));
        SetWindowTextFormat(m_labelActiveSetUncertaintyWeight, L"Uncertainty Weight: " + FormatFloat(m_activeSetScoringSettings.uncertaintyWeight, 2));
        SetWindowTextFormat(m_labelActiveSetEditWeight, L"Edit Weight: " + FormatFloat(m_activeSetScoringSettings.editWeight, 2));
        std::wstring tuningHeader = L"Active-Set Runtime Tuning";
        tuningHeader += m_activeSetBudget.enableBudgetAdaptation ? L" [adaptive]" : L" [fixed]";
        tuningHeader += m_activeSetBudget.enableHardBudgetClamp ? L" [clamp]" : L" [open]";
        SetWindowTextFormat(m_labelActiveSetTuning, tuningHeader);
    }


    void GpuHairViewer::RefreshQuickEditFormLabels()
    {
        std::wstring selectedLabel = std::wstring(L"Layers - selected: ");
        const int selectedIndex = GetSelectedInlineLayerIndex();
        if (selectedIndex >= 0)
        {
            selectedLabel += std::to_wstring(selectedIndex + 1);
            selectedLabel += m_quickEditEnabled ? L" [enabled]" : L" [disabled]";
        }
        else
        {
            selectedLabel += L"none";
        }
        SetWindowTextFormat(m_labelLayerList, selectedLabel);

        std::wstring toolLabel = std::wstring(L"Groom Form - ") + GetGroomToolDisplayName();
        SetWindowTextFormat(m_labelQuickEditForm, toolLabel);
        SetWindowTextFormat(m_labelQuickEditName, L"Layer / Stroke Name");

