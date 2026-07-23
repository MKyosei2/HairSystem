#include "GpuHairViewer.h"

#include "hairgpu/AssetIO.h"
#include "hairgpu/Types.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
    struct LocalDebugCaptureOptions
    {
        bool enabled = false;
        bool autoCommit = false;
        bool push = false;
        uint32_t warmupFrames = 120u;
        uint32_t captureFrames = 1800u;
        std::wstring methodCode = L"DEV";
        std::wstring runName = L"local_debug";
        std::filesystem::path repositoryRoot;
        std::filesystem::path runDirectory;
    };

    void SetEnvironmentValue(const wchar_t* name, const std::wstring& value)
    {
        if (name != nullptr)
        {
            SetEnvironmentVariableW(name, value.c_str());
        }
    }

    uint32_t ParsePositiveUInt(const wchar_t* value, uint32_t fallback)
    {
        if (value == nullptr || *value == L'\0')
        {
            return fallback;
        }
        try
        {
            const unsigned long parsed = std::stoul(value);
            return parsed > 0ul ? static_cast<uint32_t>(parsed) : fallback;
        }
        catch (...)
        {
            return fallback;
        }
    }

    std::filesystem::path FindRepositoryRoot(std::filesystem::path start)
    {
        std::error_code ec;
        start = std::filesystem::absolute(start, ec);
        if (ec)
        {
            return {};
        }
        for (uint32_t depth = 0u; depth < 10u; ++depth)
        {
            if (std::filesystem::is_regular_file(start / L"HairSystem.sln", ec) &&
                std::filesystem::exists(start / L".git", ec))
            {
                return start;
            }
            const std::filesystem::path parent = start.parent_path();
            if (parent.empty() || parent == start)
            {
                break;
            }
            start = parent;
        }
        return {};
    }

    std::wstring BuildUtcRunDirectoryName()
    {
        SYSTEMTIME time = {};
        GetSystemTime(&time);
        std::wstringstream stream;
        stream << std::setfill(L'0')
               << std::setw(4) << time.wYear
               << std::setw(2) << time.wMonth
               << std::setw(2) << time.wDay
               << L'T'
               << std::setw(2) << time.wHour
               << std::setw(2) << time.wMinute
               << std::setw(2) << time.wSecond
               << L'Z'
               << L'_' << GetCurrentProcessId();
        return stream.str();
    }

    std::string JsonEscapeText(const std::string& value)
    {
        std::ostringstream stream;
        for (unsigned char ch : value)
        {
            switch (ch)
            {
            case '\\': stream << "\\\\"; break;
            case '"': stream << "\\\""; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (ch < 0x20u)
                {
                    stream << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<unsigned int>(ch)
                           << std::dec;
                }
                else
                {
                    stream << static_cast<char>(ch);
                }
                break;
            }
        }
        return stream.str();
    }

    bool WriteCaptureContext(
        const LocalDebugCaptureOptions& options,
        const std::filesystem::path& executablePath)
    {
        std::ofstream output(options.runDirectory / L"capture_context.json", std::ios::binary);
        if (!output)
        {
            return false;
        }
    #if defined(_DEBUG)
        constexpr const char* buildConfiguration = "Debug";
    #else
        constexpr const char* buildConfiguration = "Release";
    #endif
        output << "{\n"
               << "  \"schemaVersion\": \"hairgpu-local-debug-context-v1\",\n"
               << "  \"evidenceClass\": \"diagnostic_debugger\",\n"
               << "  \"formalEvidence\": false,\n"
               << "  \"launchMode\": \"visual_studio_local_debugger\",\n"
               << "  \"debuggerAttached\": " << (IsDebuggerPresent() ? "true" : "false") << ",\n"
               << "  \"buildConfiguration\": \"" << buildConfiguration << "\",\n"
               << "  \"platform\": \"x64\",\n"
               << "  \"warmupFrames\": " << options.warmupFrames << ",\n"
               << "  \"captureFrames\": " << options.captureFrames << ",\n"
               << "  \"methodCode\": \"" << JsonEscapeText(std::filesystem::path(options.methodCode).u8string()) << "\",\n"
               << "  \"guideCount\": " << kGuideCount << ",\n"
               << "  \"followersPerGuide\": " << kFollowersPerGuide << ",\n"
               << "  \"displayStrandCount\": " << kDisplayStrandCount << ",\n"
               << "  \"particlesPerStrand\": " << kParticlesPerStrand << ",\n"
               << "  \"solverIterations\": " << kSolverIterations << ",\n"
               << "  \"repositoryRoot\": \"" << JsonEscapeText(options.repositoryRoot.u8string()) << "\",\n"
               << "  \"runDirectory\": \"" << JsonEscapeText(options.runDirectory.u8string()) << "\",\n"
               << "  \"executable\": \"" << JsonEscapeText(executablePath.u8string()) << "\",\n"
               << "  \"automaticCommitRequested\": " << (options.autoCommit ? "true" : "false") << ",\n"
               << "  \"automaticPushRequested\": " << (options.push ? "true" : "false") << ",\n"
               << "  \"warning\": \"Debugger data is diagnostic and must not be used as formal paper evidence.\"\n"
               << "}\n";
        return output.good();
    }

    std::wstring QuoteCommandLineArgument(const std::wstring& value)
    {
        std::wstring quoted = L"\"";
        size_t backslashCount = 0u;
        for (wchar_t ch : value)
        {
            if (ch == L'\\')
            {
                ++backslashCount;
                continue;
            }
            if (ch == L'"')
            {
                quoted.append(backslashCount * 2u + 1u, L'\\');
                quoted.push_back(L'"');
                backslashCount = 0u;
                continue;
            }
            quoted.append(backslashCount, L'\\');
            backslashCount = 0u;
            quoted.push_back(ch);
        }
        quoted.append(backslashCount * 2u, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    bool LaunchAndWait(
        const std::wstring& commandLine,
        const std::filesystem::path& workingDirectory,
        DWORD& exitCode)
    {
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');
        STARTUPINFOW startupInfo = {};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo = {};
        const BOOL created = CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.c_str(),
            &startupInfo,
            &processInfo);
        if (!created)
        {
            exitCode = GetLastError();
            return false;
        }
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD childExitCode = 1u;
        GetExitCodeProcess(processInfo.hProcess, &childExitCode);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        exitCode = childExitCode;
        return true;
    }

    DWORD RunLocalDebugPostprocessor(
        const LocalDebugCaptureOptions& options,
        int viewerExitCode)
    {
        const std::filesystem::path script =
            options.repositoryRoot / L"tools" / L"final_stage" / L"hairgpu_local_debug_report.py";
        if (!std::filesystem::is_regular_file(script))
        {
            std::ofstream error(options.runDirectory / L"POSTPROCESS_LAUNCH_ERROR.txt");
            error << "Postprocessor was not found: " << script.u8string() << "\n";
            return ERROR_FILE_NOT_FOUND;
        }

        std::wstring arguments =
            L" " + QuoteCommandLineArgument(script.wstring()) +
            L" --repo-root " + QuoteCommandLineArgument(options.repositoryRoot.wstring()) +
            L" --run-dir " + QuoteCommandLineArgument(options.runDirectory.wstring()) +
            L" --run-name " + QuoteCommandLineArgument(options.runName) +
            L" --viewer-exit-code " + std::to_wstring(viewerExitCode);
        if (options.autoCommit)
        {
            arguments += L" --auto-commit";
        }
        if (options.push)
        {
            arguments += L" --push";
        }

        DWORD exitCode = 1u;
        if (LaunchAndWait(L"python.exe" + arguments, options.repositoryRoot, exitCode))
        {
            return exitCode;
        }
        const DWORD pythonError = exitCode;
        if (LaunchAndWait(L"py.exe -3" + arguments, options.repositoryRoot, exitCode))
        {
            return exitCode;
        }

        std::ofstream error(options.runDirectory / L"POSTPROCESS_LAUNCH_ERROR.txt");
        error << "Failed to start python.exe or py.exe. Win32 errors: "
              << pythonError << ", " << exitCode << "\n";
        return exitCode;
    }

    bool ConfigureCommandLineAndLocalCapture(
        const std::filesystem::path& executablePath,
        LocalDebugCaptureOptions& options)
    {
        bool explicitlyDisabled = false;
        for (int index = 1; index < __argc; ++index)
        {
            const std::wstring argument = __wargv[index] ? __wargv[index] : L"";
            auto nextValue = [&]() -> const wchar_t*
            {
                return (index + 1 < __argc && __wargv[index + 1]) ? __wargv[++index] : L"";
            };

            if (argument == L"--start-paused")
                SetEnvironmentValue(L"HAIRGPU_START_PAUSED", L"1");
            else if (argument == L"--visual-validation")
                SetEnvironmentValue(L"HAIRGPU_VISUAL_VALIDATION", L"1");
            else if (argument == L"--experiment-config")
                SetEnvironmentValue(L"HAIRGPU_CASE_JSON", nextValue());
            else if (argument == L"--max-frames")
                SetEnvironmentValue(L"HAIRGPU_MAX_FRAMES", nextValue());
            else if (argument == L"--render-mode")
                SetEnvironmentValue(L"HAIRGPU_RENDER_MODE", nextValue());
            else if (argument == L"--self-collision")
                SetEnvironmentValue(L"HAIRGPU_SELF_COLLISION", nextValue());
            else if (argument == L"--guides-only")
                SetEnvironmentValue(L"HAIRGPU_GUIDES_ONLY", nextValue());
            else if (argument == L"--local-capture")
            {
                options.enabled = true;
                explicitlyDisabled = false;
            }
            else if (argument == L"--no-local-capture")
            {
                options.enabled = false;
                explicitlyDisabled = true;
            }
            else if (argument == L"--local-capture-warmup")
                options.warmupFrames = ParsePositiveUInt(nextValue(), options.warmupFrames);
            else if (argument == L"--local-capture-frames")
                options.captureFrames = ParsePositiveUInt(nextValue(), options.captureFrames);
            else if (argument == L"--local-capture-method")
                options.methodCode = nextValue();
            else if (argument == L"--local-capture-autocommit")
                options.autoCommit = true;
            else if (argument == L"--local-capture-push")
            {
                options.autoCommit = true;
                options.push = true;
            }
        }

        if (!options.enabled || explicitlyDisabled)
        {
            return true;
        }

        options.repositoryRoot = FindRepositoryRoot(executablePath.parent_path());
        if (options.repositoryRoot.empty())
        {
            return false;
        }
        options.runDirectory =
            options.repositoryRoot / L"results" / L"committed" / L"local_debug" /
            BuildUtcRunDirectoryName();
        std::error_code ec;
        std::filesystem::create_directories(options.runDirectory, ec);
        if (ec)
        {
            return false;
        }

        SetEnvironmentValue(L"HAIRGPU_LOCAL_DEBUG_CAPTURE", L"1");
        SetEnvironmentValue(L"HAIRGPU_LOCAL_DEBUG_WARMUP_FRAMES", std::to_wstring(options.warmupFrames));
        SetEnvironmentValue(L"HAIRGPU_LOCAL_DEBUG_CAPTURE_FRAMES", std::to_wstring(options.captureFrames));
        SetEnvironmentValue(L"HAIRGPU_LOCAL_DEBUG_METHOD", options.methodCode);
        SetEnvironmentValue(L"HAIRGPU_LOCAL_DEBUG_RUN_NAME", options.runName);
        SetEnvironmentValue(
            L"HAIRGPU_MAX_FRAMES",
            std::to_wstring(options.warmupFrames + options.captureFrames));
        SetEnvironmentValue(L"HAIRGPU_AUTO_TELEMETRY", L"1");
        SetEnvironmentValue(L"HAIRGPU_TELEMETRY_DIR", options.runDirectory.wstring());
        SetEnvironmentValue(L"HAIRGPU_TELEMETRY_RUN_NAME", options.runName);
        SetEnvironmentValue(L"HAIRGPU_LOCAL_DEBUG_EVIDENCE_CLASS", L"diagnostic_debugger");
        return WriteCaptureContext(options, executablePath);
    }


}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path executablePath(modulePath);
    const std::filesystem::path exeDir = executablePath.parent_path();

    LocalDebugCaptureOptions localCapture;
    if (!ConfigureCommandLineAndLocalCapture(executablePath, localCapture))
    {
        MessageBoxW(
            nullptr,
            L"Local debugger capture could not locate the repository or create its output directory.",
            L"HairSystem Local Capture Failure",
            MB_OK | MB_ICONERROR);
        return 81;
    }

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

    const int viewerExitCode = app.Run();
    if (!localCapture.enabled)
    {
        return viewerExitCode;
    }

    if (!app.ExportLocalDebugCapture(localCapture.runDirectory, localCapture.runName))
    {
        std::ofstream error(localCapture.runDirectory / L"TELEMETRY_EXPORT_ERROR.txt");
        error << "Viewer failed to export local debugger telemetry.\n";
        return viewerExitCode == 0 ? 89 : viewerExitCode;
    }

    const DWORD postprocessExitCode =
        RunLocalDebugPostprocessor(localCapture, viewerExitCode);
    if (postprocessExitCode != 0u && viewerExitCode == 0)
    {
        return 90;
    }
    return viewerExitCode;
}
