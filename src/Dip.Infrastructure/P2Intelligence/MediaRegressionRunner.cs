// Purpose: Runs visual/video/audio regression through the bounded QA process sandbox.
using System.Text.Json;
using Dip.Application.Features.P2Intelligence;
using Dip.Application.Features.QualityAutomation;
using Dip.Infrastructure.QualityAutomation;

namespace Dip.Infrastructure.P2Intelligence;

public sealed class MediaRegressionRunner(
    ProcessExecutor executor,
    IQaWorkspaceResolver workspaceResolver) : IMediaRegressionRunner
{
    public async Task<MediaRegressionResult> AnalyzeAsync(MediaRegressionCommand command, CancellationToken ct)
    {
        var workspace = workspaceResolver.Resolve(command.WorkspaceRelativePath);
        var baseline = SafePath(workspace, command.BaselinePath);
        var current = SafePath(workspace, command.CurrentPath);
        if (!File.Exists(baseline) || !File.Exists(current))
            return Failure(command.Type, "One or both media inputs do not exist.", "");

        var ffmpeg = ProcessExecutor.FindExecutable("ffmpeg");
        var ffprobe = ProcessExecutor.FindExecutable("ffprobe");
        if (ffmpeg is null || ffprobe is null)
            return Failure(command.Type, "ffmpeg and ffprobe are required for P2 media regression.", "");

        // Every path passed to ffmpeg/ffprobe stays inside the trusted QA workspace.
        // The autonomous dispatcher imports this scratch directory into durable Artifact Store
        // before the disposable worktree is removed.
        var root = Path.Combine(workspace, ".dipqa", "media", command.Type.ToString().ToLowerInvariant(), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        return command.Type switch
        {
            MediaRegressionType.Visual => await AnalyzeVisualAsync(command, ffmpeg, ffprobe, baseline, current, workspace, root, ct),
            MediaRegressionType.Video => await AnalyzeVideoAsync(command, ffmpeg, ffprobe, baseline, current, workspace, root, ct),
            MediaRegressionType.Audio => await AnalyzeAudioAsync(command, ffmpeg, baseline, current, workspace, root, ct),
            _ => Failure(command.Type, "Unsupported media regression type.", root)
        };
    }

    private async Task<MediaRegressionResult> AnalyzeVisualAsync(
        MediaRegressionCommand command, string ffmpeg, string ffprobe, string baseline, string current, string workspace, string root, CancellationToken ct)
    {
        var sizeA = await ProbeSizeAsync(ffprobe, baseline, workspace, root, ct);
        var sizeB = await ProbeSizeAsync(ffprobe, current, workspace, root, ct);
        if (sizeA is null || sizeB is null) return Failure(command.Type, "Could not read image dimensions.", root);
        if (sizeA != sizeB)
        {
            var detail = JsonSerializer.Serialize(new { baseline = sizeA, current = sizeB, reason = "dimension-mismatch" });
            return Build(command, 100m, 98m, "Image dimensions differ.", root, detail, "0s");
        }

        var a = Path.Combine(root, "baseline.rgb");
        var b = Path.Combine(root, "current.rgb");
        if (!await DecodeFrameAsync(ffmpeg, baseline, a, null, workspace, root, ct)
            || !await DecodeFrameAsync(ffmpeg, current, b, null, workspace, root, ct))
            return Failure(command.Type, "Could not decode one or both images.", root);

        var diff = CompareRgb(
            await File.ReadAllBytesAsync(a, ct),
            await File.ReadAllBytesAsync(b, ct),
            sizeA.Value.Width,
            sizeA.Value.Height,
            Path.Combine(root, "difference.ppm"));
        var comparisonDetail = JsonSerializer.Serialize(new
        {
            sizeA.Value.Width,
            sizeA.Value.Height,
            diff.MeanDifferencePercent,
            diff.ChangedPixelPercent,
            diff.MaxChannelDifference,
            sandboxed = true
        });
        return Build(command, diff.MeanDifferencePercent, 99m,
            $"{diff.ChangedPixelPercent:0.##}% of pixels changed.", root, comparisonDetail, "0s");
    }

    private async Task<MediaRegressionResult> AnalyzeVideoAsync(
        MediaRegressionCommand command, string ffmpeg, string ffprobe, string baseline, string current, string workspace, string root, CancellationToken ct)
    {
        var durationA = await ProbeDurationAsync(ffprobe, baseline, workspace, root, ct);
        var durationB = await ProbeDurationAsync(ffprobe, current, workspace, root, ct);
        var sizeA = await ProbeSizeAsync(ffprobe, baseline, workspace, root, ct);
        var sizeB = await ProbeSizeAsync(ffprobe, current, workspace, root, ct);
        if (durationA is null || durationB is null || sizeA is null || sizeB is null)
            return Failure(command.Type, "Could not probe video metadata.", root);
        if (sizeA != sizeB)
        {
            var detail = JsonSerializer.Serialize(new { baseline = sizeA, current = sizeB, durationA, durationB, reason = "dimension-mismatch" });
            return Build(command, 100m, 95m, "Video dimensions differ.", root, detail, "0s");
        }

        var sampleFps = Math.Clamp(command.VideoSampleFps, 1, 10);
        var interval = 1d / sampleFps;
        var duration = Math.Min(durationA.Value, durationB.Value);
        var frameCount = Math.Clamp((int)Math.Ceiling(duration * sampleFps), 1, 3600);
        decimal total = 0m, max = 0m;
        var compared = 0;
        string? first = null;
        var frameDir = Path.Combine(root, "frames");
        Directory.CreateDirectory(frameDir);

        for (var i = 0; i < frameCount; i++)
        {
            ct.ThrowIfCancellationRequested();
            var at = Math.Min(duration, i * interval);
            var a = Path.Combine(frameDir, $"a-{i:D5}.rgb");
            var b = Path.Combine(frameDir, $"b-{i:D5}.rgb");
            if (!await DecodeFrameAsync(ffmpeg, baseline, a, at, workspace, root, ct)
                || !await DecodeFrameAsync(ffmpeg, current, b, at, workspace, root, ct))
                continue;
            var d = CompareRgb(await File.ReadAllBytesAsync(a, ct), await File.ReadAllBytesAsync(b, ct), sizeA.Value.Width, sizeA.Value.Height, null);
            total += d.MeanDifferencePercent;
            max = Math.Max(max, d.MeanDifferencePercent);
            compared++;
            if (first is null && d.MeanDifferencePercent >= command.WarningThresholdPercent)
                first = $"{at:0.00}s";
            File.Delete(a);
            File.Delete(b);
        }

        if (compared == 0) return Failure(command.Type, "No video frames could be compared.", root);
        var average = decimal.Round(total / compared, 3);
        var durationDelta = Math.Abs(durationA.Value - durationB.Value);
        if (durationDelta > .25)
            average = Math.Min(100m, average + (decimal)Math.Min(20d, durationDelta * 2d));
        var comparisonDetail = JsonSerializer.Serialize(new
        {
            samples = compared,
            averageDifferencePercent = average,
            maxDifferencePercent = max,
            durationA,
            durationB,
            durationDeltaSeconds = durationDelta,
            firstDifference = first,
            sandboxed = true
        });
        return Build(command, average, Math.Min(98m, 75m + compared / 10m),
            $"Compared {compared} sampled frame(s). Maximum sampled difference {max:0.##}%.", root, comparisonDetail, first);
    }

    private async Task<MediaRegressionResult> AnalyzeAudioAsync(
        MediaRegressionCommand command, string ffmpeg, string baseline, string current, string workspace, string root, CancellationToken ct)
    {
        var a = Path.Combine(root, "baseline.s16le");
        var b = Path.Combine(root, "current.s16le");
        if (!await DecodeAudioAsync(ffmpeg, baseline, a, workspace, root, ct)
            || !await DecodeAudioAsync(ffmpeg, current, b, workspace, root, ct))
            return Failure(command.Type, "Could not decode one or both audio inputs.", root);
        var metrics = CompareAudio(await File.ReadAllBytesAsync(a, ct), await File.ReadAllBytesAsync(b, ct));
        return Build(command, metrics.DifferencePercent, 96m,
            $"RMS baseline {metrics.BaselineRms:0.###}, current {metrics.CurrentRms:0.###}; silence delta {metrics.SilenceDeltaPercent:0.##}%.",
            root, JsonSerializer.Serialize(new { metrics, sandboxed = true }), null);
    }

    private static MediaRegressionResult Build(MediaRegressionCommand command, decimal difference, decimal confidence, string summary, string artifact, string detail, string? first)
    {
        var status = difference >= command.FailureThresholdPercent ? "Failed" : difference >= command.WarningThresholdPercent ? "Warning" : "Passed";
        return new(status, command.Type, decimal.Round(Math.Clamp(difference, 0m, 100m), 3), confidence, first, summary, artifact, detail);
    }

    private static MediaRegressionResult Failure(MediaRegressionType type, string summary, string artifact) =>
        new("InfrastructureFailure", type, 0m, 0m, null, summary, artifact, JsonSerializer.Serialize(new { error = summary }));

    private async Task<(int Width, int Height)?> ProbeSizeAsync(string ffprobe, string file, string workspace, string root, CancellationToken ct)
    {
        var id = Guid.NewGuid().ToString("N");
        var r = await executor.RunQaAsync(ffprobe,
            ["-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height", "-of", "csv=s=x:p=0", file],
            workspace, null, TimeSpan.FromSeconds(20), Path.Combine(root, $"probe-{id}.out"), Path.Combine(root, $"probe-{id}.err"), ct);
        var parts = r.StandardOutput.Trim().Split('x');
        return r.ExitCode == 0 && parts.Length == 2 && int.TryParse(parts[0], out var w) && int.TryParse(parts[1], out var h) ? (w, h) : null;
    }

    private async Task<double?> ProbeDurationAsync(string ffprobe, string file, string workspace, string root, CancellationToken ct)
    {
        var id = Guid.NewGuid().ToString("N");
        var r = await executor.RunQaAsync(ffprobe,
            ["-v", "error", "-show_entries", "format=duration", "-of", "default=noprint_wrappers=1:nokey=1", file],
            workspace, null, TimeSpan.FromSeconds(20), Path.Combine(root, $"duration-{id}.out"), Path.Combine(root, $"duration-{id}.err"), ct);
        return r.ExitCode == 0 && double.TryParse(r.StandardOutput.Trim(), System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out var d) ? d : null;
    }

    private async Task<bool> DecodeFrameAsync(string ffmpeg, string input, string output, double? at, string workspace, string root, CancellationToken ct)
    {
        var args = new List<string> { "-y", "-v", "error" };
        if (at is not null)
        {
            args.Add("-ss");
            args.Add(at.Value.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture));
        }
        args.AddRange(["-i", input, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", output]);
        var id = Guid.NewGuid().ToString("N");
        var r = await executor.RunQaAsync(ffmpeg, args, workspace, null, TimeSpan.FromSeconds(30),
            Path.Combine(root, $"ffmpeg-{id}.out"), Path.Combine(root, $"ffmpeg-{id}.err"), ct);
        return r.ExitCode == 0 && File.Exists(output) && new FileInfo(output).Length > 0;
    }

    private async Task<bool> DecodeAudioAsync(string ffmpeg, string input, string output, string workspace, string root, CancellationToken ct)
    {
        var id = Guid.NewGuid().ToString("N");
        var r = await executor.RunQaAsync(ffmpeg,
            ["-y", "-v", "error", "-i", input, "-vn", "-ac", "1", "-ar", "16000", "-f", "s16le", output],
            workspace, null, TimeSpan.FromMinutes(2), Path.Combine(root, $"audio-{id}.out"), Path.Combine(root, $"audio-{id}.err"), ct);
        return r.ExitCode == 0 && File.Exists(output) && new FileInfo(output).Length > 0;
    }

    private static (decimal MeanDifferencePercent, decimal ChangedPixelPercent, int MaxChannelDifference) CompareRgb(byte[] a, byte[] b, int width, int height, string? diffPath)
    {
        var length = Math.Min(a.Length, b.Length);
        if (length == 0) return (100m, 100m, 255);
        var pixelCount = length / 3;
        if (pixelCount == 0) return (100m, 100m, 255);
        long total = 0, changedPixels = 0;
        var max = 0;
        byte[]? diff = diffPath is null ? null : new byte[length];
        for (var pixel = 0; pixel < pixelCount; pixel++)
        {
            var pixelChanged = false;
            for (var channel = 0; channel < 3; channel++)
            {
                var i = pixel * 3 + channel;
                var d = Math.Abs(a[i] - b[i]);
                total += d;
                max = Math.Max(max, d);
                if (d >= 8) pixelChanged = true;
                if (diff is not null) diff[i] = (byte)Math.Clamp(d * 4, 0, 255);
            }
            if (pixelChanged) changedPixels++;
        }
        if (diff is not null)
        {
            using var fs = File.Create(diffPath!);
            var header = System.Text.Encoding.ASCII.GetBytes($"P6\n{width} {height}\n255\n");
            fs.Write(header);
            fs.Write(diff);
        }
        var mean = decimal.Round((decimal)total / (pixelCount * 3m * 255m) * 100m, 3);
        var changedPct = decimal.Round((decimal)changedPixels / pixelCount * 100m, 3);
        return (mean, changedPct, max);
    }

    private sealed record AudioMetrics(decimal DifferencePercent, decimal BaselineRms, decimal CurrentRms, decimal SilenceDeltaPercent, decimal ClipDeltaPercent);

    private static AudioMetrics CompareAudio(byte[] a, byte[] b)
    {
        var sa = ToSamples(a);
        var sb = ToSamples(b);
        var n = Math.Min(sa.Length, sb.Length);
        if (n == 0) return new(100m, 0, 0, 100m, 0);
        double sqA = 0, sqB = 0, diff = 0;
        var silenceA = 0; var silenceB = 0; var clipA = 0; var clipB = 0;
        for (var i = 0; i < n; i++)
        {
            var aa = sa[i] / 32768d;
            var bb = sb[i] / 32768d;
            sqA += aa * aa; sqB += bb * bb; diff += Math.Abs(aa - bb);
            if (Math.Abs(aa) < .005) silenceA++;
            if (Math.Abs(bb) < .005) silenceB++;
            if (Math.Abs(aa) > .99) clipA++;
            if (Math.Abs(bb) > .99) clipB++;
        }
        var rmsA = Math.Sqrt(sqA / n);
        var rmsB = Math.Sqrt(sqB / n);
        var difference = (decimal)Math.Min(1d, diff / n) * 100m;
        if (Math.Abs(sa.Length - sb.Length) > n * .02) difference = Math.Min(100m, difference + 10m);
        var silence = Math.Abs((decimal)silenceA / n * 100m - (decimal)silenceB / n * 100m);
        var clip = Math.Abs((decimal)clipA / n * 100m - (decimal)clipB / n * 100m);
        return new(decimal.Round(difference, 3), decimal.Round((decimal)rmsA, 4), decimal.Round((decimal)rmsB, 4), decimal.Round(silence, 3), decimal.Round(clip, 3));
    }

    private static short[] ToSamples(byte[] bytes)
    {
        var samples = new short[bytes.Length / 2];
        for (var i = 0; i < samples.Length; i++) samples[i] = BitConverter.ToInt16(bytes, i * 2);
        return samples;
    }

    private static string SafePath(string root, string relative)
    {
        if (Path.IsPathRooted(relative)) throw new InvalidOperationException("Media path must be workspace-relative.");
        var full = Path.GetFullPath(Path.Combine(root, relative));
        var prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        if (!full.StartsWith(prefix, comparison)) throw new InvalidOperationException("Media path escapes the configured workspace.");
        return full;
    }

}
