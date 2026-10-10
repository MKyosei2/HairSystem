// Purpose: Runs repeated executions through the QA sandbox to measure flaky or unstable behavior.
using Dip.Application.Features.P2Intelligence;
using Dip.Application.Features.QualityAutomation;
using Dip.Infrastructure.QualityAutomation;

namespace Dip.Infrastructure.P2Intelligence;

internal sealed class RepeatedExperimentRunner(ProcessExecutor executor, IQaWorkspaceResolver workspaceResolver)
    : ExperimentExecutionSupport(executor, workspaceResolver)
{
    public async Task<FlakyResult> RunRepeatedAsync(RepeatedRunCommand command, CancellationToken ct)
    {
        var workspace = workspaceResolver.Resolve(command.Target.WorkspaceRelativePath);
        var executable = ResolveTargetExecutable(workspace, command.Target.Executable);
        var root = ArtifactRoot(command.Target.ProjectId, "flaky");
        var attempts = Math.Clamp(command.Attempts, 2, 200);
        var runs = new List<FlakyRunAttempt>(attempts);

        for (var i = 1; i <= attempts; i++)
        {
            ct.ThrowIfCancellationRequested();
            var directory = Path.Combine(root, $"attempt-{i:D3}");
            Directory.CreateDirectory(directory);
            var result = await executor.RunQaAsync(
                executable,
                command.Target.Arguments ?? Array.Empty<string>(),
                workspace,
                null,
                TimeSpan.FromSeconds(Math.Clamp(command.Target.TimeoutSeconds, 1, 3600)),
                Path.Combine(directory, "stdout.log"),
                Path.Combine(directory, "stderr.log"),
                ct);
            if (!result.TimedOut && result.ExitCode is null)
            {
                runs.Add(new(i, false, false, null, result.DurationMs, Fingerprint(result.StandardError)));
                return new("InfrastructureFailure", attempts, runs.Count(x => x.Passed), runs.Count(x => !x.Passed), 0, 0m, 0m, runs, RelativeArtifact(root));
            }

            var passed = !result.TimedOut && result.ExitCode == 0;
            runs.Add(new(i, passed, result.TimedOut, result.ExitCode, result.DurationMs,
                Fingerprint(result.StandardOutput + "\n" + result.StandardError)));
        }

        var failed = runs.Count(x => !x.Passed);
        var timedOut = runs.Count(x => x.TimedOut);
        var rate = decimal.Round((decimal)failed / attempts * 100m, 2);
        var status = rate >= command.FlakyFailurePercent ? "Failed" : rate >= command.FlakyWarningPercent ? "Warning" : "Passed";
        var confidence = decimal.Round(Math.Min(99m, 55m + (attempts - 2) * 2m), 2);
        return new(status, attempts, attempts - failed, failed, timedOut, rate, confidence, runs, RelativeArtifact(root));
    }
}
