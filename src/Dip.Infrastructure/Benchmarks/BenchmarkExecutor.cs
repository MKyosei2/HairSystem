// Purpose: Executes process benchmarks through the QA sandbox and keeps direct HTTP mode explicit for trusted non-autonomous callers.
using System.Diagnostics;
using System.Net;
using System.Text.Json;
using Dip.Application.Features.Benchmarks;
using Dip.Application.Features.QualityAutomation;
using Dip.Infrastructure.QualityAutomation;

namespace Dip.Infrastructure.Benchmarks;

public sealed class BenchmarkExecutor(ProcessExecutor processExecutor, IQaWorkspaceResolver workspaceResolver) : IBenchmarkExecutor, IDisposable
{
    private readonly HttpClient _http = new(new SocketsHttpHandler { AutomaticDecompression = DecompressionMethods.All })
    {
        Timeout = Timeout.InfiniteTimeSpan
    };

    public async Task<BenchmarkExecutionResult> ExecuteAsync(BenchmarkExecutionSpec spec, CancellationToken ct)
    {
        var warmups = Math.Clamp(spec.WarmupIterations, 0, 20);
        var measurements = Math.Clamp(spec.MeasurementIterations, 3, 500);
        var samples = new List<BenchmarkSample>();
        for (var i = 0; i < warmups; i++)
            await ExecuteOneAsync(spec, -1, ct);
        for (var i = 0; i < measurements; i++)
            samples.Add(await ExecuteOneAsync(spec, i + 1, ct));
        var environment = EnvironmentDescriptor();
        return new(samples, environment.Fingerprint, JsonSerializer.Serialize(new { spec, environment, samples }));
    }

    private async Task<BenchmarkSample> ExecuteOneAsync(BenchmarkExecutionSpec spec, int iteration, CancellationToken ct)
    {
        var sw = Stopwatch.StartNew();
        try
        {
            if (spec.Mode.Equals("http", StringComparison.OrdinalIgnoreCase))
            {
                using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct);
                linked.CancelAfter(TimeSpan.FromSeconds(Math.Clamp(spec.TimeoutSeconds, 1, 600)));
                using var request = new HttpRequestMessage(HttpMethod.Get, spec.Target);
                using var response = await _http.SendAsync(request, HttpCompletionOption.ResponseContentRead, linked.Token);
                sw.Stop();
                return new(iteration, response.IsSuccessStatusCode, false, (int)response.StatusCode,
                    ToMicroseconds(sw.Elapsed), response.IsSuccessStatusCode ? null : $"HTTP {(int)response.StatusCode}");
            }

            if (!spec.Mode.Equals("process", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Benchmark mode must be 'http' or 'process'.");

            var workspace = workspaceResolver.Resolve(spec.WorkspaceRelativePath);
            var executable = spec.Target;
            if (!Path.IsPathRooted(executable))
            {
                var local = Path.GetFullPath(Path.Combine(workspace, executable));
                if (File.Exists(local))
                    executable = local;
            }
            var root = Path.Combine(Path.GetFullPath("artifacts/benchmark-exec"), Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(root);
            var result = await processExecutor.RunQaAsync(
                executable,
                spec.Arguments ?? Array.Empty<string>(),
                workspace,
                null,
                TimeSpan.FromSeconds(Math.Clamp(spec.TimeoutSeconds, 1, 600)),
                Path.Combine(root, "stdout.log"),
                Path.Combine(root, "stderr.log"),
                ct);
            sw.Stop();
            return new(
                iteration,
                result.ExitCode == 0 && !result.TimedOut,
                result.TimedOut,
                result.ExitCode,
                ToMicroseconds(sw.Elapsed),
                result.ExitCode == 0 && !result.TimedOut ? null : result.StandardError);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            sw.Stop();
            return new(iteration, false, true, null, ToMicroseconds(sw.Elapsed), "Benchmark iteration timed out.");
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            sw.Stop();
            return new(iteration, false, false, null, ToMicroseconds(sw.Elapsed), ex.Message);
        }
    }

    private static long ToMicroseconds(TimeSpan elapsed) =>
        (long)Math.Round(elapsed.TotalMilliseconds * 1000d, MidpointRounding.AwayFromZero);

    private static BenchmarkEnvironment EnvironmentDescriptor()
    {
        var machine = Environment.MachineName;
        var os = System.Runtime.InteropServices.RuntimeInformation.OSDescription;
        var osArch = System.Runtime.InteropServices.RuntimeInformation.OSArchitecture.ToString();
        var processArch = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString();
        var framework = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription;
        var cpu = Environment.GetEnvironmentVariable("PROCESSOR_IDENTIFIER") ?? "unknown";
        var processors = Environment.ProcessorCount;
        var text = $"{machine}|{os}|{osArch}|{processArch}|{framework}|{cpu}|{processors}";
        var fingerprint = Convert.ToHexString(
            System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(text))).ToLowerInvariant();
        return new(fingerprint, machine, os, osArch, processArch, framework, cpu, processors);
    }

    private sealed record BenchmarkEnvironment(
        string Fingerprint,
        string Machine,
        string Os,
        string OsArchitecture,
        string ProcessArchitecture,
        string Framework,
        string CpuIdentifier,
        int ProcessorCount);

    public void Dispose() => _http.Dispose();
}
