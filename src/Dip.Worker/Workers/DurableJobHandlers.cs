// Purpose: Implements Durable Job Handlers responsibilities for this repository area.
using System.Text.Json;
using Dip.Application.Features.Artifacts;
using Dip.Application.Features.Benchmarks;
using Dip.Application.Features.Incidents;
using Dip.Application.Features.Jobs;
using Dip.Application.Features.P2Intelligence;
using Dip.Application.Features.QualityAutomation;
using Dip.Domain.Entities;
using Dip.Domain.Enums;

namespace Dip.Worker.Workers;

internal static class JobPayload
{
    public static T Read<T>(DurableJob job) => JsonSerializer.Deserialize<T>(job.PayloadJson, new JsonSerializerOptions(JsonSerializerDefaults.Web) { PropertyNameCaseInsensitive = true }) ?? throw new InvalidDataException($"Job {job.Id} payload cannot be deserialized as {typeof(T).Name}.");
    public static string Json(object value) => JsonSerializer.Serialize(value, new JsonSerializerOptions(JsonSerializerDefaults.Web));
}

public sealed class FuzzJobHandler(P2TechnicalIntelligenceService technical, ArtifactService artifacts, IncidentAggregationService incidents) : IDurableJobHandler
{
    public DurableJobType Type => DurableJobType.Fuzz;
    public async Task<DurableJobExecutionResult> ExecuteAsync(DurableJob job, CancellationToken ct)
    {
        var command = JobPayload.Read<FuzzCommand>(job);
        var result = await technical.RunFuzzAsync(command, ct);
        var imported = await artifacts.ImportPathAsync(job.ProjectId, "fuzz", result.ArtifactReference, new ArtifactLinkContext(job.Id), ct);
        if (result.Crashes > 0 || result.Timeouts > 0)
        {
            var fp = result.Failures?.FirstOrDefault()?.Fingerprint ?? $"fuzz:{job.SubjectKey}";
            var incident = await incidents.ObserveSyntheticAsync(job.ProjectId, job.SubjectKey, $"fuzz:{fp}", $"Fuzz failure: {job.SubjectKey}", result.Crashes > 0 ? Severity.Critical : Severity.Warning, JobPayload.Json(result), job.Id, ct);
            foreach (var a in imported) await incidents.AttachArtifactAsync(incident.Id, a.Id, a.FileName, ct);
        }
        return new(JobPayload.Json(result), result.ArtifactReference);
    }
}

public sealed class MutationJobHandler(P2TechnicalIntelligenceService technical, ArtifactService artifacts) : IDurableJobHandler
{
    public DurableJobType Type => DurableJobType.Mutation;
    public async Task<DurableJobExecutionResult> ExecuteAsync(DurableJob job, CancellationToken ct)
    {
        var command = JobPayload.Read<MutationCommand>(job);
        var result = await technical.RunMutationAsync(command, ct);
        await artifacts.ImportPathAsync(job.ProjectId, "mutation", result.ArtifactReference, new ArtifactLinkContext(job.Id), ct);
        return new(JobPayload.Json(result), result.ArtifactReference);
    }
}

public sealed class SoakJobHandler(P2TechnicalIntelligenceService technical, ArtifactService artifacts, IncidentAggregationService incidents) : IDurableJobHandler
{
    public DurableJobType Type => DurableJobType.Soak;
    public async Task<DurableJobExecutionResult> ExecuteAsync(DurableJob job, CancellationToken ct)
    {
        var command = JobPayload.Read<SoakCommand>(job);
        var result = await technical.RunSoakAsync(command, ct);
        var imported = await artifacts.ImportPathAsync(job.ProjectId, "soak", result.ArtifactReference, new ArtifactLinkContext(job.Id), ct);
        if ((result.Status is "Failed" or "Warning") || result.Crashed || result.TimedOut)
        {
            var incident = await incidents.ObserveSyntheticAsync(job.ProjectId, job.SubjectKey, $"soak:{job.SubjectKey}", $"Soak reliability regression: {job.SubjectKey}", result.Crashed ? Severity.Critical : Severity.Warning, JobPayload.Json(result), job.Id, ct);
            foreach (var a in imported) await incidents.AttachArtifactAsync(incident.Id, a.Id, a.FileName, ct);
        }
        return new(JobPayload.Json(result), result.ArtifactReference);
    }
}

public sealed class GitBisectJobHandler(P2TechnicalIntelligenceService technical, ArtifactService artifacts) : IDurableJobHandler
{
    public DurableJobType Type => DurableJobType.GitBisect;
    public async Task<DurableJobExecutionResult> ExecuteAsync(DurableJob job, CancellationToken ct)
    {
        var command = JobPayload.Read<GitBisectCommand>(job);
        var result = await technical.FindRegressionCommitAsync(command, ct);
        await artifacts.ImportPathAsync(job.ProjectId, "git-bisect", result.ArtifactReference, new ArtifactLinkContext(job.Id), ct);
        return new(JobPayload.Json(result), result.ArtifactReference);
    }
}

public sealed class MediaRegressionJobHandler(P2TechnicalIntelligenceService technical, ArtifactService artifacts, IncidentAggregationService incidents) : IDurableJobHandler
{
    public DurableJobType Type => DurableJobType.MediaRegression;
    public async Task<DurableJobExecutionResult> ExecuteAsync(DurableJob job, CancellationToken ct)
    {
        var command = JobPayload.Read<MediaRegressionCommand>(job);
        var result = await technical.AnalyzeMediaAsync(command, ct);
        var imported = await artifacts.ImportPathAsync(job.ProjectId, "media-regression", result.ArtifactReference, new ArtifactLinkContext(job.Id), ct);
        if (result.Status is "Failed" or "Warning")
        {
            var incident = await incidents.ObserveSyntheticAsync(job.ProjectId, job.SubjectKey, $"media:{command.Type}:{job.SubjectKey}", $"{command.Type} regression: {job.SubjectKey}", result.Status == "Failed" ? Severity.Critical : Severity.Warning, JobPayload.Json(result), job.Id, ct);
            foreach (var a in imported) await incidents.AttachArtifactAsync(incident.Id, a.Id, a.FileName, ct);
        }
        return new(JobPayload.Json(result), result.ArtifactReference);
    }
}

public sealed class BenchmarkJobHandler(BenchmarkOrchestrationService benchmarks) : IDurableJobHandler
{
    public DurableJobType Type => DurableJobType.Benchmark;
    public async Task<DurableJobExecutionResult> ExecuteAsync(DurableJob job, CancellationToken ct)
    {
        var request = JobPayload.Read<BenchmarkJobRequest>(job);
        var result = await benchmarks.ExecuteJobAsync(job, request, ct);
        return new(JobPayload.Json(result));
    }
}

public sealed class GameSessionReplayJobHandler(Dip.Application.Features.Replay.GameReplayService replay) : IDurableJobHandler
{
    public DurableJobType Type => DurableJobType.GameSessionReplay;
    public async Task<DurableJobExecutionResult> ExecuteAsync(DurableJob job, CancellationToken ct)
    {
        var request = JobPayload.Read<Dip.Application.Features.Replay.ReplayJobRequest>(job);
        var result = await replay.ExecuteJobAsync(job, request, ct);
        return new(JobPayload.Json(result));
    }
}

public sealed class ReleaseEvidenceJobHandler(Dip.Application.Features.Release.ReleaseEvidenceService release) : IDurableJobHandler
{
    public DurableJobType Type => DurableJobType.ReleaseEvidence;
    public async Task<DurableJobExecutionResult> ExecuteAsync(DurableJob job, CancellationToken ct)
    {
        var request = JobPayload.Read<Dip.Application.Features.Release.ReleaseEvidenceJobRequest>(job);
        var result = await release.ExecuteJobAsync(job, request, ct);
        return new(JobPayload.Json(result), result.BundleArtifactId);
    }
}

/// <summary>
/// Runs the complete autonomous QA session outside the HTTP request lifecycle.
/// The session itself persists hypotheses, experiment plans, baseline/post-patch
/// evidence and the final ReadyForHumanReview/NotVerified state in the graph.
/// </summary>
public sealed class AutonomousQaSessionJobHandler(
    AutonomousDebugService autonomous,
    AutonomousQaJobProgressReporter progress) : IDurableJobHandler
{
    public DurableJobType Type => DurableJobType.AutonomousQaSession;

    public async Task<DurableJobExecutionResult> ExecuteAsync(DurableJob job, CancellationToken ct)
    {
        progress.Bind(job);
        await progress.ReportAsync(1m, "Starting autonomous QA", ct);
        var request = JobPayload.Read<AutonomousDebugRequest>(job) with { DurableJobId = job.Id };
        var result = await autonomous.RunAsync(request, ct);
        await progress.ReportAsync(99.5m, result.Status, ct);
        return new(JobPayload.Json(result));
    }
}
