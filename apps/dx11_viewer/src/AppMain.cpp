#include "GpuHairViewer.h"

#include "hairgpu/AssetIO.h"
#include "hairgpu/Types.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    constexpr uint32_t kGuideCount = 4000u;
    constexpr uint32_t kFollowersPerGuide = 24u;
    constexpr uint32_t kDisplayStrandCount = 100000u;
    constexpr uint32_t kParticlesPerStrand = 128u;
    constexpr uint32_t kSolverIterations = 6u;
    constexpr uint32_t kGroupsPerBlock = 8u;
    constexpr uint32_t kGroupsPerGuide = kParticlesPerStrand - 1u;
    constexpr float kReferenceLengthMeters = 0.50f;
    constexpr char kTopologyMagic[8] = { 'H', 'G', 'P', 'U', 'T', 'O', 'P', '1' };

    static_assert(kGuideCount * (kFollowersPerGuide + 1u) == kDisplayStrandCount,
                  "The canonical frozen source must contain exactly 100,000 strands.");

#pragma pack(push, 1)
    struct CanonicalTopologyHeader
    {
        char magic[8] = {};
        uint32_t formatVersion = 1u;
        uint32_t guideStrandCount = 0u;
        uint32_t followerStrandCount = 0u;
        uint32_t displayStrandCount = 0u;
        uint32_t particlesPerStrand = 0u;
        uint32_t groupsPerGuide = 0u;
        uint32_t groupsPerBlock = 0u;
        uint32_t guideGroupCount = 0u;
        uint32_t blockCount = 0u;
        uint64_t topologyHash = 0u;
    };

    struct CanonicalGuideTopologyRecord
    {
        uint32_t guideIndex = 0u;
        uint32_t firstGroup = 0u;
        uint32_t groupCount = 0u;
        uint32_t firstBlock = 0u;
        uint32_t blockCount = 0u;
    };
#pragma pack(pop)

    bool ArgumentEquals(int index, const wchar_t* expected)
    {
        return index >= 0 && index < __argc && __wargv[index] != nullptr &&
               std::wstring(__wargv[index]) == expected;
    }

    uint64_t Fnv1a64(const void* data, size_t size, uint64_t hash = 1469598103934665603ull)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::filesystem::path ResolveBundleMember(
        const std::filesystem::path& root,
        const std::filesystem::path& nested,
        const std::filesystem::path& flat)
    {
        const std::filesystem::path nestedPath = root / nested;
        if (std::filesystem::is_regular_file(nestedPath))
        {
            return nestedPath;
        }
        return root / flat;
    }

    bool WriteCanonicalTopology(const std::filesystem::path& outputPath)
    {
        const uint32_t blocksPerGuide =
            (kGroupsPerGuide + kGroupsPerBlock - 1u) / kGroupsPerBlock;
        const uint64_t totalGroups64 =
            static_cast<uint64_t>(kGuideCount) * kGroupsPerGuide;
        const uint64_t totalBlocks64 =
            static_cast<uint64_t>(kGuideCount) * blocksPerGuide;
        if (totalGroups64 > std::numeric_limits<uint32_t>::max() ||
            totalBlocks64 > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }

        std::vector<CanonicalGuideTopologyRecord> records;
        records.reserve(kGuideCount);
        for (uint32_t guide = 0u; guide < kGuideCount; ++guide)
        {
            CanonicalGuideTopologyRecord record;
            record.guideIndex = guide;
            record.firstGroup = guide * kGroupsPerGuide;
            record.groupCount = kGroupsPerGuide;
            record.firstBlock = guide * blocksPerGuide;
            record.blockCount = blocksPerGuide;
            records.push_back(record);
        }

        CanonicalTopologyHeader header;
        std::memcpy(header.magic, kTopologyMagic, sizeof(kTopologyMagic));
        header.formatVersion = 1u;
        header.guideStrandCount = kGuideCount;
        header.followerStrandCount = kGuideCount * kFollowersPerGuide;
        header.displayStrandCount = kDisplayStrandCount;
        header.particlesPerStrand = kParticlesPerStrand;
        header.groupsPerGuide = kGroupsPerGuide;
        header.groupsPerBlock = kGroupsPerBlock;
        header.guideGroupCount = static_cast<uint32_t>(totalGroups64);
        header.blockCount = static_cast<uint32_t>(totalBlocks64);
        header.topologyHash = Fnv1a64(records.data(), records.size() * sizeof(records.front()));

        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return false;
        }
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        output.write(reinterpret_cast<const char*>(records.data()),
                     static_cast<std::streamsize>(records.size() * sizeof(records.front())));
        output.flush();
        return output.good();
    }

    bool WriteGeneratorSettings(const std::filesystem::path& outputPath)
    {
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return false;
        }
        output
            << "{\n"
            << "  \"schemaVersion\": \"novelty01-generator-settings-v1\",\n"
            << "  \"guideStrandCount\": " << kGuideCount << ",\n"
            << "  \"followersPerGuide\": " << kFollowersPerGuide << ",\n"
            << "  \"displayStrandCount\": " << kDisplayStrandCount << ",\n"
            << "  \"particlesPerStrand\": " << kParticlesPerStrand << ",\n"
            << "  \"solverIterations\": " << kSolverIterations << ",\n"
            << "  \"groupsPerBlock\": " << kGroupsPerBlock << ",\n"
            << "  \"maskSourceMode\": \"Procedural\",\n"
            << "  \"editStackApplied\": false\n"
            << "}\n";
        output.flush();
        return output.good();
    }

    bool TextContainsKeyValue(const std::string& text,
                              const std::string& key,
                              const std::string& value)
    {
        const std::string quotedKey = "\"" + key + "\"";
        const size_t keyPosition = text.find(quotedKey);
        if (keyPosition == std::string::npos)
        {
            return false;
        }
        const size_t colon = text.find(':', keyPosition + quotedKey.size());
        if (colon == std::string::npos)
        {
            return false;
        }
        const size_t valuePosition = text.find(value, colon + 1u);
        return valuePosition != std::string::npos;
    }

    int ValidateCanonicalFrozenSource(const std::filesystem::path& root)
    {
        const std::filesystem::path assetPath = ResolveBundleMember(
            root, L"asset/novelty01_100k.hairbin", L"novelty01_100k.hairbin");
        const std::filesystem::path topologyPath = ResolveBundleMember(
            root, L"topology/novelty01_100k_topology.bin", L"novelty01_100k_topology.bin");
        const std::filesystem::path settingsPath = ResolveBundleMember(
            root, L"settings/generator_settings.json", L"generator_settings.json");

        hairgpu::HairAsset asset;
        std::string error;
        if (!hairgpu::LoadHairAssetFromHairBinary(assetPath, asset, error))
        {
            return 71;
        }
        if (asset.guideStrandCount != kGuideCount ||
            asset.followerStrandCount != kGuideCount * kFollowersPerGuide ||
            asset.particlesPerStrand != kParticlesPerStrand ||
            asset.strands.size() != kDisplayStrandCount ||
            asset.restPoints.size() !=
                static_cast<size_t>(kDisplayStrandCount) * kParticlesPerStrand ||
            asset.pointWidths.size() != asset.restPoints.size() ||
            asset.materials.size() != asset.strands.size() ||
            asset.rootBindings.size() != asset.strands.size() ||
            asset.followData.size() != asset.strands.size())
        {
            return 72;
        }

        std::ifstream topology(topologyPath, std::ios::binary);
        CanonicalTopologyHeader header;
        if (!topology.read(reinterpret_cast<char*>(&header), sizeof(header)))
        {
            return 73;
        }
        if (std::memcmp(header.magic, kTopologyMagic, sizeof(kTopologyMagic)) != 0 ||
            header.formatVersion != 1u ||
            header.guideStrandCount != kGuideCount ||
            header.followerStrandCount != kGuideCount * kFollowersPerGuide ||
            header.displayStrandCount != kDisplayStrandCount ||
            header.particlesPerStrand != kParticlesPerStrand ||
            header.groupsPerGuide != kGroupsPerGuide ||
            header.groupsPerBlock != kGroupsPerBlock)
        {
            return 74;
        }
        std::vector<CanonicalGuideTopologyRecord> records(kGuideCount);
        if (!topology.read(reinterpret_cast<char*>(records.data()),
                           static_cast<std::streamsize>(records.size() * sizeof(records.front()))))
        {
            return 75;
        }
        const uint64_t observedHash =
            Fnv1a64(records.data(), records.size() * sizeof(records.front()));
        if (observedHash != header.topologyHash)
        {
            return 76;
        }
        topology.peek();
        if (!topology.eof())
        {
            return 77;
        }

        std::ifstream settings(settingsPath, std::ios::binary);
        if (!settings)
        {
            return 78;
        }
        const std::string text{
            std::istreambuf_iterator<char>{settings},
            std::istreambuf_iterator<char>{}};
        if (!TextContainsKeyValue(text, "schemaVersion", "\"novelty01-generator-settings-v1\"") ||
            !TextContainsKeyValue(text, "guideStrandCount", "4000") ||
            !TextContainsKeyValue(text, "followersPerGuide", "24") ||
            !TextContainsKeyValue(text, "displayStrandCount", "100000") ||
            !TextContainsKeyValue(text, "particlesPerStrand", "128") ||
            !TextContainsKeyValue(text, "solverIterations", "6") ||
            !TextContainsKeyValue(text, "groupsPerBlock", "8") ||
            !TextContainsKeyValue(text, "maskSourceMode", "\"Procedural\"") ||
            !TextContainsKeyValue(text, "editStackApplied", "false"))
        {
            return 79;
        }
        return 0;
    }

    int ExportCanonicalFrozenSource(const std::filesystem::path& outputDirectory)
    {
        std::error_code errorCode;
        std::filesystem::create_directories(outputDirectory, errorCode);
        if (errorCode)
        {
            return 65;
        }

        hairgpu::SetSampleMaskOverridePaths(nullptr, nullptr);
        hairgpu::SetSampleMaskSourceMode(hairgpu::SampleMaskSourceMode::Procedural);
        const float segmentLength =
            kReferenceLengthMeters / static_cast<float>(kParticlesPerStrand - 1u);
        const hairgpu::HairAsset asset = hairgpu::BuildSampleHybridHairAsset(
            kGuideCount,
            kFollowersPerGuide,
            kParticlesPerStrand,
            segmentLength);

        if (asset.guideStrandCount != kGuideCount ||
            asset.followerStrandCount != kGuideCount * kFollowersPerGuide ||
            asset.strands.size() != kDisplayStrandCount ||
            asset.particlesPerStrand != kParticlesPerStrand)
        {
            return 66;
        }

        std::string error;
        if (!hairgpu::SaveHairAssetToHairBinary(
                outputDirectory / L"novelty01_100k.hairbin", asset, error))
        {
            return 67;
        }
        if (!WriteCanonicalTopology(
                outputDirectory / L"novelty01_100k_topology.bin"))
        {
            return 68;
        }
        if (!WriteGeneratorSettings(outputDirectory / L"generator_settings.json"))
        {
            return 69;
        }
        const int validationResult = ValidateCanonicalFrozenSource(outputDirectory);
        return validationResult == 0 ? 0 : 70 + validationResult;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    SetCurrentDirectoryW(exeDir.c_str());

    if (ArgumentEquals(1, L"--export-frozen-source"))
    {
        if (__argc != 3 || __wargv[2] == nullptr)
        {
            return 64;
        }
        return ExportCanonicalFrozenSource(std::filesystem::path(__wargv[2]));
    }

    if (ArgumentEquals(1, L"--validate-frozen-source"))
    {
        if (__argc != 3 || __wargv[2] == nullptr)
        {
            return 64;
        }
        return ValidateCanonicalFrozenSource(std::filesystem::path(__wargv[2]));
    }

    if (ArgumentEquals(1, L"--native-self-test"))
    {
        hairgpu::viewer::GpuHairViewer app;
        return app.RunNativeSelfTests();
    }

    if (ArgumentEquals(1, L"--shader-self-test"))
    {
        SetEnvironmentVariableW(L"HAIRGPU_SHADER_SELF_TEST", L"1");
        hairgpu::viewer::GpuHairViewer app;
        return app.Initialize(instance, SW_HIDE) ? 0 : 2;
    }

    hairgpu::viewer::GpuHairViewer app;
    if (!app.Initialize(instance, showCommand))
    {
        MessageBoxW(
            nullptr,
            L"GpuHairViewer::Initialize returned false.\r\n\r\n"
            L"Startup failed before the normal viewer loop began.\r\n"
            L"A startup diagnostic dialog should already have been shown.",
            L"Hair GPU Viewer Startup Failure",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    return app.Run();
}
