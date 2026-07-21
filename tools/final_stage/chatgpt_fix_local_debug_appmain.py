from __future__ import annotations

from pathlib import Path


PATH = Path(__file__).resolve().parents[2] / "apps" / "dx11_viewer" / "src" / "AppMain.cpp"


def replace_function(text: str, signature: str, replacement: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise RuntimeError(f"Function signature not found: {signature}")
    line_start = text.rfind("\n", 0, start) + 1
    brace = text.find("{", start)
    if brace < 0:
        raise RuntimeError(f"Opening brace not found: {signature}")
    depth = 0
    end = brace
    while end < len(text):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                end += 1
                # Consume line endings only. Consuming ordinary spaces would remove
                # the indentation of the next function and break its signature lookup.
                while end < len(text) and text[end] in "\r\n":
                    end += 1
                break
        end += 1
    if depth != 0:
        raise RuntimeError(f"Unbalanced function: {signature}")
    return text[:line_start] + replacement.rstrip() + "\n\n" + text[end:]


text = PATH.read_text(encoding="utf-8")
write_context = r'''    bool WriteCaptureContext(
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
    }'''

quote_function = r'''    std::wstring QuoteCommandLineArgument(const std::wstring& value)
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
    }'''

text = replace_function(text, "    bool WriteCaptureContext(", write_context)
text = replace_function(text, "    std::wstring QuoteCommandLineArgument(", quote_function)
PATH.write_text(text, encoding="utf-8", newline="\n")
