// Purpose: Creates disposable exact-revision baseline worktrees and resolves deterministic changed-file sets for autonomous QA.
using Dip.Application.Features.QualityAutomation;

namespace Dip.Infrastructure.QualityAutomation;

public sealed class GitAutonomousQaWorkspaceManager(
    IQaWorkspaceResolver workspaces,
    ProcessExecutor executor) : IAutonomousQaWorkspaceLeaseFactory, IAutonomousQaChangeSetResolver
{
    public async Task<IAutonomousQaWorkspaceLease> CreateBaselineAsync(
        string workspaceRelativePath,
        string? revision,
        CancellationToken ct)
    {
        var source = workspaces.Resolve(workspaceRelativePath);
        var git = ProcessExecutor.FindExecutable("git")
                  ?? throw new InvalidOperationException("Git is required for exact-revision autonomous QA baselines.");
        if (!Directory.Exists(Path.Combine(source, ".git")) && !File.Exists(Path.Combine(source, ".git")))
            throw new InvalidOperationException("Exact-revision autonomous QA requires a Git workspace.");

        var targetRevision = string.IsNullOrWhiteSpace(revision) ? "HEAD" : revision.Trim();
        var resolved = await RunGitAsync(git, source, ["rev-parse", "--verify", $"{targetRevision}^{{commit}}"], ct);
        if (resolved.ExitCode != 0 || string.IsNullOrWhiteSpace(resolved.StandardOutput))
            throw new InvalidOperationException($"Autonomous QA baseline revision '{targetRevision}' could not be resolved.");
        var commit = resolved.StandardOutput.Trim();

        var root = Path.Combine(Path.GetTempPath(), "dip-autonomous-qa-baseline", Guid.NewGuid().ToString("N"));
        var worktree = Path.Combine(root, "workspace");
        Directory.CreateDirectory(root);
        var add = await RunGitAsync(git, source, ["worktree", "add", "--detach", worktree, commit], ct);
        if (add.ExitCode != 0)
        {
            DeleteBestEffort(root);
            throw new InvalidOperationException("Unable to create exact-revision autonomous QA worktree: " + Last(add.StandardError));
        }

        return new Lease(source, worktree, root, commit, git, executor);
    }

    public async Task<IReadOnlyList<string>> ResolveChangedFilesAsync(
        string workspaceRelativePath,
        string? revision,
        CancellationToken ct)
    {
        var source = workspaces.Resolve(workspaceRelativePath);
        var git = ProcessExecutor.FindExecutable("git");
        if (git is null || (!Directory.Exists(Path.Combine(source, ".git")) && !File.Exists(Path.Combine(source, ".git"))))
            return EnumerateRelevantFiles(source);

        var targetRevision = string.IsNullOrWhiteSpace(revision) ? "HEAD" : revision.Trim();
        var result = await RunGitAsync(
            git,
            source,
            ["diff-tree", "--root", "-m", "--no-commit-id", "--name-only", "-r", targetRevision],
            ct);
        if (result.ExitCode != 0)
            return Array.Empty<string>();

        return result.StandardOutput
            .Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(x => x.Replace('\\', '/'))
            .Where(x => !string.IsNullOrWhiteSpace(x))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Take(1000)
            .ToArray();
    }

    private async Task<ProcessExecutionResult> RunGitAsync(
        string git,
        string cwd,
        IEnumerable<string> args,
        CancellationToken ct)
    {
        var root = Path.Combine(Path.GetTempPath(), "dip-autonomous-qa-git", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            return await executor.RunAsync(
                git,
                args,
                cwd,
                null,
                TimeSpan.FromMinutes(2),
                Path.Combine(root, "stdout.log"),
                Path.Combine(root, "stderr.log"),
                ct);
        }
        finally
        {
            DeleteBestEffort(root);
        }
    }

    private static IReadOnlyList<string> EnumerateRelevantFiles(string root)
    {
        var ignored = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            ".git", "bin", "obj", "node_modules", "Library", "Temp", "Logs", "artifacts", ".dipqa", "target"
        };
        var result = new List<string>();
        var stack = new Stack<string>();
        stack.Push(root);
        while (stack.Count > 0 && result.Count < 1000)
        {
            var current = stack.Pop();
            foreach (var file in Directory.EnumerateFiles(current))
            {
                result.Add(Path.GetRelativePath(root, file).Replace('\\', '/'));
                if (result.Count >= 1000)
                    break;
            }
            foreach (var directory in Directory.EnumerateDirectories(current))
                if (!ignored.Contains(Path.GetFileName(directory)))
                    stack.Push(directory);
        }
        return result;
    }

    private static string Last(string text) =>
        text.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries).LastOrDefault() ?? text;

    private static void DeleteBestEffort(string path)
    {
        try
        {
            if (Directory.Exists(path))
                Directory.Delete(path, true);
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private sealed class Lease(
        string source,
        string workspace,
        string root,
        string revision,
        string git,
        ProcessExecutor executor) : IAutonomousQaWorkspaceLease
    {
        private int _disposed;
        public string WorkspacePath { get; } = workspace;
        public string Revision { get; } = revision;

        public async ValueTask DisposeAsync()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0)
                return;
            try
            {
                var logs = Path.Combine(root, "cleanup");
                Directory.CreateDirectory(logs);
                await executor.RunAsync(
                    git,
                    ["worktree", "remove", "--force", workspace],
                    source,
                    null,
                    TimeSpan.FromMinutes(1),
                    Path.Combine(logs, "stdout.log"),
                    Path.Combine(logs, "stderr.log"),
                    CancellationToken.None);
            }
            catch
            {
                // Best effort cleanup. A stale disposable worktree must not alter
                // the already-recorded experiment verdict.
            }
            finally
            {
                DeleteBestEffort(root);
            }
        }
    }
}
