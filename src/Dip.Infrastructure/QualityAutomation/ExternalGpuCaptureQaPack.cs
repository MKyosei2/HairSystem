// Purpose: Executes explicitly configured RenderDoc/Nsight-style GPU capture/profiling commands through the QA sandbox.
using System.Text.Json;
using Dip.Application.Features.QualityAutomation;
using Dip.Domain.Enums;

namespace Dip.Infrastructure.QualityAutomation;

public sealed class ExternalGpuCaptureQaPack(ProcessExecutor executor) : ICrossCuttingQaPack
{
    public string PackId => "gpu-capture";

    public bool Applies(QaTechnologyProfile profile, string workspace) =>
        profile.GameEngines.Count > 0 && File.Exists(Path.Combine(workspace, ".dip", "gpu-qa.json"));

    public async Task<QaCrossCuttingResult> RunAsync(
        QaExecutionCommand command,
        QaTechnologyProfile profile,
        string workspace,
        string artifactRoot,
        CancellationToken ct)
    {
        Directory.CreateDirectory(artifactRoot);
        var manifest = Path.Combine(workspace, ".dip", "gpu-qa.json");
        GpuQaManifest? spec;
        try
        {
            spec = JsonSerializer.Deserialize<GpuQaManifest>(
                await File.ReadAllTextAsync(manifest, ct),
                new JsonSerializerOptions(JsonSerializerDefaults.Web) { PropertyNameCaseInsensitive = true });
        }
        catch (JsonException ex)
        {
            var finding = F(Severity.Critical, "GPU QA manifest is invalid", ex.Message, ".dip/gpu-qa.json", "DIP-GPU-MANIFEST");
            return new(PackId, [new("gpu-capture-manifest", true, false, 1, 0, "", "", ex.Message)], [finding], JsonSerializer.Serialize(new { error = ex.Message }));
        }

        if (spec is null || string.IsNullOrWhiteSpace(spec.Tool))
        {
            var finding = F(Severity.Critical, "GPU QA tool is not configured", "Set tool and arguments in .dip/gpu-qa.json.", ".dip/gpu-qa.json", "DIP-GPU-CONFIG");
            return new(PackId, [new("gpu-capture", false, false, null, 0, "", "", "GPU QA tool is not configured.")], [finding], JsonSerializer.Serialize(new { configured = false }));
        }

        var tool = ProcessExecutor.FindExecutable(spec.Tool);
        if (tool is null)
            return new(PackId, [new(spec.Tool, false, false, null, 0, "", "", $"{spec.Tool} is unavailable.")], [], JsonSerializer.Serialize(new { configured = true, available = false, spec.Tool }));

        var result = await executor.RunQaAsync(
            tool,
            spec.Arguments ?? Array.Empty<string>(),
            workspace,
            new Dictionary<string, string> { ["DIP_GPU_QA"] = "1" },
            TimeSpan.FromSeconds(Math.Clamp(spec.TimeoutSeconds, 5, 1800)),
            Path.Combine(artifactRoot, "stdout.log"),
            Path.Combine(artifactRoot, "stderr.log"),
            ct);

        var succeeded = !result.TimedOut && result.ExitCode == 0;
        var findings = succeeded
            ? Array.Empty<QaFindingCandidate>()
            : [F(Severity.Critical, "GPU capture/profiling command failed", result.StandardError, ".dip/gpu-qa.json", "DIP-GPU-CAPTURE")];
        var sandboxJson = JsonSerializer.Serialize(result.Sandbox);
        var qaTool = new QaToolExecution(
            $"gpu-capture:{spec.Tool}",
            true,
            succeeded,
            result.ExitCode,
            result.DurationMs,
            result.StandardOutputPath,
            result.StandardErrorPath,
            succeeded ? "GPU capture/profiling command completed." : "GPU capture/profiling command failed.",
            tool,
            null,
            sandboxJson,
            result.CpuTimeMs);
        var summary = JsonSerializer.Serialize(new
        {
            spec.Tool,
            result.ExitCode,
            result.TimedOut,
            result.DurationMs,
            sandbox = result.Sandbox,
            output = Clip(result.StandardOutput, 16000),
            error = Clip(result.StandardError, 16000)
        });
        await File.WriteAllTextAsync(Path.Combine(artifactRoot, "gpu-qa.json"), summary, ct);
        return new(PackId, [qaTool], findings, summary);
    }

    private static QaFindingCandidate F(Severity severity, string title, string message, string file, string rule) =>
        new("gpu", severity, "dip-gpu-qa", title, Clip(message, 4000), file, RuleId: rule, SourceFormat: "dip-gpu-qa", SourceFormatVersion: "1.0");

    private static string Clip(string? value, int max)
    {
        var text = value ?? string.Empty;
        return text.Length <= max ? text : text[..max];
    }

    private sealed record GpuQaManifest(string? Tool, IReadOnlyList<string>? Arguments = null, int TimeoutSeconds = 300);
}
