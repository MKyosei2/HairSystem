// Purpose: Defines shared hardware, adaptive-QA, experiment-target, test-impact, root-cause, and decision-intelligence contracts.
// Focused experiment contracts are split into SoakContracts, MutationContracts, FuzzContracts, RegressionContracts, MediaRegressionContracts, and GameRegressionContracts; GameRegressionEvaluator lives with the game-regression contracts.
using Dip.Application.Features.QualityAutomation;

namespace Dip.Application.Features.P2Intelligence;

public sealed record CommunityHardwareSubmission(
    Guid ProjectId,
    string ProfileKey,
    string Platform,
    string HardwareKey,
    string OsFamily,
    string DriverFamily,
    decimal CpuSingleScore,
    decimal CpuMultiScore,
    decimal GpuScore,
    decimal MemoryScore,
    decimal StorageScore,
    decimal ThermalRetentionPercent,
    long AvailableMemoryMb,
    bool RealDevice,
    bool ConsentGranted,
    DateTimeOffset MeasuredAtUtc);

public sealed record CreateCommunityHardwareProfileCommand(
    Guid ProjectId,
    string ProfileKey,
    string Platform,
    string Name,
    int TargetFps = 30);

public sealed record HardwareProfileConfidenceDto(
    string ProfileKey,
    string Platform,
    int SampleCount,
    int RealDeviceSampleCount,
    int OsFamilyCount,
    int DriverFamilyCount,
    decimal MedianCpuSingle,
    decimal MedianCpuMulti,
    decimal MedianGpu,
    decimal MedianMemory,
    decimal MedianStorage,
    decimal MedianThermalRetention,
    decimal P10Gpu,
    decimal P90Gpu,
    string Confidence,
    decimal ConfidencePercent,
    DateTimeOffset? LatestSampleAtUtc);

public sealed record PortabilityCommand(
    Guid ProjectId,
    string SubjectKey,
    Guid SourceHardwareProfileId,
    Guid TargetHardwareProfileId,
    decimal RequiredCpuScore,
    decimal RequiredGpuScore,
    decimal RequiredMemoryScore,
    decimal RequiredStorageScore,
    long RequiredMemoryMb,
    decimal RequiredThermalRetention);

public sealed record PortabilityDimension(
    string Name,
    decimal Available,
    decimal Required,
    decimal Ratio,
    string Status,
    string Explanation);

public sealed record PortabilityResult(
    decimal Score,
    string Status,
    string Confidence,
    IReadOnlyList<PortabilityDimension> Dimensions,
    IReadOnlyList<string> Blockers);

public sealed record AdaptiveQaPlanCommand(
    Guid ProjectId,
    string SubjectKey,
    IReadOnlyList<string> ChangedFiles,
    IReadOnlyList<string>? ChangedTags = null,
    decimal MaxEstimatedMinutes = 60m,
    string WorkspaceRelativePath = ".");

public sealed record AdaptiveQaStep(
    string Id,
    string Name,
    string Category,
    string Reason,
    decimal RiskScore,
    decimal EstimatedMinutes,
    int Priority,
    bool Required);

public sealed record AdaptiveQaPlanDto(
    Guid Id,
    string ChangeSetHash,
    decimal RiskScore,
    decimal EstimatedMinutes,
    IReadOnlyList<AdaptiveQaStep> Steps,
    IReadOnlyList<string> SkippedReasons,
    AutonomousQaExperimentPlan? ExecutablePlan = null);

public sealed record ExperimentTarget(
    Guid ProjectId,
    string SubjectKey,
    string WorkspaceRelativePath,
    string Executable,
    IReadOnlyList<string>? Arguments = null,
    int TimeoutSeconds = 60);

public sealed record RepeatedRunCommand(
    ExperimentTarget Target,
    int Attempts = 20,
    decimal FlakyWarningPercent = 2m,
    decimal FlakyFailurePercent = 10m);

public sealed record FlakyRunAttempt(
    int Attempt,
    bool Passed,
    bool TimedOut,
    int? ExitCode,
    long DurationMs,
    string Fingerprint);

public sealed record FlakyResult(
    string Status,
    int Attempts,
    int Passed,
    int Failed,
    int TimedOut,
    decimal FailureRatePercent,
    decimal ConfidencePercent,
    IReadOnlyList<FlakyRunAttempt> Runs,
    string ArtifactReference);

public sealed record TestImpactCommand(
    Guid ProjectId,
    string SubjectKey,
    string WorkspaceRelativePath,
    IReadOnlyList<string> ChangedFiles,
    int MaxSelectedTests = 50);

public sealed record TestImpactSelection(
    string TestPath,
    decimal Score,
    IReadOnlyList<string> Reasons);

public sealed record TestImpactResult(
    string Status,
    string ChangeSetHash,
    int CandidateTests,
    int SelectedTests,
    decimal EstimatedReductionPercent,
    IReadOnlyList<TestImpactSelection> Tests,
    IReadOnlyList<string> UnresolvedChangedFiles);

public interface ITestImpactAnalyzer
{
    Task<TestImpactResult> AnalyzeAsync(
        TestImpactCommand command,
        CancellationToken cancellationToken);
}

public sealed record NativeSymbolicationCommand(
    Guid ProjectId,
    string SubjectKey,
    string WorkspaceRelativePath,
    string BinaryRelativePath,
    IReadOnlyList<string> Addresses);

public sealed record NativeSymbolicatedFrame(
    string Address,
    string? Function,
    string? File,
    int? Line,
    string Raw);

public sealed record NativeSymbolicationResult(
    string Status,
    string Tool,
    IReadOnlyList<NativeSymbolicatedFrame> Frames,
    string ArtifactReference,
    string Summary);

public interface INativeSymbolicator
{
    Task<NativeSymbolicationResult> SymbolicateAsync(
        NativeSymbolicationCommand command,
        CancellationToken cancellationToken);
}

public sealed record RootCauseCommand(
    Guid ProjectId,
    string SubjectKey,
    string? Question = null,
    int EvidenceLimit = 40);

public sealed record RootCauseCandidate(
    string Title,
    decimal ConfidencePercent,
    string Rationale,
    IReadOnlyList<Guid> EvidenceIds);

public sealed record RootCauseResult(
    string Mode,
    string Status,
    IReadOnlyList<RootCauseCandidate> Candidates,
    IReadOnlyList<string> SuggestedActions,
    IReadOnlyList<Guid> GroundingEvidenceIds,
    string Summary);

public sealed record SimilarIncidentDto(
    Guid RecordId,
    string SubjectKey,
    string Kind,
    string Summary,
    decimal SimilarityPercent,
    DateTimeOffset CreatedAtUtc);

public sealed record ScheduleRiskDto(
    string Status,
    decimal RiskPercent,
    string Confidence,
    DateTimeOffset? ReleaseDateUtc,
    decimal EstimatedDaysRemaining,
    decimal ExpectedResolutionDays,
    int CriticalBlockers,
    int OpenBlockers,
    string Explanation);

public sealed record CompletionForecastDto(
    string Status,
    DateTimeOffset? Earliest,
    DateTimeOffset? MostLikely,
    DateTimeOffset? Latest,
    string Confidence,
    string Explanation);

public sealed record StagnationItem(
    string SubjectKey,
    DateTimeOffset? LastObjectiveEvidenceAtUtc,
    DateTimeOffset? LastReportedAtUtc,
    int DaysWithoutObjectiveEvidence,
    string Status,
    string Explanation);

public sealed record DependencyRiskCommand(
    Guid ProjectId,
    IReadOnlyDictionary<string, IReadOnlyList<string>> Dependencies);

public sealed record DependencyRiskItem(
    string SubjectKey,
    string Dependency,
    string Status,
    string Explanation);

public sealed record EvidenceFreshnessItem(
    Guid EvidenceId,
    string SubjectKey,
    string EvidenceKind,
    DateTimeOffset ObservedAtUtc,
    int AgeHours,
    string Freshness,
    string EffectiveConfidence);

public sealed record EvidenceConflictItem(
    string SubjectKey,
    string EvidenceKind,
    Guid AuthoritativeEvidenceId,
    IReadOnlyList<Guid> ConflictingEvidenceIds,
    string Resolution,
    string Explanation);

public sealed record DecisionPriority(
    int Priority,
    string SubjectKey,
    string Title,
    string Why,
    string SuggestedNextAction,
    string EvidenceConfidence,
    IReadOnlyList<Guid> EvidenceIds);

public sealed record DecisionBriefCommand(
    Guid ProjectId,
    IReadOnlyDictionary<string, IReadOnlyList<string>>? Dependencies = null);

public sealed record DecisionBriefDto(
    Guid ProjectId,
    string OverallStatus,
    string Summary,
    IReadOnlyList<DecisionPriority> Priorities,
    ScheduleRiskDto ScheduleRisk,
    CompletionForecastDto CompletionForecast,
    IReadOnlyList<StagnationItem> Stagnation,
    IReadOnlyList<DependencyRiskItem> DependencyRisks);

public sealed record DailyChangeDto(
    int NewEvidence,
    int NewFailures,
    int ResolvedOrSucceeded,
    int NewClaims,
    int NewContradictions,
    IReadOnlyList<string> Highlights);

public sealed record HealthChangeDto(
    decimal? PreviousHealth,
    decimal? CurrentHealth,
    decimal? Difference,
    string Explanation,
    IReadOnlyList<string> Reasons);

public sealed record P2IntelligenceDto(
    Guid Id,
    string SubjectKey,
    string Kind,
    string Status,
    decimal? Score,
    string Confidence,
    string Summary,
    string PayloadJson,
    string? SourceReference,
    DateTimeOffset CreatedAtUtc);

public sealed record ExternalCommandResult(
    bool Started,
    bool Passed,
    bool TimedOut,
    int? ExitCode,
    long DurationMs,
    string Stdout,
    string Stderr,
    string Fingerprint,
    string ArtifactReference);

public sealed record AiRootCauseRequest(string Question, string EvidenceJson);
public sealed record AiRootCauseResponse(string Model, string Json);

public interface IAiRootCauseClient
{
    Task<AiRootCauseResponse?> AnalyzeAsync(
        AiRootCauseRequest request,
        CancellationToken cancellationToken);
}
