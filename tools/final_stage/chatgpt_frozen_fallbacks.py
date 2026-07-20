from __future__ import annotations


EXPORT_CPP = r'''    int GpuHairViewer::ExportCanonicalFrozenAssetSource(
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
'''


VALIDATE_CPP = r'''    int GpuHairViewer::ValidateCanonicalFrozenAssetSource(
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
'''


def patch_generator_script(script: str) -> str:
    marker = "from __future__ import annotations\n"
    payload = "\nFROZEN_FALLBACKS = " + repr({
        "ExportCanonicalFrozenAssetSource": EXPORT_CPP,
        "ValidateCanonicalFrozenAssetSource": VALIDATE_CPP,
    }) + "\n"
    if "FROZEN_FALLBACKS =" not in script:
        script = script.replace(marker, marker + payload, 1)

    old = '''for function_name in ("ExportCanonicalFrozenAssetSource", "ValidateCanonicalFrozenAssetSource"):
    if f"GpuHairViewer::{function_name}(" not in text:
        recovered_blocks.append(recover_historical_function(function_name))'''
    new = '''for function_name in ("ExportCanonicalFrozenAssetSource", "ValidateCanonicalFrozenAssetSource"):
    if f"GpuHairViewer::{function_name}(" not in text:
        try:
            recovered_blocks.append(recover_historical_function(function_name))
        except RuntimeError:
            print(f"Using canonical frozen-asset implementation for {function_name}")
            recovered_blocks.append(FROZEN_FALLBACKS[function_name])'''
    if old not in script:
        raise RuntimeError("Frozen-asset recovery block was not found in generator script")
    return script.replace(old, new, 1)
