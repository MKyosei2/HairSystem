// GpuHairViewer.cpp ordered part 09: settings_load_save_and_export
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.
// P0_FIXED_WORKLOAD: persisted workload values are overridden during initialization and UI synchronization.

        constexpr float kRadToDeg = 57.2957795f;
        switch (m_groomTool)
        {
        case GroomTool::Comb:
            SetWindowTextFormat(m_labelQuickEditCenterU, L"Brush Center U: " + FormatFloat(m_quickEditCenterU, 2));
            SetWindowTextFormat(m_labelQuickEditCenterV, L"Brush Center V: " + FormatFloat(m_quickEditCenterV, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusU, L"Brush Width: " + FormatFloat(m_quickEditRadiusU, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusV, L"Brush Height: " + FormatFloat(m_quickEditRadiusV, 2));
            SetWindowTextFormat(m_labelQuickEditLengthScale, L"Length Bias: " + FormatFloat(m_quickEditLengthScale, 2));
            SetWindowTextFormat(m_labelQuickEditLayerWeight, L"Stroke Strength: " + FormatFloat(m_quickEditLayerWeight, 2));
            SetWindowTextFormat(m_labelQuickEditFeather, L"Falloff: " + FormatFloat(m_quickEditFeather, 2));
            SetWindowTextFormat(m_labelQuickEditTipOffsetX, L"Comb Drift X: " + FormatFloat(m_quickEditTipOffsetX, 3));
            SetWindowTextFormat(m_labelQuickEditTipOffsetZ, L"Comb Drift Z: " + FormatFloat(m_quickEditTipOffsetZ, 3));
            SetWindowTextFormat(m_labelQuickEditSideOffset, L"Side Flow: " + FormatFloat(m_quickEditSideOffset, 3));
            SetWindowTextFormat(m_labelQuickEditYaw, L"Flow Angle: " + FormatFloat(m_quickEditYawRadians * kRadToDeg, 1) + L" deg");
            SetWindowTextFormat(m_labelQuickEditFollowerInfluence, L"Clump Follow: " + FormatFloat(m_quickEditFollowerInfluence, 2));
            SetWindowTextFormat(m_labelQuickEditBoundarySmoothing, L"Relax After Stroke: " + FormatFloat(m_quickEditBoundarySmoothing, 2));
            break;
        case GroomTool::Grab:
        case GroomTool::StrandFix:
            SetWindowTextFormat(m_labelQuickEditCenterU, L"Grab Center U: " + FormatFloat(m_quickEditCenterU, 2));
            SetWindowTextFormat(m_labelQuickEditCenterV, L"Grab Center V: " + FormatFloat(m_quickEditCenterV, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusU, L"Grab Radius U: " + FormatFloat(m_quickEditRadiusU, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusV, L"Grab Radius V: " + FormatFloat(m_quickEditRadiusV, 2));
            SetWindowTextFormat(m_labelQuickEditLayerWeight, L"Grab Strength: " + FormatFloat(m_quickEditLayerWeight, 2));
            SetWindowTextFormat(m_labelQuickEditFeather, L"Falloff: " + FormatFloat(m_quickEditFeather, 2));
            SetWindowTextFormat(m_labelQuickEditRootOffsetX, L"Root Move X: " + FormatFloat(m_quickEditRootOffsetX, 3));
            SetWindowTextFormat(m_labelQuickEditRootOffsetY, L"Root Move Y: " + FormatFloat(m_quickEditRootOffsetY, 3));
            SetWindowTextFormat(m_labelQuickEditRootOffsetZ, L"Root Move Z: " + FormatFloat(m_quickEditRootOffsetZ, 3));
            SetWindowTextFormat(m_labelQuickEditTipOffsetX, L"Tip Move X: " + FormatFloat(m_quickEditTipOffsetX, 3));
            SetWindowTextFormat(m_labelQuickEditTipOffsetY, L"Tip Move Y: " + FormatFloat(m_quickEditTipOffsetY, 3));
            SetWindowTextFormat(m_labelQuickEditTipOffsetZ, L"Tip Move Z: " + FormatFloat(m_quickEditTipOffsetZ, 3));
            break;
        case GroomTool::Smooth:
            SetWindowTextFormat(m_labelQuickEditLayerWeight, L"Smooth Strength: " + FormatFloat(m_quickEditLayerWeight, 2));
            SetWindowTextFormat(m_labelQuickEditFeather, L"Smooth Falloff: " + FormatFloat(m_quickEditFeather, 2));
            SetWindowTextFormat(m_labelQuickEditBoundarySmoothing, L"Boundary Relax: " + FormatFloat(m_quickEditBoundarySmoothing, 2));
            SetWindowTextFormat(m_labelQuickEditBoundaryRadiusUv, L"Relax Radius: " + FormatFloat(m_quickEditBoundaryRadiusUv, 2));
            break;
        case GroomTool::Length:
            SetWindowTextFormat(m_labelQuickEditCenterU, L"Target Area U: " + FormatFloat(m_quickEditCenterU, 2));
            SetWindowTextFormat(m_labelQuickEditCenterV, L"Target Area V: " + FormatFloat(m_quickEditCenterV, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusU, L"Area Width: " + FormatFloat(m_quickEditRadiusU, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusV, L"Area Height: " + FormatFloat(m_quickEditRadiusV, 2));
            SetWindowTextFormat(m_labelQuickEditLengthScale, L"Length Scale: " + FormatFloat(m_quickEditLengthScale, 2));
            SetWindowTextFormat(m_labelQuickEditLayerWeight, L"Length Weight: " + FormatFloat(m_quickEditLayerWeight, 2));
            SetWindowTextFormat(m_labelQuickEditFeather, L"Blend: " + FormatFloat(m_quickEditFeather, 2));
            break;
        case GroomTool::Volume:
            SetWindowTextFormat(m_labelQuickEditCenterU, L"Volume Center U: " + FormatFloat(m_quickEditCenterU, 2));
            SetWindowTextFormat(m_labelQuickEditCenterV, L"Volume Center V: " + FormatFloat(m_quickEditCenterV, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusU, L"Volume Width: " + FormatFloat(m_quickEditRadiusU, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusV, L"Volume Height: " + FormatFloat(m_quickEditRadiusV, 2));
            SetWindowTextFormat(m_labelQuickEditLayerWeight, L"Volume Strength: " + FormatFloat(m_quickEditLayerWeight, 2));
            SetWindowTextFormat(m_labelQuickEditSideOffset, L"Lift / Push: " + FormatFloat(m_quickEditSideOffset, 3));
            SetWindowTextFormat(m_labelQuickEditWidthScale, L"Width Scale: " + FormatFloat(m_quickEditWidthScale, 2));
            SetWindowTextFormat(m_labelQuickEditFollowerInfluence, L"Clump Response: " + FormatFloat(m_quickEditFollowerInfluence, 2));
            SetWindowTextFormat(m_labelQuickEditBoundarySmoothing, L"Relax After Volume: " + FormatFloat(m_quickEditBoundarySmoothing, 2));
            break;
        case GroomTool::Part:
            SetWindowTextFormat(m_labelQuickEditCenterU, L"Part Center U: " + FormatFloat(m_quickEditCenterU, 2));
            SetWindowTextFormat(m_labelQuickEditCenterV, L"Part Center V: " + FormatFloat(m_quickEditCenterV, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusU, L"Part Width: " + FormatFloat(m_quickEditRadiusU, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusV, L"Part Height: " + FormatFloat(m_quickEditRadiusV, 2));
            SetWindowTextFormat(m_labelQuickEditAngleCenter, L"Direction: " + FormatFloat(m_quickEditAngleCenterRadians * kRadToDeg, 1) + L" deg");
            SetWindowTextFormat(m_labelQuickEditAngleHalfExtent, L"Spread: " + FormatFloat(m_quickEditAngleHalfExtentRadians * kRadToDeg, 1) + L" deg");
            SetWindowTextFormat(m_labelQuickEditLayerWeight, L"Part Strength: " + FormatFloat(m_quickEditLayerWeight, 2));
            SetWindowTextFormat(m_labelQuickEditYaw, L"Bias Angle: " + FormatFloat(m_quickEditYawRadians * kRadToDeg, 1) + L" deg");
            break;
        case GroomTool::Freeze:
        case GroomTool::Select:
        default:
            SetWindowTextFormat(m_labelQuickEditCenterU, L"Selection Center U: " + FormatFloat(m_quickEditCenterU, 2));
            SetWindowTextFormat(m_labelQuickEditCenterV, L"Selection Center V: " + FormatFloat(m_quickEditCenterV, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusU, L"Selection Width: " + FormatFloat(m_quickEditRadiusU, 2));
            SetWindowTextFormat(m_labelQuickEditRadiusV, L"Selection Height: " + FormatFloat(m_quickEditRadiusV, 2));
            SetWindowTextFormat(m_labelQuickEditLayerWeight, L"Selection Weight: " + FormatFloat(m_quickEditLayerWeight, 2));
            SetWindowTextFormat(m_labelQuickEditFeather, L"Selection Feather: " + FormatFloat(m_quickEditFeather, 2));
            SetWindowTextFormat(m_labelQuickEditGroupId, L"Group Filter: " + FormatOptionalGroupId(m_quickEditGroupId));
            break;
        }

        // labels that may be shown outside the current tool still need sensible captions
        SetWindowTextFormat(m_labelQuickEditInnerRadius, L"Inner Radius: " + FormatFloat(m_quickEditInnerRadius, 2));
        SetWindowTextFormat(m_labelQuickEditWidthScale, L"Width Scale: " + FormatFloat(m_quickEditWidthScale, 2));
        SetWindowTextFormat(m_labelQuickEditFollowerInfluence, L"Follower Influence: " + FormatFloat(m_quickEditFollowerInfluence, 2));
        SetWindowTextFormat(m_labelQuickEditBoundarySmoothing, L"Boundary Blend: " + FormatFloat(m_quickEditBoundarySmoothing, 2));
        SetWindowTextFormat(m_labelQuickEditBoundaryRadiusUv, L"Boundary Radius: " + FormatFloat(m_quickEditBoundaryRadiusUv, 2));
        SetWindowTextFormat(m_labelQuickEditGroupId, L"Group Filter: " + FormatOptionalGroupId(m_quickEditGroupId));

        std::wstring previewLabel = std::wstring(L"Workspace: ") + this->GetWorkspaceDisplayName() +
            L"  Tool: " + GetGroomToolDisplayName() +
            L"\r\nTip: sculpt in the viewport, then push to layer or duplicate a layer for non-destructive refinement.";
        std::vector<float> previewInfluence;
        uint32_t previewGuideCount = 0u;
        float previewMaxInfluence = 0.0f;
        if (TryBuildSelectedInlineLayerPreview(previewInfluence, previewGuideCount, previewMaxInfluence))
        {
            previewLabel += L"\r\nSelected guides: " + std::to_wstring(previewGuideCount) +
                L"  Max influence: " + FormatFloat(previewMaxInfluence, 2) +
                L"  Overlay: " + GetMaskOverlayModeDisplayName();
        }
        uint32_t lockedCount = 0u;
        for (uint8_t v : m_lockedStrands)
        {
            lockedCount += (v != 0u) ? 1u : 0u;
        }
        previewLabel += L"\r\nLocked strands: " + std::to_wstring(lockedCount);
        if (m_brushStrokeActive)
        {
            previewLabel += L"  Active brush guides: " + std::to_wstring(m_brushStrokeAffectedGuideCount);
        }
        SetWindowTextFormat(m_labelQuickEditVisualization, previewLabel);

        SetWindowTextFormat(m_btnQuickEditReplace, L"Update Layer");
        SetWindowTextFormat(m_btnQuickEditAppend, L"New Layer");
        SetWindowTextFormat(m_btnQuickEditDuplicate, L"Duplicate");
        SetWindowTextFormat(m_btnQuickEditDelete, L"Delete");
        SetWindowTextFormat(m_btnQuickEditMoveUp, L"Move Up");
        SetWindowTextFormat(m_btnQuickEditMoveDown, L"Move Down");

        SetWindowTextFormat(m_labelEditLegend, BuildHistorySummaryText());
    }


    int GpuHairViewer::ExportCanonicalFrozenAssetSource(
        const std::filesystem::path& outputDirectory)
    {
        struct FrozenTopologyHeader
        {
            char magic[8] = { 'H', 'G', 'T', 'O', 'P', '0', '1', '\0' };
            uint32_t version = 1u;
            uint32_t guideStrandCount = 0u;
            uint32_t followerStrandCount = 0u;
            uint32_t particlesPerStrand = 0u;
            uint32_t groupsPerBlock = 8u;
            uint32_t strandRangeCount = 0u;
            uint32_t blockCount = 0u;
            uint64_t totalGroupCount = 0ull;
        };

        try
        {
            if (outputDirectory.empty())
            {
                return 64;
            }

            std::error_code ec;
            std::filesystem::create_directories(outputDirectory, ec);
            if (ec)
            {
                return 65;
            }

            const std::filesystem::path assetPath = outputDirectory / L"novelty01_100k.hairbin";
            const std::filesystem::path topologyPath = outputDirectory / L"novelty01_100k_topology.bin";
            const std::filesystem::path settingsPath = outputDirectory / L"generator_settings.json";
            std::filesystem::path temporaryAssetPath = assetPath;
            std::filesystem::path temporaryTopologyPath = topologyPath;
            std::filesystem::path temporarySettingsPath = settingsPath;
            temporaryAssetPath += L".tmp";
            temporaryTopologyPath += L".tmp";
            temporarySettingsPath += L".tmp";

            std::filesystem::remove(temporaryAssetPath, ec);
            ec.clear();
            std::filesystem::remove(temporaryTopologyPath, ec);
            ec.clear();
            std::filesystem::remove(temporarySettingsPath, ec);
            ec.clear();

            const float segmentLength = SegmentLengthForParticleCount(kFixedParticlesPerStrand);
            hairgpu::HairAsset asset = BuildSampleHybridHairAsset(
                kFixedGuideCount,
                kFixedFollowersPerGuide,
                kFixedParticlesPerStrand,
                segmentLength);
            EnsureAssetScalpConformance(asset, nullptr);

            const uint64_t expectedStrandCount =
                static_cast<uint64_t>(kFixedGuideCount) *
                static_cast<uint64_t>(kFixedFollowersPerGuide + 1u);
            const uint64_t expectedPointCount =
                expectedStrandCount * static_cast<uint64_t>(kFixedParticlesPerStrand);
            const bool shapeValid =
                expectedStrandCount == static_cast<uint64_t>(kFixedDisplayStrandCount) &&
                asset.guideStrandCount == kFixedGuideCount &&
                asset.followerStrandCount == kFixedGuideCount * kFixedFollowersPerGuide &&
                asset.particlesPerStrand == kFixedParticlesPerStrand &&
                asset.strands.size() == expectedStrandCount &&
                asset.restPoints.size() == expectedPointCount &&
                asset.pointWidths.size() == expectedPointCount &&
                asset.rootBindings.size() == expectedStrandCount &&
                asset.followData.size() == expectedStrandCount;
            if (!shapeValid)
            {
                return 66;
            }

            for (const hairgpu::Vec3& point : asset.restPoints)
            {
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                {
                    return 67;
                }
            }

            std::string assetError;
            if (!hairgpu::SaveHairAssetToHairBinary(temporaryAssetPath, asset, assetError))
            {
                return 68;
            }

            constexpr uint32_t groupsPerBlock = 8u;
            const uint32_t groupsPerStrand = kFixedParticlesPerStrand - 1u;
            const uint32_t blocksPerStrand =
                (groupsPerStrand + groupsPerBlock - 1u) / groupsPerBlock;
            FrozenTopologyHeader header = {};
            header.guideStrandCount = kFixedGuideCount;
            header.followerStrandCount = kFixedGuideCount * kFixedFollowersPerGuide;
            header.particlesPerStrand = kFixedParticlesPerStrand;
            header.groupsPerBlock = groupsPerBlock;
            header.strandRangeCount = kFixedGuideCount;
            header.blockCount = kFixedGuideCount * blocksPerStrand;
            header.totalGroupCount =
                static_cast<uint64_t>(kFixedGuideCount) * groupsPerStrand;

            {
                std::ofstream topology(temporaryTopologyPath, std::ios::binary | std::ios::trunc);
                if (!topology)
                {
                    return 69;
                }
                topology.write(reinterpret_cast<const char*>(&header), sizeof(header));
                for (uint32_t strandIndex = 0u; strandIndex < kFixedGuideCount; ++strandIndex)
                {
                    hairgpu::SegmentGroupRange range = {};
                    range.firstGroup = strandIndex * groupsPerStrand;
                    range.groupCount = groupsPerStrand;
                    topology.write(reinterpret_cast<const char*>(&range), sizeof(range));
                }
                for (uint32_t strandIndex = 0u; strandIndex < kFixedGuideCount; ++strandIndex)
                {
                    for (uint32_t blockIndex = 0u; blockIndex < blocksPerStrand; ++blockIndex)
                    {
                        const uint32_t localFirst = blockIndex * groupsPerBlock;
                        hairgpu::SegmentGroupBlockRange block = {};
                        block.firstGroup = strandIndex * groupsPerStrand + localFirst;
                        block.groupCount = std::min(groupsPerBlock, groupsPerStrand - localFirst);
                        block.strandIndex = strandIndex;
                        block.blockIndex = blockIndex;
                        topology.write(reinterpret_cast<const char*>(&block), sizeof(block));
                    }
                }
                topology.flush();
                if (!topology)
                {
                    return 70;
                }
            }

            {
                std::ofstream settings(temporarySettingsPath, std::ios::binary | std::ios::trunc);
                if (!settings)
                {
                    return 71;
                }
                settings
                    << "{\n"
                    << "  \"schemaVersion\": \"novelty01-generator-settings-v1\",\n"
                    << "  \"guideStrandCount\": " << kFixedGuideCount << ",\n"
                    << "  \"followersPerGuide\": " << kFixedFollowersPerGuide << ",\n"
                    << "  \"displayStrandCount\": " << kFixedDisplayStrandCount << ",\n"
                    << "  \"particlesPerStrand\": " << kFixedParticlesPerStrand << ",\n"
                    << "  \"solverIterations\": " << kFixedSolverIterations << ",\n"
                    << "  \"groupsPerBlock\": " << groupsPerBlock << ",\n"
                    << "  \"maskSourceMode\": \"Procedural\",\n"
                    << "  \"editStackApplied\": false,\n"
                    << "  \"assetFormatVersion\": 1,\n"
                    << "  \"topologyFormatVersion\": 1\n"
                    << "}\n";
                settings.flush();
                if (!settings)
                {
                    return 72;
                }
            }

            auto replaceAtomically = [&ec](const std::filesystem::path& temporary,
                                           const std::filesystem::path& destination) -> bool
            {
                ec.clear();
                std::filesystem::remove(destination, ec);
                ec.clear();
                std::filesystem::rename(temporary, destination, ec);
                return !ec;
            };
            if (!replaceAtomically(temporaryAssetPath, assetPath) ||
                !replaceAtomically(temporaryTopologyPath, topologyPath) ||
                !replaceAtomically(temporarySettingsPath, settingsPath))
            {
                return 73;
            }

            return ValidateCanonicalFrozenAssetSource(outputDirectory);
        }
        catch (...)
        {
            return 79;
        }
    }

    int GpuHairViewer::ValidateCanonicalFrozenAssetSource(
        const std::filesystem::path& sourceDirectory)
    {
        struct FrozenTopologyHeader
        {
            char magic[8];
            uint32_t version;
            uint32_t guideStrandCount;
            uint32_t followerStrandCount;
            uint32_t particlesPerStrand;
            uint32_t groupsPerBlock;
            uint32_t strandRangeCount;
            uint32_t blockCount;
            uint64_t totalGroupCount;
        };

        try
        {
            const std::filesystem::path assetPath = sourceDirectory / L"novelty01_100k.hairbin";
            const std::filesystem::path topologyPath = sourceDirectory / L"novelty01_100k_topology.bin";
            const std::filesystem::path settingsPath = sourceDirectory / L"generator_settings.json";
            std::error_code ec;
            if (!std::filesystem::is_regular_file(assetPath, ec) || ec ||
                !std::filesystem::is_regular_file(topologyPath, ec) || ec ||
                !std::filesystem::is_regular_file(settingsPath, ec) || ec)
            {
                return 80;
            }
            if (std::filesystem::file_size(assetPath, ec) == 0u || ec ||
                std::filesystem::file_size(topologyPath, ec) == 0u || ec ||
                std::filesystem::file_size(settingsPath, ec) == 0u || ec)
            {
                return 81;
            }

            hairgpu::HairAsset asset = {};
            std::string assetError;
            if (!hairgpu::LoadHairAssetFromHairBinary(assetPath, asset, assetError))
            {
                return 82;
            }
            const uint64_t expectedStrandCount =
                static_cast<uint64_t>(kFixedGuideCount) *
                static_cast<uint64_t>(kFixedFollowersPerGuide + 1u);
            const uint64_t expectedPointCount =
                expectedStrandCount * static_cast<uint64_t>(kFixedParticlesPerStrand);
            if (expectedStrandCount != static_cast<uint64_t>(kFixedDisplayStrandCount) ||
                asset.guideStrandCount != kFixedGuideCount ||
                asset.followerStrandCount != kFixedGuideCount * kFixedFollowersPerGuide ||
                asset.particlesPerStrand != kFixedParticlesPerStrand ||
                asset.strands.size() != expectedStrandCount ||
                asset.restPoints.size() != expectedPointCount ||
                asset.pointWidths.size() != expectedPointCount ||
                asset.rootBindings.size() != expectedStrandCount ||
                asset.followData.size() != expectedStrandCount)
            {
                return 83;
            }
            for (const hairgpu::Vec3& point : asset.restPoints)
            {
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                {
                    return 84;
                }
            }
            for (const hairgpu::StrandRange& strand : asset.strands)
            {
                const uint64_t end =
                    static_cast<uint64_t>(strand.pointOffset) + strand.pointCount;
                if (strand.pointCount != kFixedParticlesPerStrand || end > asset.restPoints.size())
                {
                    return 85;
                }
            }

            constexpr char expectedMagic[8] = { 'H', 'G', 'T', 'O', 'P', '0', '1', '\0' };
            constexpr uint32_t groupsPerBlock = 8u;
            const uint32_t groupsPerStrand = kFixedParticlesPerStrand - 1u;
            const uint32_t blocksPerStrand =
                (groupsPerStrand + groupsPerBlock - 1u) / groupsPerBlock;
            FrozenTopologyHeader header = {};
            std::ifstream topology(topologyPath, std::ios::binary);
            if (!topology.read(reinterpret_cast<char*>(&header), sizeof(header)))
            {
                return 86;
            }
            if (std::memcmp(header.magic, expectedMagic, sizeof(expectedMagic)) != 0 ||
                header.version != 1u ||
                header.guideStrandCount != kFixedGuideCount ||
                header.followerStrandCount != kFixedGuideCount * kFixedFollowersPerGuide ||
                header.particlesPerStrand != kFixedParticlesPerStrand ||
                header.groupsPerBlock != groupsPerBlock ||
                header.strandRangeCount != kFixedGuideCount ||
                header.blockCount != kFixedGuideCount * blocksPerStrand ||
                header.totalGroupCount != static_cast<uint64_t>(kFixedGuideCount) * groupsPerStrand)
            {
                return 87;
            }
            for (uint32_t strandIndex = 0u; strandIndex < kFixedGuideCount; ++strandIndex)
            {
                hairgpu::SegmentGroupRange range = {};
                if (!topology.read(reinterpret_cast<char*>(&range), sizeof(range)) ||
                    range.firstGroup != strandIndex * groupsPerStrand ||
                    range.groupCount != groupsPerStrand)
                {
                    return 88;
                }
            }
            for (uint32_t strandIndex = 0u; strandIndex < kFixedGuideCount; ++strandIndex)
            {
                for (uint32_t blockIndex = 0u; blockIndex < blocksPerStrand; ++blockIndex)
                {
                    hairgpu::SegmentGroupBlockRange block = {};
                    const uint32_t localFirst = blockIndex * groupsPerBlock;
                    const uint32_t expectedCount =
                        std::min(groupsPerBlock, groupsPerStrand - localFirst);
                    if (!topology.read(reinterpret_cast<char*>(&block), sizeof(block)) ||
                        block.firstGroup != strandIndex * groupsPerStrand + localFirst ||
                        block.groupCount != expectedCount ||
                        block.strandIndex != strandIndex ||
                        block.blockIndex != blockIndex)
                    {
                        return 89;
                    }
                }
            }
            char trailing = 0;
            if (topology.read(&trailing, 1))
            {
                return 90;
            }

            std::ifstream settings(settingsPath, std::ios::binary);
            if (!settings)
            {
                return 91;
            }
            settings.seekg(0, std::ios::end);
            const std::streamoff settingsSize = settings.tellg();
            if (settingsSize <= 0)
            {
                return 92;
            }
            settings.seekg(0, std::ios::beg);
            std::string settingsText(static_cast<size_t>(settingsSize), '\0');
            settings.read(settingsText.data(), settingsSize);
            if (!settings)
            {
                return 93;
            }
            const char* requiredTokens[] = {
                "\"schemaVersion\": \"novelty01-generator-settings-v1\"",
                "\"guideStrandCount\": 4000",
                "\"followersPerGuide\": 24",
                "\"displayStrandCount\": 100000",
                "\"particlesPerStrand\": 128",
                "\"solverIterations\": 6",
                "\"groupsPerBlock\": 8",
                "\"maskSourceMode\": \"Procedural\"",
                "\"editStackApplied\": false"
            };
            for (const char* token : requiredTokens)
            {
                if (settingsText.find(token) == std::string::npos)
                {
                    return 94;
                }
            }
            return 0;
        }
        catch (...)
        {
            return 99;
        }
    }

    int GpuHairViewer::GetSelectedInlineLayerIndex() const
    {
        if (!m_listInlineLayers)
        {
            return m_selectedInlineLayerIndex;
        }
        const LRESULT selected = SendMessageW(m_listInlineLayers, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR)
        {
            return -1;
        }
        return static_cast<int>(selected);
    }

    void GpuHairViewer::SetSelectedInlineLayerIndex(int layerIndex)
    {
        m_selectedInlineLayerIndex = layerIndex;
        if (!m_listInlineLayers)
        {
            return;
        }
        const LRESULT count = SendMessageW(m_listInlineLayers, LB_GETCOUNT, 0, 0);
        if (count == LB_ERR || count <= 0)
        {
            SendMessageW(m_listInlineLayers, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
            m_selectedInlineLayerIndex = -1;
            return;
        }
        const int clamped = std::clamp(layerIndex, 0, static_cast<int>(count) - 1);
        SendMessageW(m_listInlineLayers, LB_SETCURSEL, static_cast<WPARAM>(clamped), 0);
        m_selectedInlineLayerIndex = clamped;
    }

    void GpuHairViewer::RefreshInlineLayerListFromText(int preferredSelection)
    {
        if (!m_listInlineLayers)
        {
            return;
        }

        hairgpu::EditStack stack = {};
        std::string error;
        const bool parsed = LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error);

        const bool prevSyncing = m_syncingControls;
        m_syncingControls = true;
        SendMessageW(m_listInlineLayers, LB_RESETCONTENT, 0, 0);

        if (!parsed || stack.layers.empty())
        {
            m_selectedInlineLayerIndex = -1;
            m_syncingControls = prevSyncing;
            RefreshQuickEditFormLabels();
            return;
        }

        auto modeLabel = [](hairgpu::RegionSelectMode mode)
        {
            switch (mode)
            {
            case hairgpu::RegionSelectMode::Ellipse: return L"Ellipse";
            case hairgpu::RegionSelectMode::Rectangle: return L"Rectangle";
            case hairgpu::RegionSelectMode::Sector: return L"Sector";
            default: return L"All";
            }
        };

        for (size_t i = 0; i < stack.layers.size(); ++i)
        {
            const hairgpu::EditLayer& layer = stack.layers[i];
            std::wstringstream ss;
            ss << (i + 1) << L". ";
            if (!layer.enabled)
            {
                ss << L"[off] ";
            }
            ss << Utf8ToWide(layer.name.empty() ? std::string("layer") : layer.name);
            ss << L"  (" << modeLabel(layer.region.mode) << L", w " << FormatFloat(layer.layerWeight, 2) << L", len " << FormatFloat(layer.edit.lengthScale, 2)
               << L", width " << FormatFloat(layer.edit.widthScale, 2) << L", follow " << FormatFloat(layer.edit.followerInfluence, 2);
            if (layer.region.groupId != UINT32_MAX)
            {
                ss << L", g " << layer.region.groupId;
            }
            ss << (layer.region.guidesOnly ? L", guides" : L", all") << L")";
            SendMessageW(m_listInlineLayers, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ss.str().c_str()));
        }

        const int target = (preferredSelection >= 0) ? preferredSelection : ((m_selectedInlineLayerIndex >= 0) ? m_selectedInlineLayerIndex : 0);
        m_syncingControls = prevSyncing;
        SetSelectedInlineLayerIndex(target);
        RefreshQuickEditFormLabels();
    }

    void GpuHairViewer::SyncQuickEditFormControlsFromState()
    {
        const bool prevSyncing = m_syncingControls;
        m_syncingControls = true;
        SendMessageW(m_comboQuickEditMode, CB_SETCURSEL,
            (m_quickEditMode == hairgpu::RegionSelectMode::Rectangle) ? 1 :
            (m_quickEditMode == hairgpu::RegionSelectMode::Sector) ? 2 : 0, 0);
        SendMessageW(m_checkQuickEditEnabled, BM_SETCHECK, m_quickEditEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextFormat(m_editQuickEditName, m_quickEditLayerName);
        SendMessageW(m_trackQuickEditFeather, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditFeather, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditLengthScale, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditLengthScale, 0.6f, 1.4f));
        SendMessageW(m_trackQuickEditBoundarySmoothing, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundarySmoothing, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditBoundaryRadiusUv, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f));
        SendMessageW(m_trackQuickEditLayerWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditLayerWeight, 0.0f, 1.5f));
        SendMessageW(m_trackQuickEditCenterU, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditCenterU, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditCenterV, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditCenterV, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditRadiusU, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRadiusU, 0.01f, 0.60f));
        SendMessageW(m_trackQuickEditRadiusV, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRadiusV, 0.01f, 0.60f));
        SendMessageW(m_trackQuickEditInnerRadius, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditInnerRadius, 0.0f, 0.95f));
        SendMessageW(m_trackQuickEditAngleCenter, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditAngleCenterRadians, -3.1415926f, 3.1415926f));
        SendMessageW(m_trackQuickEditAngleHalfExtent, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditAngleHalfExtentRadians, 0.05f, 3.1415926f));
        SendMessageW(m_trackQuickEditRootOffsetX, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRootOffsetX, -0.12f, 0.12f));
        SendMessageW(m_trackQuickEditRootOffsetY, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRootOffsetY, -0.12f, 0.12f));
        SendMessageW(m_trackQuickEditRootOffsetZ, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRootOffsetZ, -0.12f, 0.12f));
        SendMessageW(m_trackQuickEditTipOffsetX, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditTipOffsetX, -0.20f, 0.20f));
        SendMessageW(m_trackQuickEditTipOffsetY, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditTipOffsetY, -0.20f, 0.20f));
        SendMessageW(m_trackQuickEditTipOffsetZ, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditTipOffsetZ, -0.20f, 0.20f));
        SendMessageW(m_trackQuickEditSideOffset, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditSideOffset, -0.20f, 0.20f));
        SendMessageW(m_trackQuickEditYaw, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditYawRadians, -1.5707963f, 1.5707963f));
        SendMessageW(m_trackQuickEditWidthScale, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditWidthScale, 0.50f, 1.80f));
        SendMessageW(m_trackQuickEditFollowerInfluence, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditFollowerInfluence, 0.0f, 1.5f));
        SendMessageW(m_trackQuickEditBoundarySmoothing, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundarySmoothing, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditBoundaryRadiusUv, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f));
        SetWindowTextFormat(m_editQuickEditGroupId, (m_quickEditGroupId == UINT32_MAX) ? std::wstring(L"") : std::to_wstring(m_quickEditGroupId));
        SendMessageW(m_checkQuickEditGuidesOnly, BM_SETCHECK, m_quickEditGuidesOnly ? BST_CHECKED : BST_UNCHECKED, 0);
        RefreshQuickEditFormLabels();
        m_syncingControls = prevSyncing;
    }

    void GpuHairViewer::LoadQuickEditFormFromInlineText(int preferredSelection)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            m_quickEditLayerName = L"quick_form";
            m_quickEditCenterU = 0.50f;
            m_quickEditCenterV = 0.50f;
            m_quickEditRadiusU = 0.18f;
            m_quickEditRadiusV = 0.18f;
            m_quickEditInnerRadius = 0.00f;
            m_quickEditAngleCenterRadians = 0.00f;
            m_quickEditAngleHalfExtentRadians = 3.1415926f;
            m_quickEditRootOffsetX = 0.00f;
            m_quickEditRootOffsetY = 0.00f;
            m_quickEditRootOffsetZ = 0.00f;
            m_quickEditTipOffsetX = 0.02f;
            m_quickEditTipOffsetY = 0.00f;
            m_quickEditTipOffsetZ = -0.08f;
            m_quickEditSideOffset = 0.00f;
            m_quickEditYawRadians = 0.00f;
            m_quickEditWidthScale = 1.00f;
            m_quickEditFollowerInfluence = 1.00f;
            m_quickEditBoundarySmoothing = 0.42f;
            m_quickEditBoundaryRadiusUv = 0.10f;
            m_quickEditGroupId = UINT32_MAX;
            m_quickEditGuidesOnly = true;
            RefreshInlineLayerListFromText(-1);
            SyncQuickEditFormControlsFromState();
            if (IsEditInfluenceOverlayMode())
            {
                UpdateScalpOverlayVertexBuffer();
            }
            return;
        }

        m_quickEditBoundarySmoothing = hairgpu::Clamp(stack.boundarySmoothing, 0.0f, 1.0f);
        if (m_quickEditBoundarySmoothing <= 0.0f)
        {
            m_quickEditBoundarySmoothing = 0.42f;
        }
        m_quickEditBoundaryRadiusUv = hairgpu::Clamp(stack.boundaryRadiusUv, 0.01f, 0.30f);

        int layerIndex = preferredSelection;
        if (layerIndex < 0)
        {
            layerIndex = GetSelectedInlineLayerIndex();
        }
        if (layerIndex < 0)
        {
            layerIndex = m_selectedInlineLayerIndex;
        }
        layerIndex = std::clamp(layerIndex, 0, static_cast<int>(stack.layers.size()) - 1);
        m_selectedInlineLayerIndex = layerIndex;

        const hairgpu::EditLayer& layer = stack.layers[static_cast<size_t>(layerIndex)];
        m_quickEditLayerName = Utf8ToWide(layer.name.empty() ? std::string("layer") : layer.name);
        m_quickEditEnabled = layer.enabled;
        m_quickEditMode = layer.region.mode;
        if (m_quickEditMode == hairgpu::RegionSelectMode::All)
        {
            m_quickEditMode = hairgpu::RegionSelectMode::Ellipse;
        }
        m_quickEditFeather = hairgpu::Clamp(layer.region.feather, 0.0f, 1.0f);
        m_quickEditLengthScale = hairgpu::Clamp(layer.edit.lengthScale, 0.6f, 1.4f);
        m_quickEditLayerWeight = hairgpu::Clamp(layer.layerWeight, 0.0f, 1.5f);
        m_quickEditCenterU = hairgpu::Clamp(layer.region.centerUv.x, 0.0f, 1.0f);
        m_quickEditCenterV = hairgpu::Clamp(layer.region.centerUv.y, 0.0f, 1.0f);
        m_quickEditRadiusU = hairgpu::Clamp(layer.region.radiusUv.x, 0.01f, 0.60f);
        m_quickEditRadiusV = hairgpu::Clamp(layer.region.radiusUv.y, 0.01f, 0.60f);
        m_quickEditInnerRadius = hairgpu::Clamp(layer.region.innerRadius, 0.0f, 0.95f);
        m_quickEditAngleCenterRadians = hairgpu::Clamp(layer.region.angleCenterRadians, -3.1415926f, 3.1415926f);
        m_quickEditAngleHalfExtentRadians = hairgpu::Clamp(layer.region.angleHalfExtentRadians, 0.05f, 3.1415926f);
        m_quickEditRootOffsetX = hairgpu::Clamp(layer.edit.rootOffset.x, -0.12f, 0.12f);
        m_quickEditRootOffsetY = hairgpu::Clamp(layer.edit.rootOffset.y, -0.12f, 0.12f);
        m_quickEditRootOffsetZ = hairgpu::Clamp(layer.edit.rootOffset.z, -0.12f, 0.12f);
        m_quickEditTipOffsetX = hairgpu::Clamp(layer.edit.tipOffset.x, -0.20f, 0.20f);
        m_quickEditTipOffsetY = hairgpu::Clamp(layer.edit.tipOffset.y, -0.20f, 0.20f);
        m_quickEditTipOffsetZ = hairgpu::Clamp(layer.edit.tipOffset.z, -0.20f, 0.20f);
        m_quickEditSideOffset = hairgpu::Clamp(layer.edit.sideOffset, -0.20f, 0.20f);
        m_quickEditYawRadians = hairgpu::Clamp(layer.edit.yawRadians, -1.5707963f, 1.5707963f);
        m_quickEditWidthScale = hairgpu::Clamp(layer.edit.widthScale, 0.50f, 1.80f);
        m_quickEditFollowerInfluence = hairgpu::Clamp(layer.edit.followerInfluence, 0.0f, 1.5f);
        m_quickEditGroupId = layer.region.groupId;
        m_quickEditGuidesOnly = layer.region.guidesOnly;
        // Loading an edit layer into the form must not mutate viewport selection.
        // Explicit picking and session restore own m_selectedStrandIndex.
        // The old implicit assignment made a selected-strand comb/gizmo appear
        // immediately on startup even though the user had not selected hair.
        RefreshInlineLayerListFromText(layerIndex);
        SyncQuickEditFormControlsFromState();
        if (IsEditInfluenceOverlayMode())
        {
            UpdateScalpOverlayVertexBuffer();
        }
    }

    bool GpuHairViewer::IsEditInfluenceOverlayMode() const
    {
        return m_maskOverlayMode == MaskOverlayMode::EditInfluence || m_maskOverlayMode == MaskOverlayMode::EditInfluenceBeforeAfter;
    }

    bool GpuHairViewer::IsBoundaryBeforeAfterOverlayMode() const
    {
        return m_maskOverlayMode == MaskOverlayMode::EditInfluenceBeforeAfter;
    }

    bool GpuHairViewer::IsActiveSetRiskOverlayMode() const
    {
        return m_maskOverlayMode == MaskOverlayMode::ActiveSetRiskSelection ||
               m_maskOverlayMode == MaskOverlayMode::ActiveSetMotion ||
               m_maskOverlayMode == MaskOverlayMode::ActiveSetContact ||
               m_maskOverlayMode == MaskOverlayMode::ActiveSetUncertainty ||
               m_maskOverlayMode == MaskOverlayMode::ActiveSetEdit;
    }

    const wchar_t* GpuHairViewer::GetMaskOverlayModeDisplayName() const
    {
        switch (m_maskOverlayMode)
        {
        case MaskOverlayMode::Parting: return L"Parting";
        case MaskOverlayMode::Region: return L"Region";
        case MaskOverlayMode::EditInfluence: return L"Edit Influence";
        case MaskOverlayMode::EditInfluenceBeforeAfter: return L"Boundary Before/After";
        case MaskOverlayMode::ActiveSetRiskSelection: return L"Active-Set Risk";
        case MaskOverlayMode::ActiveSetMotion: return L"Active-Set Motion";
        case MaskOverlayMode::ActiveSetContact: return L"Active-Set Contact";
        case MaskOverlayMode::ActiveSetUncertainty: return L"Active-Set Uncertainty";
        case MaskOverlayMode::ActiveSetEdit: return L"Active-Set Edit";
        default: return L"None";
        }
    }

    float GpuHairViewer::GetActiveSetOverlayScoreValue(const hairgpu::ActiveSetClusterScore& score) const
    {
        switch (m_maskOverlayMode)
        {
        case MaskOverlayMode::ActiveSetMotion:
            return hairgpu::Clamp(score.motionScore, 0.0f, 1.0f);
        case MaskOverlayMode::ActiveSetContact:
            return hairgpu::Clamp(score.contactScore, 0.0f, 1.0f);
        case MaskOverlayMode::ActiveSetUncertainty:
            return hairgpu::Clamp(score.uncertaintyScore, 0.0f, 1.0f);
        case MaskOverlayMode::ActiveSetEdit:
            return hairgpu::Clamp(score.editScore, 0.0f, 1.0f);
        case MaskOverlayMode::ActiveSetRiskSelection:
        default:
            return hairgpu::Clamp(score.finalScore, 0.0f, 1.0f);
        }
    }

    bool GpuHairViewer::TryBuildSelectedInlineLayerPreview(std::vector<float>& outGuideInfluence, uint32_t& outActiveGuideCount, float& outMaxInfluence) const
    {
        outGuideInfluence.clear();
        outActiveGuideCount = 0u;
        outMaxInfluence = 0.0f;

        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            return false;
        }

        int layerIndex = GetSelectedInlineLayerIndex();
        if (layerIndex < 0)
        {
            layerIndex = m_selectedInlineLayerIndex;
        }
        if (layerIndex < 0 || stack.layers.empty())
        {
            return false;
        }
        layerIndex = std::clamp(layerIndex, 0, static_cast<int>(stack.layers.size()) - 1);

        const hairgpu::HairAsset& previewAsset = m_baseAsset.strands.empty() ? m_asset : m_baseAsset;
        const uint32_t guideCount = std::min<uint32_t>(previewAsset.guideStrandCount, static_cast<uint32_t>(previewAsset.strands.size()));
        if (guideCount == 0u)
        {
            return false;
        }

        outGuideInfluence.assign(guideCount, 0.0f);
        const hairgpu::EditLayer& layer = stack.layers[static_cast<size_t>(layerIndex)];
        if (!layer.enabled)
        {
            return true;
        }

        const hairgpu::RegionSelectionResult selection = hairgpu::SelectRegion(previewAsset, layer.region);
        for (const hairgpu::SelectedStrand& selected : selection.strands)
        {
            if (selected.strandIndex >= guideCount)
            {
                continue;
            }
            const float influence = hairgpu::Clamp(selected.weight * layer.layerWeight, 0.0f, 1.0f);
            outGuideInfluence[selected.strandIndex] = std::max(outGuideInfluence[selected.strandIndex], influence);
        }

        for (float influence : outGuideInfluence)
        {
            if (influence > 1.0e-4f)
            {
                ++outActiveGuideCount;
                outMaxInfluence = std::max(outMaxInfluence, influence);
            }
        }
        return true;
    }

    bool GpuHairViewer::BuildEditVisualizationCategories(std::vector<uint32_t>& outCategories) const
    {
        const size_t totalStrands = m_asset.strands.size();
        outCategories.assign(totalStrands, 1u);
        if (totalStrands == 0u)
        {
            return false;
        }

        const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(totalStrands));
        std::vector<float> guideInfluence;
        uint32_t activeGuideCount = 0u;
        float maxInfluence = 0.0f;
        bool havePreview = TryBuildSelectedInlineLayerPreview(guideInfluence, activeGuideCount, maxInfluence);
        if (!havePreview || guideInfluence.size() != guideCount)
        {
            guideInfluence.assign(guideCount, 0.0f);
            const hairgpu::HairAsset& previewAsset = m_baseAsset.strands.empty() ? m_asset : m_baseAsset;
            hairgpu::EditLayer layer = BuildQuickEditLayerFromUi();
            const hairgpu::RegionSelectionResult selection = hairgpu::SelectRegion(previewAsset, layer.region);
            for (const hairgpu::SelectedStrand& selected : selection.strands)
            {
                if (selected.strandIndex < guideCount)
                {
                    guideInfluence[selected.strandIndex] = std::max(guideInfluence[selected.strandIndex], hairgpu::Clamp(selected.weight * layer.layerWeight, 0.0f, 1.0f));
                }
            }
            havePreview = true;
        }

        const float editableThreshold = 0.50f;
        const float previewBoundaryThreshold = 0.05f;
        const float uvBoundaryRadius = 0.085f;
        std::vector<Vec2> guideRootUv(guideCount, Vec2{ 0.5f, 0.5f });
        if (m_asset.rootBindings.size() >= guideCount)
        {
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                guideRootUv[guideIndex] = m_asset.rootBindings[guideIndex].rootUv;
            }
        }

        std::vector<uint32_t> stronglyEditableGuides;
        stronglyEditableGuides.reserve(guideCount);
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            const float influence = (guideIndex < guideInfluence.size()) ? guideInfluence[guideIndex] : 0.0f;
            if (influence >= editableThreshold)
            {
                outCategories[guideIndex] = 0u;
                stronglyEditableGuides.push_back(guideIndex);
            }
            else if (influence > previewBoundaryThreshold)
            {
                outCategories[guideIndex] = 4u;
            }
        }

        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            if (outCategories[guideIndex] != 1u)
            {
                continue;
            }
            const Vec2 uv = guideRootUv[guideIndex];
            for (uint32_t editableGuide : stronglyEditableGuides)
            {
                const Vec2 refUv = guideRootUv[editableGuide];
                const float du = uv.x - refUv.x;
                const float dv = uv.y - refUv.y;
                if ((du * du + dv * dv) <= (uvBoundaryRadius * uvBoundaryRadius))
                {
                    outCategories[guideIndex] = 4u;
                    break;
                }
            }
        }

        for (uint32_t strandIndex = guideCount; strandIndex < static_cast<uint32_t>(totalStrands); ++strandIndex)
        {
            uint32_t category = 1u;
            if (strandIndex < m_asset.followData.size())
            {
                const int32_t parentGuide = m_asset.followData[strandIndex].parentGuide;
                if (parentGuide >= 0 && static_cast<uint32_t>(parentGuide) < guideCount)
                {
                    category = outCategories[static_cast<uint32_t>(parentGuide)];
                }
            }
            outCategories[strandIndex] = category;
        }

        if (m_selectedStrandIndex >= 0 && static_cast<size_t>(m_selectedStrandIndex) < totalStrands)
        {
            const uint32_t selected = static_cast<uint32_t>(m_selectedStrandIndex);
            outCategories[selected] = 2u;
            if (selected < guideCount)
            {
                for (uint32_t strandIndex = guideCount; strandIndex < static_cast<uint32_t>(totalStrands); ++strandIndex)
                {
                    if (strandIndex < m_asset.followData.size() && m_asset.followData[strandIndex].parentGuide == m_selectedStrandIndex)
                    {
                        outCategories[strandIndex] = 3u;
                    }
                }
            }
            else if (selected < m_asset.followData.size())
            {
                const int32_t parentGuide = m_asset.followData[selected].parentGuide;
                if (parentGuide >= 0 && static_cast<uint32_t>(parentGuide) < guideCount)
                {
                    outCategories[static_cast<uint32_t>(parentGuide)] = 3u;
                }
            }
        }
        return havePreview;
    }

    hairgpu::EditLayer GpuHairViewer::BuildQuickEditLayerFromUi() const
    {
        hairgpu::EditLayer layer = {};
        std::string layerName = WideToUtf8(m_quickEditLayerName);
        for (char& c : layerName)
        {
            if (c == ',' || c == '\r' || c == '\n')
            {
                c = '_';
            }
        }
        if (layerName.empty())
        {
            layerName = "quick_form";
        }
        layer.name = layerName;
        layer.enabled = m_quickEditEnabled;
        layer.layerWeight = m_quickEditLayerWeight;
        layer.region.mode = m_quickEditMode;
        layer.region.centerUv = { hairgpu::Clamp(m_quickEditCenterU, 0.0f, 1.0f), hairgpu::Clamp(m_quickEditCenterV, 0.0f, 1.0f) };
        layer.region.radiusUv = { hairgpu::Clamp(m_quickEditRadiusU, 0.01f, 0.60f), hairgpu::Clamp(m_quickEditRadiusV, 0.01f, 0.60f) };
        const float quickEditMinRadius = std::min(layer.region.radiusUv.x, layer.region.radiusUv.y);
        layer.region.innerRadius = hairgpu::Clamp(m_quickEditInnerRadius, 0.0f, quickEditMinRadius * 0.95f);
        layer.region.angleCenterRadians = hairgpu::Clamp(m_quickEditAngleCenterRadians, -3.1415926f, 3.1415926f);
        layer.region.angleHalfExtentRadians = hairgpu::Clamp(m_quickEditAngleHalfExtentRadians, 0.05f, 3.1415926f);
        layer.region.feather = m_quickEditFeather;
        layer.region.groupId = m_quickEditGroupId;
        layer.region.guidesOnly = m_quickEditGuidesOnly;
        layer.edit.lengthScale = m_quickEditLengthScale;
        layer.edit.widthScale = hairgpu::Clamp(m_quickEditWidthScale, 0.50f, 1.80f);
        layer.edit.followerInfluence = hairgpu::Clamp(m_quickEditFollowerInfluence, 0.0f, 1.5f);
        layer.edit.rootOffset = {
            hairgpu::Clamp(m_quickEditRootOffsetX, -0.12f, 0.12f),
            hairgpu::Clamp(m_quickEditRootOffsetY, -0.12f, 0.12f),
            hairgpu::Clamp(m_quickEditRootOffsetZ, -0.12f, 0.12f)
        };
        layer.edit.tipOffset = {
            hairgpu::Clamp(m_quickEditTipOffsetX, -0.20f, 0.20f),
            hairgpu::Clamp(m_quickEditTipOffsetY, -0.20f, 0.20f),
            hairgpu::Clamp(m_quickEditTipOffsetZ, -0.20f, 0.20f)
        };
        layer.edit.sideOffset = hairgpu::Clamp(m_quickEditSideOffset, -0.20f, 0.20f);
        layer.edit.yawRadians = hairgpu::Clamp(m_quickEditYawRadians, -1.5707963f, 1.5707963f);
        if (m_selectedStrandIndex >= 0 && static_cast<size_t>(m_selectedStrandIndex) < m_asset.strands.size())
        {
            const uint32_t selectedIndex = static_cast<uint32_t>(m_selectedStrandIndex);
            layer.strandSelection = { selectedIndex };
            layer.strandSelectionMode = hairgpu::StrandSelectionMode::Replace;
            layer.strandSelectionWeight = 1.0f;
        }
        return layer;
    }

    bool GpuHairViewer::PushQuickEditFormToInlineText(bool replaceSelectedLayer, bool applyAfterPush)
    {
        SyncStateFromControls();

        hairgpu::EditStack stack = {};
        std::string error;
        const std::string inlineUtf8 = WideToUtf8(GetInlineEditStackText());
        if (!inlineUtf8.empty())
        {
            LocalLoadEditStackCsvText(inlineUtf8, stack, error);
        }
        stack.boundarySmoothing = hairgpu::Clamp(m_quickEditBoundarySmoothing, 0.0f, 1.0f);
        stack.boundaryRadiusUv = hairgpu::Clamp(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f);
        if (stack.boundarySmoothing <= 0.0f)
        {
            stack.boundarySmoothing = 0.42f;
        }

        hairgpu::EditLayer layer = BuildQuickEditLayerFromUi();
        int selectionAfterWrite = static_cast<int>(stack.layers.size());
        if (replaceSelectedLayer && !stack.layers.empty())
        {
            const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
            stack.layers[static_cast<size_t>(selectedIndex)] = layer;
            selectionAfterWrite = selectedIndex;
        }
        else
        {
            stack.layers.push_back(layer);
        }

        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        LoadQuickEditFormFromInlineText(selectionAfterWrite);
        if (applyAfterPush)
        {
            return ApplyInlineEditStackFromUi(true);
        }
        return true;
    }

    bool GpuHairViewer::MoveSelectedInlineLayer(int direction, bool applyAfterMove)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.size() < 2)
        {
            return false;
        }

        const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
        const int targetIndex = std::clamp(selectedIndex + direction, 0, static_cast<int>(stack.layers.size()) - 1);
        if (targetIndex == selectedIndex)
        {
            return false;
        }

