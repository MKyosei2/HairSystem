// Purpose: Runs sustained soak experiments through the QA sandbox and captures resource/runtime drift telemetry.
using System.Globalization;
using System.Text.Json;
using Dip.Application.Features.P2Intelligence;
using Dip.Application.Features.QualityAutomation;
using Dip.Infrastructure.QualityAutomation;

namespace Dip.Infrastructure.P2Intelligence;

internal sealed class SoakExperimentRunner(ProcessExecutor executor, IQaWorkspaceResolver workspaceResolver)
    : ExperimentExecutionSupport(executor, workspaceResolver)
{
    public async Task<SoakResult> RunSoakAsync(SoakCommand command, CancellationToken ct)
    {
        var workspace = workspaceResolver.Resolve(command.Target.WorkspaceRelativePath);
        var executable = ResolveTargetExecutable(workspace, command.Target.Executable);
        var root = ArtifactRoot(command.Target.ProjectId, "soak");
        var stdoutPath = Path.Combine(root, "stdout.log");
        var stderrPath = Path.Combine(root, "stderr.log");
        var duration = Math.Clamp(command.DurationSeconds, 5, 86400);
        var interval = Math.Clamp(command.SampleIntervalSeconds, 1, 60);

        // A soak run that survives the configured observation window is intentionally
        // terminated by the harness. RunQaObservedAsync preserves that semantics while
        // applying the same sandbox boundary as every other autonomous QA target.
        var result = await executor.RunQaObservedAsync(
            executable,
            command.Target.Arguments ?? Array.Empty<string>(),
            workspace,
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["DIP_SOAK_MODE"] = "1",
                ["DIP_SOAK_DURATION_SECONDS"] = duration.ToString(CultureInfo.InvariantCulture)
            },
            TimeSpan.FromSeconds(duration),
            stdoutPath,
            stderrPath,
            TimeSpan.FromSeconds(interval),
            ct);

        var resource = result.ResourceSamples ?? Array.Empty<ProcessResourceSample>();
        var runtime = ParseRuntimeSamples(result.StandardOutput);
        var samples = MergeSamples(resource, runtime, interval);

        var startMemory = samples.Count > 0
            ? samples[0].WorkingSetMb
            : Math.Max(0, result.Sandbox?.PeakWorkingSetMb ?? 0);
        var endMemory = samples.Count > 0
            ? samples[^1].WorkingSetMb
            : startMemory;
        var memoryGrowth = startMemory <= 0
            ? 0m
            : decimal.Round((decimal)(endMemory - startMemory) / startMemory * 100m, 2);

        var allFps = runtime.Select(x => x.Fps).Where(x => x.HasValue && x.Value > 0).Select(x => x!.Value).ToArray();
        decimal? startFps = allFps.Length > 0
            ? allFps.Take(Math.Max(1, allFps.Length / 4)).Average()
            : null;
        decimal? endFps = allFps.Length > 0
            ? allFps.Skip(Math.Max(0, allFps.Length * 3 / 4)).DefaultIfEmpty().Average()
            : null;
        decimal? drift = startFps is > 0m && endFps is not null
            ? decimal.Round((startFps.Value - endFps.Value) / startFps.Value * 100m, 2)
            : null;

        // Timeout is expected when the harness reaches the full observation window.
        // A process exit before the requested duration or a sandbox/resource termination
        // is a failure and is not upgraded to PASS.
        var observedSeconds = result.DurationMs / 1000d;
        var reachedWindow = observedSeconds >= duration - Math.Max(1, interval);
        var sandboxResourceFailure = result.ExitCode is null
                                     && !result.TimedOut
                                     && result.Sandbox is { Enabled: true }
                                     && result.Sandbox.Detail.Contains("budget exceeded", StringComparison.OrdinalIgnoreCase);
        var crashed = !result.TimedOut && result.ExitCode is not null && result.ExitCode.Value != 0;
        var earlyExit = !result.TimedOut && observedSeconds < duration - Math.Max(1, interval);

        string status;
        if (sandboxResourceFailure || crashed || earlyExit)
            status = "Failed";
        else if (!reachedWindow)
            status = "InfrastructureFailure";
        else if (memoryGrowth >= command.MemoryGrowthWarningPercent
                 || drift >= command.PerformanceDriftWarningPercent)
            status = "Warning";
        else
            status = "Passed";

        var manifest = JsonSerializer.Serialize(new
        {
            status,
            observationSeconds = observedSeconds,
            requestedSeconds = duration,
            reachedWindow,
            memoryGrowthPercent = memoryGrowth,
            performanceDriftPercent = drift,
            sandbox = result.Sandbox,
            sampleCount = samples.Count
        }, new JsonSerializerOptions { WriteIndented = true });
        await File.WriteAllTextAsync(Path.Combine(root, "soak-manifest.json"), manifest, CancellationToken.None);

        return new(
            status,
            startMemory,
            endMemory,
            memoryGrowth,
            startFps,
            endFps,
            drift,
            crashed || sandboxResourceFailure || earlyExit,
            result.TimedOut && !reachedWindow,
            samples,
            RelativeArtifact(root));
    }

    private static IReadOnlyList<SoakTelemetryPoint> MergeSamples(
        IReadOnlyList<ProcessResourceSample> resource,
        IReadOnlyList<RuntimeMetricSample> runtime,
        int intervalSeconds)
    {
        if (resource.Count == 0)
            return Array.Empty<SoakTelemetryPoint>();
        var result = new List<SoakTelemetryPoint>(resource.Count);
        foreach (var sample in resource)
        {
            var second = Math.Max(0, sample.ElapsedMilliseconds / 1000);
            var nearest = runtime
                .Where(x => Math.Abs(x.Second - second) <= intervalSeconds * 2)
                .OrderBy(x => Math.Abs(x.Second - second))
                .FirstOrDefault();
            result.Add(new(
                second,
                sample.WorkingSetMb,
                sample.CpuPercent,
                nearest?.Fps,
                nearest?.FrameTimeMs));
        }
        return result;
    }

    private static IReadOnlyList<RuntimeMetricSample> ParseRuntimeSamples(string stdout)
    {
        var result = new List<RuntimeMetricSample>();
        var ordinal = 0;
        foreach (var line in stdout.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries))
        {
            var trimmed = line.Trim();
            if (trimmed.StartsWith("DIP_SOAK_METRIC ", StringComparison.Ordinal))
            {
                try
                {
                    using var doc = JsonDocument.Parse(trimmed[16..]);
                    var root = doc.RootElement;
                    var second = root.TryGetProperty("second", out var s) && s.TryGetInt32(out var sec) ? sec : ordinal;
                    decimal? fps = root.TryGetProperty("fps", out var f) && f.TryGetDecimal(out var fv) ? fv : null;
                    decimal? frame = root.TryGetProperty("frame_time_ms", out var ft) && ft.TryGetDecimal(out var ftv) ? ftv : null;
                    result.Add(new(second, fps, frame));
                    ordinal++;
                    continue;
                }
                catch (JsonException)
                {
                    // Fall through to the text metric parser.
                }
            }

            var marker = trimmed.IndexOf("DIP_METRIC ", StringComparison.Ordinal);
            if (marker < 0)
                continue;
            string? name = null;
            decimal? value = null;
            foreach (var part in trimmed[(marker + 11)..].Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                var pair = part.Split('=', 2);
                if (pair.Length != 2)
                    continue;
                if (pair[0] == "name") name = pair[1];
                else if (pair[0] == "value"
                         && decimal.TryParse(pair[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed))
                    value = parsed;
            }
            if (value is null)
                continue;
            if (string.Equals(name, "fps", StringComparison.OrdinalIgnoreCase))
                result.Add(new(ordinal++, value, null));
            else if (string.Equals(name, "frame_time_ms", StringComparison.OrdinalIgnoreCase))
                result.Add(new(ordinal++, null, value));
        }
        return result;
    }

    private sealed record RuntimeMetricSample(int Second, decimal? Fps, decimal? FrameTimeMs);
}
