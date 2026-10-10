// Purpose: Validates executable trust, workspace boundaries, and narrowly scoped external toolchain paths for QA process launches.
namespace Dip.Infrastructure.QualityAutomation;

internal static class ProcessCommandValidator
{
    internal static readonly HashSet<string> ExternalQaTools = new(
        new[]
        {
            "dotnet","python","python3","py","pytest","ruff","mypy","cmake","ctest","ninja","msbuild",
            "clang","clang++","clang-tidy","cppcheck","gcc","g++","node","npm","npx","pnpm","yarn","tsc",
            "go","cargo","rustc","javac","java","kotlinc","ruby","php","bash","pwsh","powershell","luac",
            "lua","swiftc","sqlfluff","glslangvalidator","dxc","fxc","semgrep","eslint","shellcheck","rubocop",
            "rspec","phpstan","psalm","phpunit","bats","busted","swift","godot","godot4","gradle","mvn",
            "mvnw","gradlew","cargo-fuzz",
            // Bounded autonomous-QA media/load/GPU tools. These remain canonical PATH-only tools;
            // arbitrary external executables are still rejected and custom runners must live in the workspace.
            "ffmpeg","ffprobe","k6","renderdoccmd","nsys","ncu"
        },
        StringComparer.OrdinalIgnoreCase);

    internal static void ValidateExecutable(string executable, string workspace)
    {
        var canonicalWorkspace = CanonicalDirectory(workspace);
        var comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;

        if (Path.IsPathRooted(executable))
        {
            var full = Path.GetFullPath(executable);
            var canonical = CanonicalFile(full);
            if (IsInside(canonicalWorkspace, canonical, comparison))
                return;
            var name = Path.GetFileNameWithoutExtension(full);
            if (ExternalQaTools.Contains(name))
            {
                var resolved = FindExecutable(Path.GetFileName(full)) ?? FindExecutable(name);
                if (!string.IsNullOrWhiteSpace(resolved)
                    && string.Equals(CanonicalFile(resolved), canonical, comparison))
                    return;
            }
            throw new InvalidOperationException(
                $"QA executable outside workspace is not the canonical trusted PATH tool: {full}");
        }

        if (executable.Contains(Path.DirectorySeparatorChar)
            || executable.Contains(Path.AltDirectorySeparatorChar))
        {
            var full = Path.GetFullPath(Path.Combine(workspace, executable));
            var canonical = CanonicalCandidate(full);
            if (!IsInside(canonicalWorkspace, canonical, comparison))
                throw new InvalidOperationException(
                    "QA executable escapes the workspace, including through a symbolic link.");
            return;
        }

        if (!ExternalQaTools.Contains(Path.GetFileNameWithoutExtension(executable)))
        {
            throw new InvalidOperationException(
                $"QA external tool '{executable}' is not allowlisted. Put custom runners inside the workspace or extend the trusted runner image.");
        }
    }

    internal static void ValidatePathArguments(IEnumerable<string> arguments, string workspace)
    {
        var canonicalWorkspace = CanonicalDirectory(workspace);
        var comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;

        foreach (var argument in arguments)
        {
            var candidate = ArgumentPathCandidate(argument, workspace);
            if (candidate is null)
                continue;

            var full = Path.IsPathFullyQualified(candidate)
                ? Path.GetFullPath(candidate)
                : Path.GetFullPath(Path.Combine(workspace, candidate));
            var canonical = CanonicalCandidate(full);
            if (IsInside(canonicalWorkspace, canonical, comparison))
                continue;

            if (IsTrustedExternalToolchainArgument(argument, canonical, comparison))
                continue;

            throw new InvalidOperationException($"QA argument path escapes the workspace: {candidate}");
        }
    }

    internal static string? ArgumentPathCandidate(string argument, string workspace)
    {
        if (string.IsNullOrWhiteSpace(argument))
            return null;

        var trimmed = argument.Trim('"');
        if (Path.IsPathFullyQualified(trimmed))
            return trimmed;

        var equals = trimmed.IndexOf('=');
        if (equals > 0 && equals < trimmed.Length - 1)
        {
            var name = trimmed[..equals];
            if (IsCMakeFlagsAssignment(name))
                return null;

            var value = trimmed[(equals + 1)..].Trim('"');
            if (Path.IsPathFullyQualified(value) || LooksLikeRelativePath(value, workspace))
                return value;
        }

        if (!trimmed.StartsWith('-') && LooksLikeRelativePath(trimmed, workspace))
            return trimmed;

        return null;
    }

    internal static bool LooksLikeRelativePath(string value, string workspace)
    {
        if (string.IsNullOrWhiteSpace(value))
            return false;
        if (value.StartsWith('.') || value.Contains(Path.DirectorySeparatorChar)
            || value.Contains(Path.AltDirectorySeparatorChar))
            return true;
        try
        {
            var candidate = Path.Combine(workspace, value);
            return File.Exists(candidate) || Directory.Exists(candidate);
        }
        catch
        {
            return false;
        }
    }

    internal static string CanonicalCandidate(string path)
    {
        var full = Path.GetFullPath(path);
        if (File.Exists(full))
            return CanonicalFile(full);
        if (Directory.Exists(full))
            return CanonicalDirectory(full);

        var tail = new Stack<string>();
        var current = full;
        while (!string.IsNullOrWhiteSpace(current)
               && !File.Exists(current)
               && !Directory.Exists(current))
        {
            var name = Path.GetFileName(current);
            if (!string.IsNullOrEmpty(name))
                tail.Push(name);
            var parent = Path.GetDirectoryName(current);
            if (string.IsNullOrWhiteSpace(parent) || string.Equals(parent, current, StringComparison.Ordinal))
                break;
            current = parent;
        }

        var canonical = Directory.Exists(current)
            ? CanonicalDirectory(current)
            : File.Exists(current)
                ? CanonicalFile(current)
                : Path.GetFullPath(current);
        while (tail.Count > 0)
            canonical = Path.Combine(canonical, tail.Pop());
        return Path.GetFullPath(canonical);
    }

    internal static string CanonicalFile(string path)
    {
        var full = Path.GetFullPath(path);
        try { return new FileInfo(full).ResolveLinkTarget(true)?.FullName ?? full; }
        catch { return full; }
    }

    internal static string CanonicalDirectory(string path)
    {
        var full = Path.GetFullPath(path);
        try { return new DirectoryInfo(full).ResolveLinkTarget(true)?.FullName ?? full; }
        catch { return full; }
    }

    internal static bool IsInside(string root, string path, StringComparison comparison)
        => path.Equals(root, comparison)
           || path.StartsWith(
               root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
               + Path.DirectorySeparatorChar,
               comparison);

    internal static string? FindExecutable(string name)
    {
        if (Path.IsPathRooted(name) && File.Exists(name))
            return name;
        var candidates = new List<string> { name };
        if (OperatingSystem.IsWindows() && Path.GetExtension(name).Length == 0)
            candidates.AddRange([name + ".exe", name + ".cmd", name + ".bat"]);
        foreach (var directory in (Environment.GetEnvironmentVariable("PATH") ?? "")
                 .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        {
            foreach (var candidate in candidates)
            {
                var path = Path.Combine(directory.Trim('"'), candidate);
                if (File.Exists(path))
                    return path;
            }
        }
        return null;
    }

    internal static bool IsTrustedVisualStudioInstance(
        string candidate,
        string expectedInstallationPath,
        StringComparison comparison)
    {
        if (string.IsNullOrWhiteSpace(candidate)
            || string.IsNullOrWhiteSpace(expectedInstallationPath)
            || !Directory.Exists(candidate)
            || !Directory.Exists(expectedInstallationPath))
            return false;

        var canonicalCandidate = CanonicalDirectory(candidate);
        var canonicalExpected = CanonicalDirectory(expectedInstallationPath);
        if (!string.Equals(canonicalCandidate, canonicalExpected, comparison))
            return false;

        var vcvars = Path.Combine(canonicalCandidate, "VC", "Auxiliary", "Build", "vcvars64.bat");
        return File.Exists(vcvars);
    }

    private static bool IsTrustedExternalToolchainArgument(
        string argument,
        string canonicalCandidate,
        StringComparison comparison)
    {
        var variable = CMakeAssignmentName(argument);
        if (variable is null)
            return false;

        if (variable.Equals("CMAKE_GENERATOR_INSTANCE", StringComparison.OrdinalIgnoreCase))
        {
            if (!OperatingSystem.IsWindows())
                return false;
            var resolved = VisualStudioToolchainResolver.TryResolve();
            return resolved is not null
                   && IsTrustedVisualStudioInstance(canonicalCandidate, resolved.InstallationPath, comparison);
        }

        if (variable.Equals("CMAKE_C_COMPILER", StringComparison.OrdinalIgnoreCase)
            || variable.Equals("CMAKE_CXX_COMPILER", StringComparison.OrdinalIgnoreCase)
            || variable.Equals("CMAKE_MAKE_PROGRAM", StringComparison.OrdinalIgnoreCase))
        {
            if (!File.Exists(canonicalCandidate))
                return false;
            var name = Path.GetFileNameWithoutExtension(canonicalCandidate);
            if (!ExternalQaTools.Contains(name))
                return false;
            var resolved = FindExecutable(Path.GetFileName(canonicalCandidate)) ?? FindExecutable(name);
            return !string.IsNullOrWhiteSpace(resolved)
                   && string.Equals(CanonicalFile(resolved), CanonicalFile(canonicalCandidate), comparison);
        }

        return false;
    }

    private static string? CMakeAssignmentName(string argument)
    {
        if (string.IsNullOrWhiteSpace(argument))
            return null;
        var trimmed = argument.Trim('"');
        var equals = trimmed.IndexOf('=');
        if (equals <= 0)
            return null;
        var name = trimmed[..equals].Trim();
        if (name.StartsWith("-D", StringComparison.OrdinalIgnoreCase))
            name = name[2..];
        else
            name = name.TrimStart('-', '/');
        return name.StartsWith("CMAKE_", StringComparison.OrdinalIgnoreCase) ? name : null;
    }

    private static bool IsCMakeFlagsAssignment(string name)
    {
        var normalized = name.Trim();
        if (normalized.StartsWith("-D", StringComparison.OrdinalIgnoreCase))
            normalized = normalized[2..];
        else
            normalized = normalized.TrimStart('-', '/');
        if (!normalized.StartsWith("CMAKE_", StringComparison.OrdinalIgnoreCase))
            return false;
        return normalized.EndsWith("_FLAGS", StringComparison.OrdinalIgnoreCase)
               || normalized.Contains("_FLAGS_", StringComparison.OrdinalIgnoreCase);
    }
}
