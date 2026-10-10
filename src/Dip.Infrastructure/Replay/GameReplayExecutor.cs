// Purpose: Executes deterministic game replay targets through the QA sandbox and parses replay metrics.
using System.Globalization;
using System.Text.Json;
using Dip.Application.Abstractions.Replay;
using Dip.Infrastructure.QualityAutomation;
using Microsoft.Extensions.Options;

namespace Dip.Infrastructure.Replay;

public sealed class GameReplayExecutor(ProcessExecutor process, IOptions<GameReplayOptions> options) : IGameReplayExecutor
{
    private readonly GameReplayOptions _options = options.Value;

    public async Task<ReplayExecutionResult> ExecuteAsync(ReplayExecutionRequest request, CancellationToken ct)
    {
        var workspaceRoot = Path.GetFullPath(_options.WorkspaceRoot);
        if (!Directory.Exists(workspaceRoot))
            throw new DirectoryNotFoundException($"Game replay workspace root does not exist: {workspaceRoot}");
        var working = ResolveWorkingDirectory(workspaceRoot, request.WorkingDirectory);
        var executable = ResolveExecutable(workspaceRoot, working, request.Command);
        var root = Path.Combine(Path.GetTempPath(), "dip-replay", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        var stdout = Path.Combine(root, "stdout.log");
        var stderr = Path.Combine(root, "stderr.log");
        var env = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["DIP_REPLAY_MODE"] = "1",
            ["DIP_REPLAY_SCENE"] = request.Scene,
            ["DIP_REPLAY_INPUT_ARTIFACT"] = request.InputArtifactId,
            ["DIP_REPLAY_SEED"] = request.RandomSeed.ToString(CultureInfo.InvariantCulture),
            ["DIP_REPLAY_DURATION_SECONDS"] = request.DurationSeconds.ToString(CultureInfo.InvariantCulture),
            ["DIP_REPLAY_COMMIT"] = request.CommitSha,
            ["DIP_REPLAY_BUILD_ID"] = request.BuildId,
            ["DIP_REPLAY_HARDWARE_FINGERPRINT"] = request.HardwareFingerprint
        };
        var result = await process.RunQaAsync(
            executable,
            SplitArguments(request.Arguments),
            working,
            env,
            TimeSpan.FromSeconds(request.DurationSeconds + 60),
            stdout,
            stderr,
            ct);
        var samples = ParseSamples(result.StandardOutput);
        var crashed = !result.TimedOut && result.ExitCode.HasValue && result.ExitCode.Value != 0;
        var succeeded = !result.TimedOut && result.ExitCode == 0;
        return new(
            succeeded,
            result.ExitCode ?? -1,
            crashed,
            result.TimedOut,
            result.StandardOutput,
            result.StandardError,
            samples,
            request.HardwareFingerprint);
    }

    private string ResolveExecutable(string workspaceRoot, string working, string command)
    {
        if (string.IsNullOrWhiteSpace(command))
            throw new ArgumentException("Replay launch command is required.", nameof(command));
        var value = command.Trim();
        if (_options.AllowPathLookup
            && !value.Contains(Path.DirectorySeparatorChar)
            && !value.Contains(Path.AltDirectorySeparatorChar))
            return ProcessExecutor.FindExecutable(value)
                   ?? throw new FileNotFoundException($"Replay executable '{value}' was not found on PATH.");
        var candidate = Path.IsPathRooted(value)
            ? Path.GetFullPath(value)
            : Path.GetFullPath(Path.Combine(working, value));
        EnsureInsideRoot(candidate, workspaceRoot, "Replay executable");
        if (!File.Exists(candidate))
            throw new FileNotFoundException($"Replay executable does not exist: {candidate}", candidate);
        return candidate;
    }

    private static string ResolveWorkingDirectory(string workspaceRoot, string? configured)
    {
        var candidate = string.IsNullOrWhiteSpace(configured)
            ? workspaceRoot
            : Path.IsPathRooted(configured)
                ? Path.GetFullPath(configured)
                : Path.GetFullPath(Path.Combine(workspaceRoot, configured));
        EnsureInsideRoot(candidate, workspaceRoot, "Replay working directory");
        if (!Directory.Exists(candidate))
            throw new DirectoryNotFoundException(candidate);
        return candidate;
    }

    private static void EnsureInsideRoot(string candidate, string root, string label)
    {
        var relative = Path.GetRelativePath(root, candidate);
        if (relative == ".."
            || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
            || Path.IsPathRooted(relative))
            throw new UnauthorizedAccessException($"{label} must stay inside configured GameReplay:WorkspaceRoot.");
    }

    private static IReadOnlyList<ReplaySample> ParseSamples(string stdout)
    {
        var result = new List<ReplaySample>();
        foreach (var line in stdout.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries))
        {
            var trimmed = line.Trim();
            var markerIndex = trimmed.IndexOf("DIP_REPLAY_METRIC ", StringComparison.Ordinal);
            if (markerIndex >= 0)
            {
                try
                {
                    using var doc = JsonDocument.Parse(trimmed[(markerIndex + 18)..]);
                    var root = doc.RootElement;
                    var name = root.GetProperty("name").GetString();
                    var unit = root.GetProperty("unit").GetString();
                    if (name is not null && unit is not null && root.GetProperty("value").TryGetDecimal(out var value))
                        result.Add(new(name, value, unit));
                }
                catch (JsonException)
                {
                    // Malformed application telemetry is ignored as a sample, not treated as process success evidence.
                }
            }
            else
            {
                markerIndex = trimmed.IndexOf("DIP_METRIC ", StringComparison.Ordinal);
                if (markerIndex < 0)
                    continue;
                var parts = trimmed[(markerIndex + 11)..]
                    .Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
                string? name = null, unit = null;
                decimal? value = null;
                foreach (var part in parts)
                {
                    var pair = part.Split('=', 2);
                    if (pair.Length != 2)
                        continue;
                    if (pair[0] == "name") name = pair[1];
                    else if (pair[0] == "unit") unit = pair[1];
                    else if (pair[0] == "value"
                             && decimal.TryParse(pair[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var v))
                        value = v;
                }
                if (name is not null && unit is not null && value.HasValue)
                    result.Add(new(name, value.Value, unit));
            }
        }
        return result;
    }

    private static IReadOnlyList<string> SplitArguments(string arguments)
    {
        var result = new List<string>();
        if (string.IsNullOrWhiteSpace(arguments))
            return result;
        var current = new System.Text.StringBuilder();
        var quoted = false;
        for (var i = 0; i < arguments.Length; i++)
        {
            var c = arguments[i];
            if (c == '"')
            {
                quoted = !quoted;
                continue;
            }
            if (char.IsWhiteSpace(c) && !quoted)
            {
                if (current.Length > 0)
                {
                    result.Add(current.ToString());
                    current.Clear();
                }
                continue;
            }
            current.Append(c);
        }
        if (current.Length > 0)
            result.Add(current.ToString());
        if (quoted)
            throw new ArgumentException("Replay arguments contain an unmatched quote.", nameof(arguments));
        return result;
    }
}
