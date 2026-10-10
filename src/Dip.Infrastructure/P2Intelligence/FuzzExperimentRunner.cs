// Purpose: Runs fuzz experiments, fingerprints failures, minimizes reproductions, and always executes targets through the QA sandbox.
using Dip.Application.Features.P2Intelligence;
using Dip.Application.Features.QualityAutomation;
using Dip.Infrastructure.QualityAutomation;

namespace Dip.Infrastructure.P2Intelligence;

internal sealed class FuzzExperimentRunner(ProcessExecutor executor, IQaWorkspaceResolver workspaceResolver)
    : ExperimentExecutionSupport(executor, workspaceResolver)
{
    public async Task<FuzzResult> RunFuzzAsync(FuzzCommand command, CancellationToken ct)
    {
        var workspace = workspaceResolver.Resolve(command.Target.WorkspaceRelativePath);
        var executable = ResolveTargetExecutable(workspace, command.Target.Executable);
        var root = ArtifactRoot(command.Target.ProjectId, "fuzz");
        var corpusDirectory = Path.Combine(root, "corpus");
        var seedDirectory = Path.Combine(corpusDirectory, "seed");
        var interestingDirectory = Path.Combine(corpusDirectory, "interesting");
        var crashDirectory = Path.Combine(corpusDirectory, "crash");
        var timeoutDirectory = Path.Combine(corpusDirectory, "timeout");
        Directory.CreateDirectory(seedDirectory);
        Directory.CreateDirectory(interestingDirectory);
        Directory.CreateDirectory(crashDirectory);
        Directory.CreateDirectory(timeoutDirectory);

        var configuredArguments = command.Target.Arguments ?? Array.Empty<string>();
        if (string.IsNullOrWhiteSpace(command.ArgumentPlaceholder)
            || !configuredArguments.Any(a => a.Contains(command.ArgumentPlaceholder, StringComparison.Ordinal)))
        {
            var errorPath = Path.Combine(root, "fuzz-manifest.json");
            var errorManifest = System.Text.Json.JsonSerializer.Serialize(new
            {
                status = "InfrastructureFailure",
                reason = "At least one configured target argument must contain the fuzz input placeholder.",
                argumentPlaceholder = command.ArgumentPlaceholder
            }, new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
            await File.WriteAllTextAsync(errorPath, errorManifest, ct);
            return new("InfrastructureFailure", 0, 0, 0, 0, Array.Empty<FuzzCaseResult>(), RelativeArtifact(root), Array.Empty<FuzzFailureArtifact>());
        }

        var random = new Random(command.Seed);
        var results = new List<FuzzCaseResult>();
        var failures = new List<FuzzFailureArtifact>();
        var seenFailureFingerprints = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var seenOutputFingerprints = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var infrastructureFailure = false;
        var cases = Math.Clamp(command.Cases, 1, 10000);
        var max = Math.Clamp(command.MaxInputBytes, 1, 1024 * 1024);
        var scratch = Path.Combine(workspace, ".dipqa", "fuzz");
        Directory.CreateDirectory(scratch);
        var transient = Path.Combine(scratch, $"current-input-{Guid.NewGuid():N}.bin");

        try
        {
            for (var i = 0; i < cases; i++)
            {
                ct.ThrowIfCancellationRequested();
                var bytes = BuildFuzzInput(i, max, random);
                await File.WriteAllBytesAsync(transient, bytes, ct);
                if (i < 8)
                    await File.WriteAllBytesAsync(Path.Combine(seedDirectory, $"seed-{i:D2}.bin"), bytes, ct);

                var arguments = configuredArguments
                    .Select(a => a.Replace(command.ArgumentPlaceholder, transient, StringComparison.Ordinal))
                    .ToArray();
                var directory = Path.Combine(root, "runs", $"case-{i:D5}");
                Directory.CreateDirectory(directory);
                var result = await executor.RunQaAsync(
                    executable,
                    arguments,
                    workspace,
                    new Dictionary<string, string>
                    {
                        ["DIP_FUZZ"] = "1",
                        ["DIP_FUZZ_SEED"] = command.Seed.ToString(),
                        ["DIP_FUZZ_CASE"] = i.ToString()
                    },
                    TimeSpan.FromSeconds(Math.Clamp(command.Target.TimeoutSeconds, 1, 600)),
                    Path.Combine(directory, "stdout.log"),
                    Path.Combine(directory, "stderr.log"),
                    ct);

                if (!result.TimedOut && result.ExitCode is null)
                {
                    infrastructureFailure = true;
                    results.Add(new(i, bytes.Length, null, false, false, Fingerprint(result.StandardError), null));
                    break;
                }

                var crashed = !result.TimedOut && result.ExitCode.HasValue && result.ExitCode.Value != 0;
                var fingerprint = Fingerprint(result.StandardOutput + "\n" + result.StandardError);
                var failure = crashed || result.TimedOut;
                string? inputReference = null;
                if (failure)
                {
                    var kind = crashed ? "crash" : "timeout";
                    var destinationDirectory = crashed ? crashDirectory : timeoutDirectory;
                    var destination = Path.Combine(destinationDirectory, $"{fingerprint}-{i:D5}.bin");
                    await File.WriteAllBytesAsync(destination, bytes, ct);
                    inputReference = RelativeArtifact(destination);
                    if (seenFailureFingerprints.Add(fingerprint))
                    {
                        string? minimizedReference = null;
                        int? minimizedBytes = null;
                        if (command.MinimizeFailures && bytes.Length > 1)
                        {
                            // The minimizer uses the same shared ProcessExecutor support;
                            // ExperimentExecutionSupport has been switched to RunQaAsync below.
                            var minimized = await MinimizeFailureAsync(executable, workspace, command, bytes, fingerprint, crashed, ct);
                            if (minimized.Length < bytes.Length)
                            {
                                var minimizedPath = Path.Combine(destinationDirectory, $"{fingerprint}-{i:D5}.min.bin");
                                await File.WriteAllBytesAsync(minimizedPath, minimized, ct);
                                minimizedReference = RelativeArtifact(minimizedPath);
                                minimizedBytes = minimized.Length;
                            }
                        }
                        failures.Add(new(i, kind, fingerprint, inputReference, minimizedReference, bytes.Length, minimizedBytes));
                    }
                }
                else if (seenOutputFingerprints.Add(fingerprint) && seenOutputFingerprints.Count <= 128)
                {
                    var interesting = Path.Combine(interestingDirectory, $"{fingerprint}-{i:D5}.bin");
                    await File.WriteAllBytesAsync(interesting, bytes, ct);
                    inputReference = RelativeArtifact(interesting);
                }
                results.Add(new(i, bytes.Length, result.ExitCode, result.TimedOut, crashed, fingerprint, inputReference));
            }
        }
        finally
        {
            try
            {
                if (File.Exists(transient))
                    File.Delete(transient);
            }
            catch (IOException ex)
            {
                await File.AppendAllTextAsync(Path.Combine(root, "fuzz-cleanup-warning.log"), ex.Message + Environment.NewLine, CancellationToken.None);
            }
            catch (UnauthorizedAccessException ex)
            {
                await File.AppendAllTextAsync(Path.Combine(root, "fuzz-cleanup-warning.log"), ex.Message + Environment.NewLine, CancellationToken.None);
            }
        }

        var crashes = results.Count(x => x.Crashed);
        var timeouts = results.Count(x => x.TimedOut);
        var unique = results.Where(x => x.Crashed || x.TimedOut)
            .Select(x => x.Fingerprint)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Count();
        var status = infrastructureFailure ? "InfrastructureFailure" : crashes > 0 ? "Failed" : timeouts > 0 ? "Warning" : "Passed";
        var manifest = System.Text.Json.JsonSerializer.Serialize(new
        {
            command.Seed,
            casesRequested = cases,
            casesExecuted = results.Count,
            maxInputBytes = max,
            crashes,
            timeouts,
            uniqueFailureFingerprints = unique,
            seedCorpus = Directory.EnumerateFiles(seedDirectory).Select(RelativeArtifact).ToArray(),
            failures,
            executionBoundary = "ProcessExecutor.RunQaAsync"
        }, new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
        await File.WriteAllTextAsync(Path.Combine(root, "fuzz-manifest.json"), manifest, ct);
        return new(status, results.Count, crashes, timeouts, unique, results, RelativeArtifact(root), failures);
    }
}
