#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Preset
{
    std::wstring name;
    std::wstring description;
    std::map<std::wstring, std::wstring> env;
};

struct CsvStats
{
    bool hasData = false;
    size_t sampleCount = 0;
    double avgFps = 0.0;
    double avgFrameMs = 0.0;
    double p95FrameMs = 0.0;
    double worstFrameMs = 0.0;
    double avgGpuFrameMs = 0.0;
    double avgGpuSimMs = 0.0;
    double avgGpuHairMs = 0.0;
    double avgActiveSetMs = 0.0;
};

static std::wstring Quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

static std::wstring GetArgValue(int argc, wchar_t** argv, const std::wstring& key, const std::wstring& fallback = L"")
{
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (argv[i] == key)
        {
            return argv[i + 1];
        }
    }
    return fallback;
}

static std::wstring GetEnv(const std::wstring& name)
{
    DWORD size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (size == 0)
    {
        return L"";
    }
    std::wstring value(size, L'\0');
    GetEnvironmentVariableW(name.c_str(), value.data(), size);
    if (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }
    return value;
}

static uint32_t EnvUInt(const std::wstring& name, uint32_t fallback)
{
    const std::wstring value = GetEnv(name);
    if (value.empty())
    {
        return fallback;
    }
    try
    {
        return static_cast<uint32_t>(std::stoul(value));
    }
    catch (...)
    {
        return fallback;
    }
}

static void SetEnv(const std::wstring& name, const std::wstring& value)
{
    if (value.empty())
    {
        SetEnvironmentVariableW(name.c_str(), nullptr);
    }
    else
    {
        SetEnvironmentVariableW(name.c_str(), value.c_str());
    }
}

static std::wstring NowStamp()
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%04u%02u%02u_%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

static std::string NarrowAscii(const std::wstring& value)
{
    std::string out;
    out.reserve(value.size());
    for (wchar_t c : value)
    {
        out.push_back(c < 128 ? static_cast<char>(c) : '_');
    }
    return out;
}

static std::vector<std::string> SplitCsvLine(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string token;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (c == '"')
        {
            inQuotes = !inQuotes;
        }
        else if (c == ',' && !inQuotes)
        {
            tokens.push_back(token);
            token.clear();
        }
        else
        {
            token.push_back(c);
        }
    }
    tokens.push_back(token);
    return tokens;
}

static double ToDouble(const std::string& text, double fallback = 0.0)
{
    try
    {
        return std::stod(text);
    }
    catch (...)
    {
        return fallback;
    }
}

static CsvStats AnalyzeFramesCsv(const fs::path& csvPath)
{
    CsvStats stats = {};
    std::ifstream in(csvPath);
    if (!in)
    {
        return stats;
    }

    std::string headerLine;
    if (!std::getline(in, headerLine))
    {
        return stats;
    }
    const std::vector<std::string> headers = SplitCsvLine(headerLine);
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < headers.size(); ++i)
    {
        index[headers[i]] = i;
    }

    auto column = [&](const std::vector<std::string>& row, const std::string& name) -> double
    {
        auto it = index.find(name);
        if (it == index.end() || it->second >= row.size())
        {
            return 0.0;
        }
        return ToDouble(row[it->second]);
    };

    std::vector<double> frames;
    double fpsSum = 0.0;
    double frameSum = 0.0;
    double gpuFrameSum = 0.0;
    double gpuSimSum = 0.0;
    double gpuHairSum = 0.0;
    double activeSetSum = 0.0;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        const std::vector<std::string> row = SplitCsvLine(line);
        const double frameMs = column(row, "frameMilliseconds");
        frames.push_back(frameMs);
        frameSum += frameMs;
        fpsSum += column(row, "fps");
        gpuFrameSum += column(row, "gpuFrameMilliseconds");
        gpuSimSum += column(row, "gpuSimulationMilliseconds");
        gpuHairSum += column(row, "gpuHairDrawMilliseconds");
        activeSetSum += column(row, "activeSetEvaluationMilliseconds");
    }

    if (frames.empty())
    {
        return stats;
    }

    std::sort(frames.begin(), frames.end());
    const size_t n = frames.size();
    const size_t p95Index = std::min(n - 1u, static_cast<size_t>(std::ceil(static_cast<double>(n) * 0.95)) - 1u);
    stats.hasData = true;
    stats.sampleCount = n;
    stats.avgFrameMs = frameSum / static_cast<double>(n);
    stats.avgFps = fpsSum / static_cast<double>(n);
    stats.p95FrameMs = frames[p95Index];
    stats.worstFrameMs = frames.back();
    stats.avgGpuFrameMs = gpuFrameSum / static_cast<double>(n);
    stats.avgGpuSimMs = gpuSimSum / static_cast<double>(n);
    stats.avgGpuHairMs = gpuHairSum / static_cast<double>(n);
    stats.avgActiveSetMs = activeSetSum / static_cast<double>(n);
    return stats;
}

static bool RunViewerPreset(const fs::path& exe,
                            const fs::path& presetDir,
                            const Preset& preset,
                            uint32_t warmupFrames,
                            uint32_t measureFrames,
                            uint32_t timeoutSeconds)
{
    fs::create_directories(presetDir);
    {
        std::wofstream started(presetDir / L"RUN_STARTED.txt");
        started << L"preset=" << preset.name << L"\n";
        started << L"description=" << preset.description << L"\n";
        started << L"exe=" << exe.wstring() << L"\n";
        started << L"warmup=" << warmupFrames << L"\n";
        started << L"measure=" << measureFrames << L"\n";
    }

    std::vector<std::wstring> allKeys = {
        L"HAIRGPU_BENCHMARK", L"HAIRGPU_BENCHMARK_WARMUP", L"HAIRGPU_BENCHMARK_FRAMES",
        L"HAIRGPU_AUTO_TELEMETRY", L"HAIRGPU_TELEMETRY_DIR", L"HAIRGPU_TELEMETRY_RUN_NAME",
        L"HAIRGPU_CPU_READBACK", L"HAIRGPU_SELF_COLLISION", L"HAIRGPU_GRID_SELF_COLLISION",
        L"HAIRGPU_SOLVER_ITERATIONS", L"HAIRGPU_DEBUG_DRAW", L"HAIRGPU_RENDER_MODE",
        L"HAIRGPU_SHOW_SCALP", L"HAIRGPU_FAST_VIEWPORT", L"HAIRGPU_ACTIVESET_RUNTIME"
    };
    for (const std::wstring& key : allKeys)
    {
        SetEnv(key, L"");
    }

    SetEnv(L"HAIRGPU_BENCHMARK", L"1");
    SetEnv(L"HAIRGPU_BENCHMARK_WARMUP", std::to_wstring(warmupFrames));
    SetEnv(L"HAIRGPU_BENCHMARK_FRAMES", std::to_wstring(measureFrames));
    SetEnv(L"HAIRGPU_AUTO_TELEMETRY", L"1");
    SetEnv(L"HAIRGPU_TELEMETRY_DIR", presetDir.wstring());
    SetEnv(L"HAIRGPU_TELEMETRY_RUN_NAME", preset.name);

    for (const auto& kv : preset.env)
    {
        SetEnv(kv.first, kv.second);
    }

    {
        std::wofstream envFile(presetDir / L"env.txt");
        for (const std::wstring& key : allKeys)
        {
            envFile << key << L"=" << GetEnv(key) << L"\n";
        }
    }

    if (!fs::exists(exe))
    {
        std::wofstream status(presetDir / L"process_status.txt");
        status << L"status=missing_executable\n";
        return false;
    }

    std::wstring commandLine = Quote(exe.wstring());
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    const fs::path workingDir = exe.parent_path();
    BOOL ok = CreateProcessW(nullptr,
                             commandLine.data(),
                             nullptr,
                             nullptr,
                             FALSE,
                             CREATE_NO_WINDOW,
                             nullptr,
                             workingDir.wstring().c_str(),
                             &si,
                             &pi);
    if (!ok)
    {
        const DWORD err = GetLastError();
        std::wofstream status(presetDir / L"process_status.txt");
        status << L"status=create_process_failed\n";
        status << L"win32_error=" << err << L"\n";
        return false;
    }

    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutSeconds * 1000u);
    DWORD exitCode = 0;
    bool timedOut = false;
    if (wait == WAIT_TIMEOUT)
    {
        timedOut = true;
        TerminateProcess(pi.hProcess, 0xFFFF0001u);
        WaitForSingleObject(pi.hProcess, 5000u);
    }
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::wofstream status(presetDir / L"process_status.txt");
    status << L"status=" << (timedOut ? L"timeout_killed" : L"finished") << L"\n";
    status << L"exit_code=" << exitCode << L"\n";
    status << L"timeout_seconds=" << timeoutSeconds << L"\n";
    return !timedOut;
}

static void EnsureFallbackTelemetry(const fs::path& presetDir, const std::wstring& name)
{
    const fs::path csv = presetDir / (name + L"_frames.csv");
    if (!fs::exists(csv))
    {
        std::ofstream out(csv);
        out << "sampleIndex,elapsedSeconds,frameMilliseconds,simulationMilliseconds,gpuFrameMilliseconds,gpuSimulationMilliseconds,gpuHairDrawMilliseconds,gpuDebugDrawMilliseconds,fps\n";
    }
    const fs::path json = presetDir / (name + L"_summary.json");
    if (!fs::exists(json))
    {
        std::ofstream out(json);
        out << "{\n  \"status\": \"telemetry_missing\",\n  \"note\": \"Viewer did not export telemetry. Check process_status.txt and RUN_STARTED.txt.\"\n}\n";
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (GetEnv(L"HAIRGPU_DISABLE_AUTO_PROFILE_POST_BUILD") == L"1")
    {
        std::wcout << L"[HairGPU AutoProfile] Skipped by HAIRGPU_DISABLE_AUTO_PROFILE_POST_BUILD=1\n";
        return 0;
    }

    const fs::path exe = GetArgValue(argc, argv, L"--exe");
    fs::path sourceDir = GetArgValue(argc, argv, L"--source-dir");
    const std::wstring configuration = GetArgValue(argc, argv, L"--configuration", L"Unknown");
    const std::wstring platform = GetArgValue(argc, argv, L"--platform", L"Windows");
    if (sourceDir.empty())
    {
        sourceDir = fs::current_path();
    }

    const uint32_t warmupFrames = EnvUInt(L"HAIRGPU_PROFILE_WARMUP_FRAMES", 30u);
    const uint32_t measureFrames = EnvUInt(L"HAIRGPU_PROFILE_MEASURE_FRAMES", 180u);
    const uint32_t timeoutSeconds = EnvUInt(L"HAIRGPU_PROFILE_TIMEOUT_SECONDS", 90u);

    std::vector<Preset> presets = {
        { L"00_current", L"Current viewer defaults.", {} },
        { L"01_no_readback", L"Disable CPU readback.", {{L"HAIRGPU_CPU_READBACK", L"off"}} },
        { L"02_no_self_collision", L"Disable self collision.", {{L"HAIRGPU_SELF_COLLISION", L"0"}, {L"HAIRGPU_GRID_SELF_COLLISION", L"0"}} },
        { L"03_low_solver", L"Use 3 solver iterations.", {{L"HAIRGPU_SOLVER_ITERATIONS", L"3"}} },
        { L"04_no_debug_draw", L"Disable debug draw.", {{L"HAIRGPU_DEBUG_DRAW", L"0"}} },
        { L"05_render_light", L"Use lighter render settings.", {{L"HAIRGPU_RENDER_MODE", L"billboard"}, {L"HAIRGPU_SHOW_SCALP", L"0"}, {L"HAIRGPU_FAST_VIEWPORT", L"1"}} }
    };

    const fs::path resultRoot = sourceDir / L"profile_results" / (NowStamp() + L"_" + configuration + L"_" + platform);
    fs::create_directories(resultRoot);
    fs::create_directories(sourceDir / L"profile_results");
    {
        std::wofstream latest(sourceDir / L"profile_results" / L"LATEST.txt");
        latest << resultRoot.wstring() << L"\n";
    }
    {
        std::wofstream started(resultRoot / L"AUTO_PROFILE_STARTED.txt");
        started << L"runner=HairGpuAutoProfileRunner.exe\n";
        started << L"exe=" << exe.wstring() << L"\n";
        started << L"sourceDir=" << sourceDir.wstring() << L"\n";
        started << L"configuration=" << configuration << L"\n";
        started << L"platform=" << platform << L"\n";
        started << L"warmup=" << warmupFrames << L"\n";
        started << L"measure=" << measureFrames << L"\n";
        started << L"timeoutSeconds=" << timeoutSeconds << L"\n";
    }

    std::map<std::wstring, CsvStats> results;
    for (const Preset& preset : presets)
    {
        const fs::path presetDir = resultRoot / preset.name;
        std::wcout << L"[HairGPU AutoProfile] Running " << preset.name << L"\n";
        RunViewerPreset(exe, presetDir, preset, warmupFrames, measureFrames, timeoutSeconds);
        EnsureFallbackTelemetry(presetDir, preset.name);
        results[preset.name] = AnalyzeFramesCsv(presetDir / (preset.name + L"_frames.csv"));
    }

    {
        std::ofstream csv(resultRoot / L"RESULT.csv");
        csv << "preset,samples,avg_fps,avg_frame_ms,p95_frame_ms,worst_frame_ms,avg_gpu_frame_ms,avg_gpu_sim_ms,avg_gpu_hair_ms,avg_active_set_ms\n";
        for (const Preset& preset : presets)
        {
            const CsvStats& s = results[preset.name];
            csv << NarrowAscii(preset.name) << ',' << s.sampleCount << ',' << s.avgFps << ',' << s.avgFrameMs << ',' << s.p95FrameMs << ',' << s.worstFrameMs << ',' << s.avgGpuFrameMs << ',' << s.avgGpuSimMs << ',' << s.avgGpuHairMs << ',' << s.avgActiveSetMs << "\n";
        }
    }

    {
        std::ofstream md(resultRoot / L"RESULT.md");
        md << "# HairGPU Auto Profile Result\n\n";
        md << "This result was generated by the native C++ post-build runner. No PowerShell script execution is required.\n\n";
        md << "Result root: `" << resultRoot.string() << "`\n\n";
        md << "| Preset | Samples | Avg FPS | Avg Frame ms | P95 Frame ms | Worst Frame ms | GPU Frame ms | GPU Sim ms | GPU Hair ms | ActiveSet ms |\n";
        md << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
        for (const Preset& preset : presets)
        {
            const CsvStats& s = results[preset.name];
            md << "| " << NarrowAscii(preset.name) << " | " << s.sampleCount << " | "
               << std::fixed << std::setprecision(2) << s.avgFps << " | " << s.avgFrameMs << " | " << s.p95FrameMs << " | " << s.worstFrameMs << " | "
               << s.avgGpuFrameMs << " | " << s.avgGpuSimMs << " | " << s.avgGpuHairMs << " | " << s.avgActiveSetMs << " |\n";
        }

        const auto currentIt = results.find(L"00_current");
        if (currentIt != results.end() && currentIt->second.hasData)
        {
            const double current = currentIt->second.avgFrameMs;
            md << "\n## Bottleneck guess\n\n";
            auto compare = [&](const wchar_t* presetName, const char* label)
            {
                auto it = results.find(presetName);
                if (it == results.end() || !it->second.hasData)
                {
                    md << "- " << label << ": no telemetry.\n";
                    return;
                }
                const double diff = current - it->second.avgFrameMs;
                md << "- " << label << ": " << std::fixed << std::setprecision(2) << diff << " ms improvement vs current";
                if (diff >= 20.0) md << " => primary bottleneck candidate";
                else if (diff >= 10.0) md << " => strong bottleneck candidate";
                else if (diff >= 5.0) md << " => possible bottleneck";
                md << "\n";
            };
            compare(L"01_no_readback", "CPU/GPU readback");
            compare(L"02_no_self_collision", "self collision");
            compare(L"03_low_solver", "solver iterations");
            compare(L"05_render_light", "rendering/overdraw");
        }
        else
        {
            md << "\n## Bottleneck guess\n\n";
            md << "No valid current telemetry was exported. Check each preset's `process_status.txt`. The folders were still created successfully.\n";
        }
    }

    std::wcout << L"[HairGPU AutoProfile] Complete: " << resultRoot.wstring() << L"\n";
    return 0;
}
