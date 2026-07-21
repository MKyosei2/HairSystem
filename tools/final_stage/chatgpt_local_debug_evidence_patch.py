from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOUCHED: list[str] = []


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")
    TOUCHED.append(path)


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    if old not in text:
        raise RuntimeError(f"anchor not found in {path}: {old[:180]!r}")
    write(path, text.replace(old, new, 1))


# -----------------------------------------------------------------------------
# 1. Commit only explicitly approved diagnostic evidence directories.
# -----------------------------------------------------------------------------
path = ".gitignore"
text = read(path)
block = r'''

# Visual Studio local-debugger diagnostic evidence.
# These files are intentionally versioned and are always marked formalEvidence=false.
!results/
!results/committed/
!results/committed/local_debug/
!results/committed/local_debug/**
'''
if "!results/committed/local_debug/**" not in text:
    text = text.rstrip() + block + "\n"
write(path, text)


# -----------------------------------------------------------------------------
# 2. Visual Studio Release x64 F5 launches a bounded automatic diagnostic run.
# -----------------------------------------------------------------------------
path = "HairSystem.vcxproj"
text = read(path)
property_block = r'''  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <LocalDebuggerWorkingDirectory>$(SolutionDir)</LocalDebuggerWorkingDirectory>
    <LocalDebuggerCommandArguments>--local-capture --local-capture-warmup 120 --local-capture-frames 1800 --local-capture-autocommit --local-capture-push</LocalDebuggerCommandArguments>
  </PropertyGroup>
'''
if "--local-capture-warmup" not in text:
    anchor = "  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">\n"
    if anchor not in text:
        raise RuntimeError("HairSystem.vcxproj ItemDefinitionGroup anchor missing")
    text = text.replace(anchor, property_block + anchor, 1)
write(path, text)


# -----------------------------------------------------------------------------
# 3. AppMain: parse P0 diagnostic flags, create a unique result directory,
#    configure Viewer telemetry, then post-process/commit/push after app.Run().
# -----------------------------------------------------------------------------
path = "apps/dx11_viewer/src/AppMain.cpp"
text = read(path)
for include_anchor, include_text in (
    ("#include <algorithm>\n", "#include <algorithm>\n#include <chrono>\n"),
    ("#include <cstdint>\n", "#include <cstdint>\n#include <cstdlib>\n"),
    ("#include <fstream>\n", "#include <fstream>\n#include <iomanip>\n"),
):
    if include_text.splitlines()[-1] not in text:
        if include_anchor not in text:
            raise RuntimeError(f"AppMain include anchor missing: {include_anchor!r}")
        text = text.replace(include_anchor, include_text, 1)

helper = r'''
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
               << "  \\\"schemaVersion\\\": \\\"hairgpu-local-debug-context-v1\\\",\n"
               << "  \\\"evidenceClass\\\": \\\"diagnostic_debugger\\\",\n"
               << "  \\\"formalEvidence\\\": false,\n"
               << "  \\\"launchMode\\\": \\\"visual_studio_local_debugger\\\",\n"
               << "  \\\"debuggerAttached\\\": " << (IsDebuggerPresent() ? "true" : "false") << ",\n"
               << "  \\\"buildConfiguration\\\": \\\"" << buildConfiguration << "\\\",\n"
               << "  \\\"platform\\\": \\\"x64\\\",\n"
               << "  \\\"warmupFrames\\\": " << options.warmupFrames << ",\n"
               << "  \\\"captureFrames\\\": " << options.captureFrames << ",\n"
               << "  \\\"methodCode\\\": \\\"" << JsonEscapeText(std::filesystem::path(options.methodCode).u8string()) << "\\\",\n"
               << "  \\\"guideCount\\\": " << kGuideCount << ",\n"
               << "  \\\"followersPerGuide\\\": " << kFollowersPerGuide << ",\n"
               << "  \\\"displayStrandCount\\\": " << kDisplayStrandCount << ",\n"
               << "  \\\"particlesPerStrand\\\": " << kParticlesPerStrand << ",\n"
               << "  \\\"solverIterations\\\": " << kSolverIterations << ",\n"
               << "  \\\"repositoryRoot\\\": \\\"" << JsonEscapeText(options.repositoryRoot.u8string()) << "\\\",\n"
               << "  \\\"runDirectory\\\": \\\"" << JsonEscapeText(options.runDirectory.u8string()) << "\\\",\n"
               << "  \\\"executable\\\": \\\"" << JsonEscapeText(executablePath.u8string()) << "\\\",\n"
               << "  \\\"automaticCommitRequested\\\": " << (options.autoCommit ? "true" : "false") << ",\n"
               << "  \\\"automaticPushRequested\\\": " << (options.push ? "true" : "false") << ",\n"
               << "  \\\"warning\\\": \\\"Debugger data is diagnostic and must not be used as formal paper evidence.\\\"\n"
               << "}\n";
        return output.good();
    }

    std::wstring QuoteCommandLineArgument(const std::wstring& value)
    {
        std::wstring quoted = L"\\\"";
        size_t backslashCount = 0u;
        for (wchar_t ch : value)
        {
            if (ch == L'\\\\')
            {
                ++backslashCount;
                continue;
            }
            if (ch == L'\\\"')
            {
                quoted.append(backslashCount * 2u + 1u, L'\\\\');
                quoted.push_back(L'\\\"');
                backslashCount = 0u;
                continue;
            }
            quoted.append(backslashCount, L'\\\\');
            backslashCount = 0u;
            quoted.push_back(ch);
        }
        quoted.append(backslashCount * 2u, L'\\\\');
        quoted.push_back(L'\\\"');
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
        SetEnvironmentValue(L"HAIRGPU_AUTO_TELEMETRY", L"1");
        SetEnvironmentValue(L"HAIRGPU_TELEMETRY_DIR", options.runDirectory.wstring());
        SetEnvironmentValue(L"HAIRGPU_TELEMETRY_RUN_NAME", options.runName);
        SetEnvironmentValue(L"HAIRGPU_LOCAL_DEBUG_EVIDENCE_CLASS", L"diagnostic_debugger");
        return WriteCaptureContext(options, executablePath);
    }

'''
if "struct LocalDebugCaptureOptions" not in text:
    anchor = "\n}\n\nint WINAPI wWinMain"
    if anchor not in text:
        raise RuntimeError("AppMain anonymous namespace closing anchor missing")
    text = text.replace(anchor, helper + "\n}\n\nint WINAPI wWinMain", 1)

main_start = text.find("int WINAPI wWinMain")
if main_start < 0:
    raise RuntimeError("AppMain wWinMain missing")
new_main = r'''int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
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

    const DWORD postprocessExitCode =
        RunLocalDebugPostprocessor(localCapture, viewerExitCode);
    if (postprocessExitCode != 0u && viewerExitCode == 0)
    {
        return 90;
    }
    return viewerExitCode;
}
'''
text = text[:main_start] + new_main
write(path, text)


# -----------------------------------------------------------------------------
# 4. Viewer run loop: local capture is bounded, fixed-step, full GPU-timing,
#    diagnostic only, and cannot become a formally valid run.
# -----------------------------------------------------------------------------
path = "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"
text = read(path)
anchor = r'''        m_globalFrameIndex = 0u;
        m_measuredFrameIndex = 0u;
'''
local_setup = anchor + r'''        const bool localDebugCaptureEnabled =
            EnvironmentValueIsTrue(GetEnvironmentVariableString(L"HAIRGPU_LOCAL_DEBUG_CAPTURE"));
        const uint32_t localDebugWarmupFrames = localDebugCaptureEnabled
            ? EnvironmentValueToUInt(
                GetEnvironmentVariableString(L"HAIRGPU_LOCAL_DEBUG_WARMUP_FRAMES"),
                120u)
            : 0u;
        const uint32_t localDebugCaptureFrames = localDebugCaptureEnabled
            ? std::max<uint32_t>(EnvironmentValueToUInt(
                GetEnvironmentVariableString(L"HAIRGPU_LOCAL_DEBUG_CAPTURE_FRAMES"),
                1800u), 1u)
            : 0u;
        if (localDebugCaptureEnabled)
        {
            std::wstring runName =
                GetEnvironmentVariableString(L"HAIRGPU_LOCAL_DEBUG_RUN_NAME");
            if (runName.empty())
            {
                runName = L"local_debug";
            }
            std::wstring methodCode =
                GetEnvironmentVariableString(L"HAIRGPU_LOCAL_DEBUG_METHOD");
            if (methodCode.empty())
            {
                methodCode = L"DEV";
            }
            // Keep enabled=false so a debugger run can never satisfy the formal gate.
            m_experimentCase.enabled = false;
            m_experimentCase.caseId = runName;
            m_experimentCase.variant = methodCode;
            m_experimentCase.scenario = L"S01_LocalDebug";
            m_experimentCase.seed = 1103u;
            m_experimentCase.warmupFrames = localDebugWarmupFrames;
            m_experimentCase.measurementFrames = localDebugCaptureFrames;
            m_experimentCase.fixedTimeStepEnabled = true;
            m_experimentCase.fixedTimeStepSeconds = m_simConfig.fixedTimeStep;
            m_experimentCase.realMeasurement = false;
            m_experimentCase.runStatus = ExperimentRunStatus::Incomplete;
            m_experimentRunStatus = ExperimentRunStatus::Incomplete;
            m_vsyncEnabled = false;
            m_presentSyncInterval = 0u;
            m_paused = false;
        }
'''
if "localDebugCaptureEnabled" not in text:
    if anchor not in text:
        raise RuntimeError("Viewer Run local setup anchor missing")
    text = text.replace(anchor, local_setup, 1)

old = r'''            const bool shouldIssueGpuTiming =
                IsExperimentModeEnabled() || ((m_globalFrameIndex % 16u) == 0u);
'''
new = r'''            const bool shouldIssueGpuTiming =
                IsExperimentModeEnabled() ||
                localDebugCaptureEnabled ||
                ((m_globalFrameIndex % 16u) == 0u);
'''
if old not in text:
    raise RuntimeError("GPU timing cadence anchor missing")
text = text.replace(old, new, 1)

old = r'''                if (IsExperimentModeEnabled())
                {
                    // Exactly one deterministic simulation tick per captured render frame.
                    ExecuteExperimentSimulationTick(m_simConfig.fixedTimeStep);
'''
new = r'''                if (IsExperimentModeEnabled() || localDebugCaptureEnabled)
                {
                    // Formal and local diagnostic captures both use exactly one fixed
                    // simulation tick per rendered frame. The debugger result remains
                    // diagnostic because m_experimentCase.enabled stays false.
                    ExecuteExperimentSimulationTick(m_simConfig.fixedTimeStep);
'''
if old not in text:
    raise RuntimeError("fixed-step local capture anchor missing")
text = text.replace(old, new, 1)

old = "            const bool captureCurrentFrame = ShouldCaptureCurrentFrame();\n"
new = r'''            const bool localDebugCaptureCurrentFrame =
                localDebugCaptureEnabled &&
                m_globalFrameIndex >= localDebugWarmupFrames &&
                m_performanceSamples.size() < static_cast<size_t>(localDebugCaptureFrames);
            const bool captureCurrentFrame =
                ShouldCaptureCurrentFrame() || localDebugCaptureCurrentFrame;
'''
if old not in text:
    raise RuntimeError("captureCurrentFrame anchor missing")
text = text.replace(old, new, 1)

old = r'''            ++m_globalFrameIndex;
            if (ShouldAutoExitAfterCurrentFrame())
'''
new = r'''            ++m_globalFrameIndex;
            if (localDebugCaptureEnabled &&
                m_performanceSamples.size() >= static_cast<size_t>(localDebugCaptureFrames))
            {
                m_running = false;
            }
            if (ShouldAutoExitAfterCurrentFrame())
'''
if old not in text:
    raise RuntimeError("local capture auto-exit anchor missing")
text = text.replace(old, new, 1)
write(path, text)


# -----------------------------------------------------------------------------
# 5. Reporter safety corrections: path validation, Git option order, and make
#    postprocess_status part of the committed artifact instead of modifying it after.
# -----------------------------------------------------------------------------
path = "tools/final_stage/hairgpu_local_debug_report.py"
text = read(path)
old = r'''    relative = run_dir.resolve().relative_to(repo_root.resolve()).as_posix()
    allowed_prefix = "results/committed/local_debug/"
    if not relative.startswith(allowed_prefix):
        raise RuntimeError(f"Refusing to commit outside {allowed_prefix}: {relative}")
'''
new = r'''    allowed_prefix = "results/committed/local_debug/"
    try:
        relative = run_dir.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError as exc:
        raise RuntimeError(
            f"Refusing to commit a directory outside the repository: {run_dir}"
        ) from exc
    if not relative.startswith(allowed_prefix):
        raise RuntimeError(f"Refusing to commit outside {allowed_prefix}: {relative}")
'''
if old not in text:
    raise RuntimeError("reporter path safety anchor missing")
text = text.replace(old, new, 1)

old = r'''    commit = _git(
        repo_root,
        "commit",
        "--only",
        "--",
        relative,
        "-m",
        f"data(local-debug): {run_name}",
    )
'''
new = r'''    commit = _git(
        repo_root,
        "commit",
        "--only",
        "-m",
        f"data(local-debug): {run_name}",
        "--",
        relative,
    )
'''
if old not in text:
    raise RuntimeError("reporter commit argument anchor missing")
text = text.replace(old, new, 1)

old = r'''    try:
        build_report(inputs)
        commit_result: dict[str, Any] = {
            "requested": inputs.auto_commit,
            "pushRequested": inputs.push,
            "committed": False,
            "pushed": False,
        }
        if inputs.auto_commit:
            commit_result.update(_auto_commit(
                inputs.repo_root,
                inputs.run_dir,
                inputs.run_name,
                inputs.push,
            ))
        _write_json(inputs.run_dir / "postprocess_status.json", {
            "schemaVersion": SCHEMA_VERSION,
            "success": True,
            "commit": commit_result,
        })
        return 0
'''
new = r'''    try:
        build_report(inputs)
        # Write the status before committing so the committed run directory is
        # self-contained. The resulting commit SHA can be queried from Git history.
        commit_result: dict[str, Any] = {
            "requested": inputs.auto_commit,
            "pushRequested": inputs.push,
            "state": "pending" if inputs.auto_commit else "not_requested",
        }
        _write_json(inputs.run_dir / "postprocess_status.json", {
            "schemaVersion": SCHEMA_VERSION,
            "success": True,
            "commit": commit_result,
        })
        if inputs.auto_commit:
            result = _auto_commit(
                inputs.repo_root,
                inputs.run_dir,
                inputs.run_name,
                inputs.push,
            )
            print(json.dumps(result, ensure_ascii=False))
        return 0
'''
if old not in text:
    raise RuntimeError("reporter main commit anchor missing")
text = text.replace(old, new, 1)
write(path, text)


# -----------------------------------------------------------------------------
# 6. Unit-test loader compatibility for Python dataclasses.
# -----------------------------------------------------------------------------
path = "tests/final_stage/test_hairgpu_local_debug_report.py"
text = read(path)
if "import sys\n" not in text:
    text = text.replace("import json\n", "import json\nimport sys\n", 1)
old = r'''reporter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(reporter)
'''
new = r'''reporter = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = reporter
SPEC.loader.exec_module(reporter)
'''
if old not in text:
    raise RuntimeError("unit test module loader anchor missing")
text = text.replace(old, new, 1)
write(path, text)


EXPECTED = [
    ".gitignore",
    "HairSystem.vcxproj",
    "apps/dx11_viewer/src/AppMain.cpp",
    "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl",
    "tools/final_stage/hairgpu_local_debug_report.py",
    "tests/final_stage/test_hairgpu_local_debug_report.py",
]
if sorted(set(TOUCHED)) != sorted(EXPECTED):
    raise RuntimeError(f"Unexpected modified-file set: {sorted(set(TOUCHED))}")

PACKAGE = EXPECTED + ["docs/LOCAL_DEBUG_CAPTURE.md"]
(ROOT / "local_debug_evidence_modified_files.txt").write_text(
    "\n".join(PACKAGE) + "\n",
    encoding="utf-8",
)
print("Patched local-debug evidence files:")
print("\n".join(PACKAGE))
