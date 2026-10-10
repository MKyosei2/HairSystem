// Purpose: Runs mutation experiments and records killed and surviving mutations through the QA sandbox.
using Dip.Application.Features.P2Intelligence;
using Dip.Application.Features.QualityAutomation;
using Dip.Infrastructure.QualityAutomation;

namespace Dip.Infrastructure.P2Intelligence;

internal sealed class MutationExperimentRunner(ProcessExecutor executor, IQaWorkspaceResolver workspaceResolver)
    : ExperimentExecutionSupport(executor, workspaceResolver)
{
    public async Task<MutationResult> RunMutationAsync(MutationCommand command, CancellationToken ct)
    {
        var workspace = workspaceResolver.Resolve(command.Target.WorkspaceRelativePath);
        var root = ArtifactRoot(command.Target.ProjectId, "mutation");
        var temp = Path.Combine(root, "workspace");
        await PrepareIsolatedWorkspaceAsync(workspace, temp, ct);
        var executable = ResolveTargetExecutable(temp, command.Target.Executable);

        var baselineDirectory = Path.Combine(root, "baseline");
        Directory.CreateDirectory(baselineDirectory);
        var baseline = await executor.RunQaAsync(
            executable,
            command.Target.Arguments ?? Array.Empty<string>(),
            temp,
            null,
            TimeSpan.FromSeconds(Math.Clamp(command.Target.TimeoutSeconds, 1, 3600)),
            Path.Combine(baselineDirectory, "stdout.log"),
            Path.Combine(baselineDirectory, "stderr.log"),
            ct);
        var baselinePassed = !baseline.TimedOut && baseline.ExitCode == 0;
        if (!baselinePassed)
        {
            var detail = baseline.TimedOut
                ? "Baseline timed out; mutation score was not calculated."
                : $"Baseline exited with code {(baseline.ExitCode?.ToString() ?? "n/a")}; mutation score was not calculated.";
            return new("InfrastructureFailure", 0, 0, 0, 0, 0m, Array.Empty<MutationCaseResult>(), RelativeArtifact(root), false, detail);
        }

        var requested = command.Mutations?.ToList() ?? new List<MutationCaseSpec>();
        if (command.GenerateCommonMutations)
            requested.AddRange(BuildAutomaticMutations(temp, command.AutoSourceFiles, Math.Clamp(command.MaxMutations, 1, 200) - requested.Count));
        var unique = requested
            .Where(x => !string.IsNullOrWhiteSpace(x.Id)
                        && !string.IsNullOrWhiteSpace(x.FileRelativePath)
                        && x.OriginalText != x.ReplacementText)
            .GroupBy(x => x.Id, StringComparer.OrdinalIgnoreCase)
            .Select(x => x.First())
            .Take(Math.Clamp(command.MaxMutations, 1, 200))
            .ToArray();

        var cases = new List<MutationCaseResult>();
        var infrastructureFailure = false;
        foreach (var mutation in unique)
        {
            ct.ThrowIfCancellationRequested();
            var file = SafePath(temp, mutation.FileRelativePath);
            if (!File.Exists(file))
            {
                cases.Add(new(mutation.Id, false, false, null, "File not found.", mutation.FileRelativePath, null, null, mutation.Operator));
                continue;
            }
            var original = await File.ReadAllTextAsync(file, ct);
            var offsets = FindAll(original, mutation.OriginalText, 3);
            if (offsets.Count == 0)
            {
                cases.Add(new(mutation.Id, false, false, null, "OriginalText was not found.", mutation.FileRelativePath, null, null, mutation.Operator));
                continue;
            }
            if (offsets.Count > 1)
            {
                cases.Add(new(mutation.Id, false, false, null, "OriginalText is ambiguous because it occurs more than once; specify a more precise mutation.", mutation.FileRelativePath, null, null, mutation.Operator));
                continue;
            }

            var index = offsets[0];
            var (line, column) = LineColumn(original, index);
            var mutated = original[..index] + mutation.ReplacementText + original[(index + mutation.OriginalText.Length)..];
            var directory = Path.Combine(root, "cases", SafeName(mutation.Id));
            Directory.CreateDirectory(directory);
            try
            {
                await File.WriteAllTextAsync(file, mutated, ct);
                await File.WriteAllTextAsync(
                    Path.Combine(directory, "mutation.patch.txt"),
                    $"File: {mutation.FileRelativePath}{Environment.NewLine}Line: {line}, Column: {column}{Environment.NewLine}Operator: {mutation.Operator ?? "explicit"}{Environment.NewLine}- {mutation.OriginalText}{Environment.NewLine}+ {mutation.ReplacementText}{Environment.NewLine}",
                    ct);
                var result = await executor.RunQaAsync(
                    executable,
                    command.Target.Arguments ?? Array.Empty<string>(),
                    temp,
                    null,
                    TimeSpan.FromSeconds(Math.Clamp(command.Target.TimeoutSeconds, 1, 3600)),
                    Path.Combine(directory, "stdout.log"),
                    Path.Combine(directory, "stderr.log"),
                    ct);
                if (!result.TimedOut && result.ExitCode is null)
                {
                    infrastructureFailure = true;
                    cases.Add(new(mutation.Id, true, false, null, "Infrastructure/sandbox failure starting the configured test target.", mutation.FileRelativePath, line, column, mutation.Operator));
                    break;
                }
                var killed = result.TimedOut || (result.ExitCode.HasValue && result.ExitCode.Value != 0);
                cases.Add(new(
                    mutation.Id,
                    true,
                    killed,
                    result.ExitCode,
                    killed ? "Tests detected the mutation." : "Mutation survived the configured tests.",
                    mutation.FileRelativePath,
                    line,
                    column,
                    mutation.Operator));
            }
            finally
            {
                await File.WriteAllTextAsync(file, original, CancellationToken.None);
            }
        }

        var applied = cases.Count(x => x.Applied);
        var killedCount = cases.Count(x => x.Applied && x.Killed);
        var score = applied == 0 ? 0m : decimal.Round((decimal)killedCount / applied * 100m, 2);
        var status = infrastructureFailure || applied == 0
            ? "InfrastructureFailure"
            : score >= 80m ? "Passed" : score >= 60m ? "Warning" : "Failed";
        var summary = $"Baseline passed under QA sandbox. {killedCount}/{applied} applied mutation(s) were killed; {Math.Max(0, applied - killedCount)} survived.";
        return new(status, cases.Count, applied, killedCount, Math.Max(0, applied - killedCount), score, cases, RelativeArtifact(root), true, summary);
    }
}
