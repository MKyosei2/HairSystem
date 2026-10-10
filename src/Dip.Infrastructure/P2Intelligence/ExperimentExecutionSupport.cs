// Purpose: Provides shared workspace, sandboxed target execution, artifact, cleanup, and diagnostic helpers for P2 experiment runners.
using System.Security.Cryptography;
using System.Text;
using Dip.Application.Features.P2Intelligence;
using Dip.Application.Features.QualityAutomation;
using Dip.Infrastructure.QualityAutomation;

namespace Dip.Infrastructure.P2Intelligence;

internal abstract class ExperimentExecutionSupport
{
    protected readonly ProcessExecutor executor;
    protected readonly IQaWorkspaceResolver workspaceResolver;

    protected ExperimentExecutionSupport(ProcessExecutor executor, IQaWorkspaceResolver workspaceResolver)
    {
        this.executor = executor;
        this.workspaceResolver = workspaceResolver;
    }

    protected async Task<byte[]> MinimizeFailureAsync(
        string executable,
        string workspace,
        FuzzCommand command,
        byte[] original,
        string expectedFingerprint,
        bool expectCrash,
        CancellationToken ct)
    {
        var current = original.ToArray();
        var maxRuns = Math.Clamp(command.MaxMinimizationRunsPerFailure, 1, 100);
        var runs = 0;
        for (var chunk = Math.Max(1, current.Length / 2);
             chunk >= 1 && runs < maxRuns;
             chunk = Math.Max(0, chunk / 2))
        {
            var changed = true;
            while (changed && runs < maxRuns)
            {
                changed = false;
                for (var offset = 0; offset < current.Length && runs < maxRuns; offset += Math.Max(1, chunk))
                {
                    var length = Math.Min(chunk, current.Length - offset);
                    if (length <= 0 || current.Length - length <= 0)
                        continue;
                    var candidate = current[..offset].Concat(current[(offset + length)..]).ToArray();
                    var input = Path.Combine(workspace, ".dipqa", "fuzz-min", $"{Guid.NewGuid():N}.bin");
                    var logs = Path.Combine(workspace, ".dipqa", "fuzz-min", $"{Guid.NewGuid():N}");
                    Directory.CreateDirectory(Path.GetDirectoryName(input)!);
                    Directory.CreateDirectory(logs);
                    try
                    {
                        await File.WriteAllBytesAsync(input, candidate, ct);
                        var arguments = (command.Target.Arguments ?? Array.Empty<string>())
                            .Select(a => a.Replace(command.ArgumentPlaceholder, input, StringComparison.Ordinal))
                            .ToArray();
                        var result = await executor.RunQaAsync(
                            executable,
                            arguments,
                            workspace,
                            new Dictionary<string, string>
                            {
                                ["DIP_FUZZ"] = "1",
                                ["DIP_FUZZ_MINIMIZE"] = "1"
                            },
                            TimeSpan.FromSeconds(Math.Clamp(command.Target.TimeoutSeconds, 1, 600)),
                            Path.Combine(logs, "stdout.log"),
                            Path.Combine(logs, "stderr.log"),
                            ct);
                        runs++;
                        var sameKind = expectCrash
                            ? !result.TimedOut && result.ExitCode is not null && result.ExitCode != 0
                            : result.TimedOut;
                        var sameFingerprint = Fingerprint(result.StandardOutput + "\n" + result.StandardError) == expectedFingerprint;
                        if (sameKind && sameFingerprint)
                        {
                            current = candidate;
                            changed = true;
                            break;
                        }
                    }
                    finally
                    {
                        DeleteBestEffort(input);
                        DeleteDirectoryBestEffort(logs);
                    }
                }
            }
            if (chunk == 1)
                break;
        }
        return current;
    }

    protected static byte[] BuildFuzzInput(int index, int max, Random random)
    {
        var boundary = new[] { 0, 1, 2, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 511, 512, 1023, 1024, 4095, 4096 };
        var size = index < boundary.Length ? Math.Min(boundary[index], max) : random.Next(0, max + 1);
        var bytes = new byte[size];
        switch (index % 8)
        {
            case 1:
                Array.Fill(bytes, (byte)0x00);
                break;
            case 2:
                Array.Fill(bytes, (byte)0xFF);
                break;
            case 3:
                Array.Fill(bytes, (byte)'A');
                break;
            case 4:
                for (var i = 0; i < bytes.Length; i++)
                    bytes[i] = (byte)(i & 0xFF);
                break;
            default:
                random.NextBytes(bytes);
                break;
        }
        return bytes;
    }

    protected static List<MutationCaseSpec> BuildAutomaticMutations(
        string workspace,
        IReadOnlyList<string>? sourceFiles,
        int remaining)
    {
        var result = new List<MutationCaseSpec>();
        if (remaining <= 0 || sourceFiles is null)
            return result;
        var operators = new (string Old, string New, string Name)[]
        {
            (">=", ">", "boundary-gte-to-gt"), ("<=", "<", "boundary-lte-to-lt"),
            ("==", "!=", "equality-invert"), ("!=", "==", "inequality-invert"),
            ("true", "false", "bool-true-to-false"), ("false", "true", "bool-false-to-true"),
            (" + ", " - ", "arithmetic-plus-to-minus"), (" - ", " + ", "arithmetic-minus-to-plus")
        };
        foreach (var relative in sourceFiles.Where(x => !string.IsNullOrWhiteSpace(x)).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            if (result.Count >= remaining)
                break;
            var file = SafePath(workspace, relative);
            if (!File.Exists(file))
                continue;
            var text = File.ReadAllText(file);
            foreach (var op in operators)
            {
                var search = 0;
                while (result.Count < remaining)
                {
                    var index = text.IndexOf(op.Old, search, StringComparison.Ordinal);
                    if (index < 0)
                        break;
                    var contextStart = Math.Max(0, index - 24);
                    var contextLength = Math.Min(text.Length - contextStart, op.Old.Length + 48);
                    var context = text.Substring(contextStart, contextLength);
                    if (FindAll(text, context, 2).Count == 1)
                    {
                        var local = context.IndexOf(op.Old, StringComparison.Ordinal);
                        var replacement = context[..local] + op.New + context[(local + op.Old.Length)..];
                        result.Add(new($"auto-{op.Name}-{result.Count + 1:D3}", relative, context, replacement, op.Name));
                    }
                    search = index + op.Old.Length;
                }
            }
        }
        return result;
    }

    protected static List<int> FindAll(string text, string value, int limit)
    {
        var result = new List<int>();
        if (string.IsNullOrEmpty(value))
            return result;
        var start = 0;
        while (start <= text.Length - value.Length && result.Count < limit)
        {
            var index = text.IndexOf(value, start, StringComparison.Ordinal);
            if (index < 0)
                break;
            result.Add(index);
            start = index + Math.Max(1, value.Length);
        }
        return result;
    }

    protected static (int Line, int Column) LineColumn(string text, int offset)
    {
        var line = 1;
        var column = 1;
        for (var i = 0; i < Math.Min(offset, text.Length); i++)
        {
            if (text[i] == '\n')
            {
                line++;
                column = 1;
            }
            else
            {
                column++;
            }
        }
        return (line, column);
    }

    protected async Task<(bool? Result, long DurationMs)> EvaluateCommitAsync(
        string git,
        string workspace,
        string root,
        string commit,
        ExperimentTarget target,
        CancellationToken ct)
    {
        var worktree = Path.Combine(root, "worktrees", SafeName(commit));
        if (Directory.Exists(worktree))
            Directory.Delete(worktree, true);
        Directory.CreateDirectory(Path.GetDirectoryName(worktree)!);
        var add = await RunGitAsync(git, workspace, root, ["worktree", "add", "--force", "--detach", worktree, commit], ct);
        if (add.ExitCode != 0)
            return (null, add.DurationMs);
        try
        {
            var executable = ResolveTargetExecutable(worktree, target.Executable);
            var directory = Path.Combine(root, "evaluations", SafeName(commit));
            Directory.CreateDirectory(directory);
            var result = await executor.RunQaAsync(
                executable,
                target.Arguments ?? Array.Empty<string>(),
                worktree,
                null,
                TimeSpan.FromSeconds(Math.Clamp(target.TimeoutSeconds, 1, 3600)),
                Path.Combine(directory, "stdout.log"),
                Path.Combine(directory, "stderr.log"),
                ct);
            if (!result.TimedOut && result.ExitCode is null)
                return (null, result.DurationMs);
            return (!result.TimedOut && result.ExitCode == 0, result.DurationMs);
        }
        finally
        {
            var remove = await RunGitAsync(git, workspace, root, ["worktree", "remove", "--force", worktree], CancellationToken.None);
            if (remove.ExitCode != 0)
                await File.AppendAllTextAsync(
                    Path.Combine(root, "worktree-cleanup-warning.log"),
                    remove.StandardError + Environment.NewLine,
                    CancellationToken.None);
        }
    }

    /// <summary>
    /// Git is a trusted control-plane tool, not target code. It intentionally uses
    /// RunAsync so repository/worktree management is not blocked by target sandbox path rules.
    /// </summary>
    protected async Task<ProcessExecutionResult> RunGitAsync(
        string git,
        string workspace,
        string root,
        IEnumerable<string> args,
        CancellationToken ct)
    {
        var id = Guid.NewGuid().ToString("N");
        var directory = Path.Combine(root, "git");
        Directory.CreateDirectory(directory);
        return await executor.RunAsync(
            git,
            args,
            workspace,
            null,
            TimeSpan.FromMinutes(5),
            Path.Combine(directory, $"{id}.out.log"),
            Path.Combine(directory, $"{id}.err.log"),
            ct);
    }

    protected async Task PrepareIsolatedWorkspaceAsync(string source, string target, CancellationToken ct)
    {
        if (Directory.Exists(target))
            Directory.Delete(target, true);
        var git = ProcessExecutor.FindExecutable("git");
        if (git is not null && (Directory.Exists(Path.Combine(source, ".git")) || File.Exists(Path.Combine(source, ".git"))))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            var root = Path.GetDirectoryName(target)!;
            var result = await RunGitAsync(git, source, root, ["worktree", "add", "--force", "--detach", target, "HEAD"], ct);
            if (result.ExitCode == 0)
                return;
        }
        CopyDirectory(source, target);
    }

    protected static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (var file in Directory.EnumerateFiles(source))
            File.Copy(file, Path.Combine(target, Path.GetFileName(file)), true);
        foreach (var directory in Directory.EnumerateDirectories(source))
        {
            var name = Path.GetFileName(directory);
            if (name is ".git" or "bin" or "obj" or ".dipqa" or "artifacts" or "node_modules" or ".vs")
                continue;
            CopyDirectory(directory, Path.Combine(target, name));
        }
    }

    protected static void DeleteBestEffort(string path)
    {
        try
        {
            if (File.Exists(path))
                File.Delete(path);
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    protected static void DeleteDirectoryBestEffort(string path)
    {
        try
        {
            if (Directory.Exists(path))
                Directory.Delete(path, true);
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    protected static string ResolveTargetExecutable(string workspace, string executable)
    {
        if (string.IsNullOrWhiteSpace(executable))
            throw new ArgumentException("Executable is required.");
        if (Path.IsPathRooted(executable))
            throw new InvalidOperationException("P2 target executable must be workspace-relative.");
        var resolved = SafePath(workspace, executable);
        if (!File.Exists(resolved))
            throw new FileNotFoundException("P2 target executable was not found inside the configured workspace.", resolved);
        return resolved;
    }

    protected static string SafePath(string root, string relative)
    {
        if (Path.IsPathRooted(relative))
            throw new InvalidOperationException("Path must be relative.");
        var full = Path.GetFullPath(Path.Combine(root, relative));
        var canonicalRoot = Path.GetFullPath(root);
        var comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        var prefix = canonicalRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (!full.StartsWith(prefix, comparison) && !full.Equals(canonicalRoot, comparison))
            throw new InvalidOperationException("Path escapes the configured workspace.");
        return full;
    }

    protected static string ArtifactRoot(Guid projectId, string category)
    {
        var root = Path.GetFullPath(Path.Combine(
            "artifacts",
            "p2",
            projectId.ToString("N"),
            category,
            $"{DateTimeOffset.UtcNow:yyyyMMdd-HHmmss}-{Guid.NewGuid():N}"));
        Directory.CreateDirectory(root);
        return root;
    }

    protected static string RelativeArtifact(string full) =>
        Path.GetRelativePath(Path.GetFullPath("."), full).Replace('\\', '/');

    protected static string Fingerprint(string text) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Normalize(text)))).ToLowerInvariant()[..16];

    protected static string Normalize(string text) =>
        string.Join(
            "\n",
            text.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries)
                .Select(line => line.Trim())
                .TakeLast(30));

    protected static string SafeName(string value)
    {
        var safe = string.Concat(value.Select(c => char.IsLetterOrDigit(c) || c is '-' or '_' ? c : '_'));
        return safe[..Math.Min(80, safe.Length)];
    }
}
