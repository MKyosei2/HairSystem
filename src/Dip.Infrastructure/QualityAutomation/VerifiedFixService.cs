// Purpose: Verifies proposed fixes in isolated workspaces and requires every formal required experiment/evidence contract to pass before Verified.
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using Dip.Application.Features.QualityAutomation;
using Dip.Domain.Enums;
using Dip.Domain.ValueObjects;

namespace Dip.Infrastructure.QualityAutomation;

public sealed class VerifiedFixService(
    IQaWorkspaceResolver workspaces,
    IRepositoryTechnologyDetector detector,
    IQaAdapterRegistry adapters,
    IAutonomousQaExperimentDispatcher? experimentDispatcher = null,
    AutonomousQaEvidenceValidator? requiredEvidenceValidator = null) : IVerifiedFixService
{
    private readonly AutonomousQaEvidenceValidator _evidenceValidator =
        requiredEvidenceValidator ?? new AutonomousQaEvidenceValidator();

    public async Task<VerifiedFixResult> VerifyAsync(VerifiedFixRequest request, CancellationToken ct)
    {
        var source = workspaces.Resolve(request.WorkspaceRelativePath);
        var worktreeAdded = false;
        var root = Path.Combine(Path.GetTempPath(), "dip-fix-verification", Guid.NewGuid().ToString("N"));
        var work = Path.Combine(root, "workspace");
        var artifact = Path.Combine(root, "artifacts");
        Directory.CreateDirectory(root);
        Directory.CreateDirectory(artifact);
        var steps = new List<string>();
        var findings = new List<QaFindingCandidate>();
        IReadOnlyList<AutonomousQaExperimentResult> postExperiments = Array.Empty<AutonomousQaExperimentResult>();
        string? commit = null;

        try
        {
            if (request.RegressionTests.Count is < 1 or > 20)
                return Fail("Verified Fix requires 1-20 regression-test drafts; a patch alone cannot become Verified.");
            if (request.UnifiedDiff.Length > 2_000_000)
                return Fail("Proposed patch exceeds the 2 MB verification limit.");
            if (request.RegressionTests.Sum(x => x.Content.Length) > 1_000_000)
                return Fail("Generated regression tests exceed the 1 MB total limit.");
            var policyViolation = PatchPolicyViolation(request.UnifiedDiff);
            if (policyViolation is not null)
                return Fail(policyViolation);

            var git = ProcessExecutor.FindExecutable("git");
            if (git is null)
                return Fail("Git is required for isolated fix verification.");

            if (Directory.Exists(Path.Combine(source, ".git")) || File.Exists(Path.Combine(source, ".git")))
            {
                var target = string.IsNullOrWhiteSpace(request.BaseCommit) ? "HEAD" : request.BaseCommit!;
                var add = await Run(git, ["worktree", "add", "--detach", work, target], source, ct);
                steps.Add("git worktree add --detach");
                if (add.ExitCode == 0)
                    worktreeAdded = true;
                if (add.ExitCode != 0)
                    return Fail("Unable to create verification worktree: " + Last(add.Error));
                commit = (await Run(git, ["rev-parse", "HEAD"], work, ct)).Output.Trim();
                if (!string.IsNullOrWhiteSpace(request.BaseCommit))
                {
                    var expected = (await Run(git, ["rev-parse", $"{request.BaseCommit}^{{commit}}"], source, ct)).Output.Trim();
                    if (string.IsNullOrWhiteSpace(expected)
                        || !string.Equals(expected, commit, StringComparison.OrdinalIgnoreCase))
                        return Fail($"Verification worktree revision mismatch (expected={expected}, actual={commit}).");
                }
            }
            else
            {
                if (!string.IsNullOrWhiteSpace(request.BaseCommit))
                    return Fail("BaseCommit was supplied but the configured source workspace is not a Git worktree; exact revision verification cannot be proven.");
                CopyTree(source, work);
                var init = await Run(git, ["init", "-q"], work, ct);
                if (init.ExitCode != 0)
                    return Fail("Unable to initialize temporary verification repository.");
                steps.Add("isolated source copy");
            }

            var requested = request.Language.IsUnknown ? LanguageId.Auto : request.Language;
            var baselineProfile = await detector.DetectAsync(work, ct);
            var baselineSelections = adapters.Resolve(requested, baselineProfile, work);
            if (baselineSelections.Count == 0)
                return Fail("No QA adapter can establish a baseline for the detected technology.");
            var adapterBaseline = await RunQaSetAsync(
                request,
                baselineSelections,
                work,
                Path.Combine(artifact, "baseline"),
                commit,
                "verified-fix-baseline",
                ct);
            steps.Add($"baseline adapter QA: {adapterBaseline.ExecutedAdapters} adapter(s), {adapterBaseline.AvailableTools.Count} available tool(s)");

            var patch = Path.Combine(root, "proposal.diff");
            await File.WriteAllTextAsync(patch, request.UnifiedDiff, Encoding.UTF8, ct);
            var patchSha256 = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(request.UnifiedDiff))).ToLowerInvariant();
            steps.Add($"patch sha256: {patchSha256}");

            var check = await Run(git, ["apply", "--check", "--whitespace=error", patch], work, ct);
            steps.Add("git apply --check");
            if (check.ExitCode != 0)
                return Fail("Proposed patch does not apply cleanly: " + Last(check.Error));
            var apply = await Run(git, ["apply", "--whitespace=error", patch], work, ct);
            if (apply.ExitCode != 0)
                return Fail("Patch application failed: " + Last(apply.Error));
            steps.Add("patch applied in isolated worktree");

            foreach (var test in request.RegressionTests)
            {
                if (test.Content.Length > 256_000)
                    return Fail($"Regression test '{test.RelativePath}' exceeds the 256 KB per-file limit.");
                var path = ResolveInside(work, test.RelativePath);
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                await File.WriteAllTextAsync(path, test.Content, Encoding.UTF8, ct);
                steps.Add("regression test: " + test.RelativePath);
            }

            var postProfile = await detector.DetectAsync(work, ct);
            var postSelections = adapters.Resolve(requested, postProfile, work);
            if (postSelections.Count == 0)
                return Fail("The patch removed or invalidated all QA adapter coverage.");
            var post = await RunQaSetAsync(
                request,
                postSelections,
                work,
                Path.Combine(artifact, "post"),
                commit,
                "verified-fix",
                ct);
            findings.AddRange(post.Findings);
            steps.Add($"post-patch adapter QA: {post.ExecutedAdapters} adapter(s), {post.AvailableTools.Count} available tool(s)");

            var missingTools = adapterBaseline.AvailableTools
                .Except(post.AvailableTools, StringComparer.OrdinalIgnoreCase)
                .OrderBy(x => x)
                .ToArray();
            if (missingTools.Length > 0)
                return Fail("Patch reduced verification coverage by making previously available tools disappear: " + string.Join(", ", missingTools));

            var newCritical = post.CriticalFingerprints
                .Except(adapterBaseline.CriticalFingerprints, StringComparer.OrdinalIgnoreCase)
                .ToArray();
            var allToolsSucceeded = post.ToolExecutions.Where(x => x.WasAvailable).All(x => x.Succeeded);

            var requiredExperimentsPassed = true;
            var environmentCompatible = true;
            var baselineContractComplete = true;
            var revisionBound = true;
            var missingRequired = Array.Empty<string>();
            var failedRequired = Array.Empty<string>();
            var missingEvidence = Array.Empty<string>();
            var environmentMismatches = Array.Empty<string>();
            var revisionMismatches = Array.Empty<string>();
            var baselineRevisionMismatches = Array.Empty<string>();

            if (request.ExperimentPlan is not null)
            {
                var requiredSteps = request.ExperimentPlan.Steps.Where(x => x.Required).ToArray();
                var baselineExperiments = request.BaselineExperiments ?? Array.Empty<AutonomousQaExperimentResult>();
                var baselineById = baselineExperiments.ToDictionary(x => x.StepId, StringComparer.OrdinalIgnoreCase);
                var baselineMissing = requiredSteps.Where(x => !baselineById.ContainsKey(x.Id)).Select(x => x.Id).ToArray();
                var baselineEvidenceMissing = requiredSteps
                    .Where(x => baselineById.TryGetValue(x.Id, out var result)
                                && !_evidenceValidator.Validate(x, result.EvidenceReferences).Satisfied)
                    .Select(x => x.Id)
                    .ToArray();
                if (!string.IsNullOrWhiteSpace(commit))
                {
                    baselineRevisionMismatches = requiredSteps
                        .Where(x => baselineById.TryGetValue(x.Id, out var result)
                                    && !string.IsNullOrWhiteSpace(result.RevisionUnderTest)
                                    && !string.Equals(result.RevisionUnderTest, commit, StringComparison.OrdinalIgnoreCase))
                        .Select(x => x.Id)
                        .ToArray();
                }
                baselineContractComplete = baselineMissing.Length == 0
                                           && baselineEvidenceMissing.Length == 0
                                           && baselineRevisionMismatches.Length == 0;
                if (!baselineContractComplete)
                    steps.Add($"baseline formal contract incomplete: missing={string.Join(',', baselineMissing)}, evidence={string.Join(',', baselineEvidenceMissing)}, revision={string.Join(',', baselineRevisionMismatches)}");

                if (experimentDispatcher is null)
                {
                    requiredExperimentsPassed = false;
                    missingRequired = requiredSteps.Select(x => x.Id).ToArray();
                    steps.Add("formal experiment dispatcher unavailable; required experiment verification failed closed");
                }
                else
                {
                    postExperiments = await experimentDispatcher.ExecutePlanAsync(
                        request.ExperimentPlan,
                        new AutonomousQaExperimentExecutionContext(
                            request.ProjectId,
                            request.SubjectKey ?? "verified-fix",
                            request.WorkspaceRelativePath,
                            commit,
                            request.Language,
                            AutonomousQaExperimentPhase.PostPatch,
                            request.ExperimentConfigurationJson,
                            work,
                            request.DurableJobId),
                        ct);

                    var byId = postExperiments.ToDictionary(x => x.StepId, StringComparer.OrdinalIgnoreCase);
                    missingRequired = requiredSteps
                        .Where(x => !byId.ContainsKey(x.Id))
                        .Select(x => x.Id)
                        .ToArray();
                    failedRequired = requiredSteps
                        .Where(x => byId.TryGetValue(x.Id, out var result)
                                    && (result.Status != AutonomousQaExperimentStatus.Passed || !result.Verified))
                        .Select(x => $"{x.Id}:{byId[x.Id].Status}")
                        .ToArray();
                    missingEvidence = requiredSteps
                        .Where(x => byId.TryGetValue(x.Id, out var result)
                                    && !_evidenceValidator.Validate(x, result.EvidenceReferences).Satisfied)
                        .Select(x => x.Id)
                        .ToArray();
                    requiredExperimentsPassed = missingRequired.Length == 0
                                                && failedRequired.Length == 0
                                                && missingEvidence.Length == 0;

                    environmentMismatches = postExperiments
                        .Where(postResult =>
                            !string.IsNullOrWhiteSpace(postResult.EnvironmentFingerprint)
                            && baselineById.TryGetValue(postResult.StepId, out var before)
                            && !string.IsNullOrWhiteSpace(before.EnvironmentFingerprint)
                            && !string.Equals(postResult.EnvironmentFingerprint, before.EnvironmentFingerprint, StringComparison.OrdinalIgnoreCase))
                        .Select(x => x.StepId)
                        .ToArray();
                    environmentCompatible = environmentMismatches.Length == 0;

                    if (!string.IsNullOrWhiteSpace(commit))
                    {
                        revisionMismatches = postExperiments
                            .Where(x => !string.IsNullOrWhiteSpace(x.RevisionUnderTest)
                                        && !string.Equals(x.RevisionUnderTest, commit, StringComparison.OrdinalIgnoreCase))
                            .Select(x => x.StepId)
                            .ToArray();
                        revisionBound = revisionMismatches.Length == 0;
                    }
                    steps.Add(
                        $"formal experiments: {postExperiments.Count} executed, requiredPass={requiredExperimentsPassed}, "
                        + $"evidenceComplete={missingEvidence.Length == 0}, environmentCompatible={environmentCompatible}, revisionBound={revisionBound}");
                }
            }

            var verified = post.ExecutedAdapters > 0
                           && allToolsSucceeded
                           && newCritical.Length == 0
                           && baselineContractComplete
                           && requiredExperimentsPassed
                           && environmentCompatible
                           && revisionBound;
            var status = verified ? "Verified" : "NotVerified";
            var summary = verified
                ? "Patch, generated regression tests, adapter QA, and every required formal experiment/evidence contract passed without reducing verification coverage. Human review is still required."
                : $"Patch did not satisfy the complete verification boundary (toolSuccess={allToolsSucceeded}, newCritical={newCritical.Length}, baselineContractComplete={baselineContractComplete}, requiredExperimentsPassed={requiredExperimentsPassed}, environmentCompatible={environmentCompatible}, revisionBound={revisionBound}, missingRequired={missingRequired.Length}, failedRequired={failedRequired.Length}, missingEvidence={missingEvidence.Length}).";

            await WriteSummaryAsync(
                artifact,
                $"Status={status}\nBaseCommit={commit}\nPatchSha256={patchSha256}\nBaselineCritical={adapterBaseline.CriticalFingerprints.Count}\nPostCritical={post.CriticalFingerprints.Count}\nNewCritical={newCritical.Length}\nMissingTools={string.Join(',', missingTools)}\nMissingRequiredExperiments={string.Join(',', missingRequired)}\nFailedRequiredExperiments={string.Join(',', failedRequired)}\nMissingRequiredEvidence={string.Join(',', missingEvidence)}\nEnvironmentMismatches={string.Join(',', environmentMismatches)}\nBaselineRevisionMismatches={string.Join(',', baselineRevisionMismatches)}\nRevisionMismatches={string.Join(',', revisionMismatches)}\nSteps={string.Join("; ", steps)}\n",
                ct);
            return new(true, verified, status, commit, artifact, steps, findings.Take(2000).ToArray(), summary, postExperiments);
        }
        finally
        {
            var cleanupFailure = await CleanupWorkspaceAsync(source, work, worktreeAdded);
            if (!string.IsNullOrWhiteSpace(cleanupFailure))
            {
                steps.Add("cleanup warning: " + cleanupFailure);
                try
                {
                    await File.AppendAllTextAsync(
                        Path.Combine(artifact, "verified-fix-summary.txt"),
                        $"CleanupFailure={cleanupFailure}\n",
                        CancellationToken.None);
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
                {
                    steps.Add("cleanup evidence write failed: " + ex.Message);
                }
            }
        }

        VerifiedFixResult Fail(string message)
        {
            try
            {
                File.WriteAllText(Path.Combine(artifact, "verified-fix-summary.txt"), "Status=Failed\n" + message);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                message += $" Verification summary could not be persisted: {ex.Message}";
            }
            return new(false, false, "Failed", commit, artifact, steps, findings, message, postExperiments);
        }
    }

    private static async Task<string?> CleanupWorkspaceAsync(string source, string work, bool worktreeAdded)
    {
        try
        {
            if (worktreeAdded)
            {
                var git = ProcessExecutor.FindExecutable("git");
                if (git is null)
                    return "Git became unavailable before the temporary worktree could be removed.";
                var result = await Run(git, ["worktree", "remove", "--force", work], source, CancellationToken.None);
                return result.ExitCode == 0 ? null : "git worktree remove failed: " + Last(result.Error);
            }
            if (Directory.Exists(work))
                Directory.Delete(work, true);
            return null;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException)
        {
            return ex.Message;
        }
    }

    private static async Task WriteSummaryAsync(string artifact, string content, CancellationToken ct)
    {
        try { await File.WriteAllTextAsync(Path.Combine(artifact, "verified-fix-summary.txt"), content, ct); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            throw new IOException("Verified Fix could not persist its verification summary artifact.", ex);
        }
    }

    private async Task<QaSnapshot> RunQaSetAsync(
        VerifiedFixRequest request,
        IReadOnlyList<QaAdapterSelection> selections,
        string workspace,
        string artifactRoot,
        string? commit,
        string subjectPrefix,
        CancellationToken ct)
    {
        Directory.CreateDirectory(artifactRoot);
        var findings = new List<(LanguageId Language, QaFindingCandidate Finding)>();
        var tools = new List<QaToolExecution>();
        var executed = 0;
        foreach (var selection in selections)
        {
            var result = await selection.Adapter.RunAsync(
                new QaExecutionCommand(
                    request.ProjectId,
                    $"{subjectPrefix}:{selection.Language.Value}",
                    selection.Language,
                    ".",
                    commit,
                    null,
                    null,
                    Math.Clamp(request.TimeoutSeconds, 30, 1800),
                    false,
                    false,
                    false),
                workspace,
                Path.Combine(artifactRoot, selection.Language.Value),
                ct);
            executed++;
            tools.AddRange(result.Tools);
            findings.AddRange(result.Findings.Select(x => (selection.Language, x)));
        }
        var available = tools.Where(x => x.WasAvailable).Select(x => x.Tool).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var critical = findings
            .Where(x => x.Finding.Severity == Severity.Critical)
            .Select(x => QaFingerprint.Create(x.Language, x.Finding))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        return new(executed, tools, findings.Select(x => x.Finding).ToArray(), available, critical);
    }

    private static string? PatchPolicyViolation(string diff)
    {
        if (string.IsNullOrWhiteSpace(diff) || !diff.Contains("diff --git ", StringComparison.Ordinal))
            return "Verified Fix requires a standard unified git diff.";
        var forbiddenTokens = new[]
        {
            "pragma warning disable", "eslint-disable", "noinspection", "@SuppressWarnings", "@Disabled", "@Ignore",
            "pytest.skip", "unittest.skip", "test.skip(", "describe.skip(", "Assert.Ignore", "TreatWarningsAsErrors>false", "NoWarn>"
        };
        var forbiddenPaths = new[] { ".github/workflows/", ".env", "secrets.", "secret.", "appsettings.production.json" };
        var hasNonTestChange = false;
        foreach (var line in diff.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries))
        {
            if (line.StartsWith("+++ b/", StringComparison.Ordinal))
            {
                var path = line[6..].Replace('\\', '/').ToLowerInvariant();
                if (forbiddenPaths.Any(token => path.Contains(token, StringComparison.OrdinalIgnoreCase)))
                    return $"Patch policy rejected protected path '{path}'.";
                if (!IsTestPath(path))
                    hasNonTestChange = true;
            }
            if (!line.StartsWith('+') || line.StartsWith("+++", StringComparison.Ordinal))
                continue;
            var token = forbiddenTokens.FirstOrDefault(t => line.Contains(t, StringComparison.OrdinalIgnoreCase));
            if (token is not null)
                return $"Patch policy rejected a diagnostic/test suppression token: {token}.";
        }
        if (!hasNonTestChange)
            return "Verified Fix requires at least one non-test source/configuration change; a generated test-only patch is not a fix.";
        return null;
    }

    private static bool IsTestPath(string path) =>
        path.Contains("/test", StringComparison.OrdinalIgnoreCase)
        || path.StartsWith("test", StringComparison.OrdinalIgnoreCase)
        || path.Contains("/spec", StringComparison.OrdinalIgnoreCase)
        || path.EndsWith(".spec.ts", StringComparison.OrdinalIgnoreCase)
        || path.EndsWith(".test.ts", StringComparison.OrdinalIgnoreCase);

    private static string ResolveInside(string root, string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative))
            throw new InvalidOperationException("Regression test path must be workspace-relative.");
        var resolvedRoot = Path.GetFullPath(root);
        var path = Path.GetFullPath(Path.Combine(resolvedRoot, relative));
        var prefix = resolvedRoot.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (!path.StartsWith(prefix, OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal))
            throw new InvalidOperationException("Regression test path escapes the verification worktree.");
        return path;
    }

    private static void CopyTree(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        var ignored = new HashSet<string>(
            [".git", "bin", "obj", "artifacts", "node_modules", "Library", "Temp", "Logs", "target", ".dipqa"],
            StringComparer.OrdinalIgnoreCase);
        var stack = new Stack<(string Source, string Destination)>();
        stack.Push((source, destination));
        while (stack.Count > 0)
        {
            var (currentSource, currentDestination) = stack.Pop();
            foreach (var file in Directory.EnumerateFiles(currentSource))
                File.Copy(file, Path.Combine(currentDestination, Path.GetFileName(file)), true);
            foreach (var child in Directory.EnumerateDirectories(currentSource))
            {
                if (ignored.Contains(Path.GetFileName(child)))
                    continue;
                var target = Path.Combine(currentDestination, Path.GetFileName(child));
                Directory.CreateDirectory(target);
                stack.Push((child, target));
            }
        }
    }

    private static async Task<(int ExitCode, string Output, string Error)> Run(
        string executable,
        IEnumerable<string> args,
        string cwd,
        CancellationToken ct)
    {
        var startInfo = new ProcessStartInfo(executable)
        {
            WorkingDirectory = cwd,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        foreach (var arg in args)
            startInfo.ArgumentList.Add(arg);
        using var process = Process.Start(startInfo) ?? throw new InvalidOperationException("Unable to start git.");
        var outputTask = process.StandardOutput.ReadToEndAsync(ct);
        var errorTask = process.StandardError.ReadToEndAsync(ct);
        await process.WaitForExitAsync(ct);
        return (process.ExitCode, await outputTask, await errorTask);
    }

    private static string Last(string text) =>
        text.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries).LastOrDefault() ?? text;

    private sealed record QaSnapshot(
        int ExecutedAdapters,
        IReadOnlyList<QaToolExecution> ToolExecutions,
        IReadOnlyList<QaFindingCandidate> Findings,
        IReadOnlySet<string> AvailableTools,
        IReadOnlySet<string> CriticalFingerprints);
}
