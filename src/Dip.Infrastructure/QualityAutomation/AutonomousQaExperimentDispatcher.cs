// Purpose: Dispatches formal autonomous-QA contracts to bounded runners, enforces capability/evidence contracts, and persists durable artifacts.
using System.Text.Json;
using System.Security.Cryptography;
using System.Text;
using Dip.Application.Abstractions.Replay;
using Dip.Application.Features.Artifacts;
using Dip.Application.Features.Benchmarks;
using Dip.Application.Features.P2Intelligence;
using Dip.Application.Features.QualityAutomation;
using Dip.Domain.Enums;
using Dip.Domain.ValueObjects;
using Dip.Infrastructure.Benchmarks;
using Dip.Infrastructure.P2Intelligence;
using Dip.Infrastructure.Replay;
using Microsoft.Extensions.Options;

namespace Dip.Infrastructure.QualityAutomation;

public sealed class AutonomousQaExperimentDispatcher(
    ProcessExecutor executor,
    IQaWorkspaceResolver configuredWorkspaceResolver,
    IRepositoryTechnologyDetector detector,
    IQaAdapterRegistry adapters,
    IQaCapabilityPlanner capabilityPlanner,
    IEnumerable<ICrossCuttingQaPack> crossCuttingPacks,
    ArtifactService artifacts,
    AutonomousQaEvidenceValidator evidenceValidator) : IAutonomousQaExperimentDispatcher
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web)
    {
        PropertyNameCaseInsensitive = true
    };

    public async Task<IReadOnlyList<AutonomousQaExperimentResult>> ExecutePlanAsync(
        AutonomousQaExperimentPlan plan,
        AutonomousQaExperimentExecutionContext context,
        CancellationToken ct)
    {
        var results = new List<AutonomousQaExperimentResult>(plan.Steps.Count);
        foreach (var step in plan.Steps)
        {
            ct.ThrowIfCancellationRequested();
            results.Add(await ExecuteAsync(step, context, ct));
        }
        return results;
    }

    public async Task<AutonomousQaExperimentResult> ExecuteAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        CancellationToken ct)
    {
        var started = DateTimeOffset.UtcNow;
        try
        {
            var workspace = ResolveWorkspace(context);
            var resolver = new ExactWorkspaceResolver(workspace);
            var configuration = ResolveConfiguration(step, context, workspace);

            var raw = step.Kind switch
            {
                DebugExperimentKind.Compile
                    or DebugExperimentKind.StaticAnalysis
                    or DebugExperimentKind.UnitTest
                    or DebugExperimentKind.RegressionTest
                    or DebugExperimentKind.AssetValidation
                    or DebugExperimentKind.ShaderValidation
                    or DebugExperimentKind.MemorySafety
                    or DebugExperimentKind.RaceDetection
                    or DebugExperimentKind.DeadlockDetection
                    or DebugExperimentKind.Security
                    or DebugExperimentKind.Reproduce
                    => await RunCapabilityQaAsync(step, context, workspace, resolver, configuration, started, ct),

                DebugExperimentKind.Fuzz
                    => await RunFuzzAsync(step, context, resolver, configuration, started, ct),
                DebugExperimentKind.Stress or DebugExperimentKind.Soak
                    => await RunSoakAsync(step, context, resolver, configuration, started, ct),
                DebugExperimentKind.Mutation
                    => await RunMutationAsync(step, context, resolver, configuration, started, ct),
                DebugExperimentKind.Bisect
                    => await RunBisectAsync(step, context, resolver, configuration, started, ct),
                DebugExperimentKind.VisualRegression
                    => await RunMediaAsync(step, context, resolver, configuration, MediaRegressionType.Visual, "visual-diff", started, ct),
                DebugExperimentKind.VideoRegression
                    => await RunMediaAsync(step, context, resolver, configuration, MediaRegressionType.Video, "video-diff", started, ct),
                DebugExperimentKind.AudioRegression
                    => await RunMediaAsync(step, context, resolver, configuration, MediaRegressionType.Audio, "audio-diff", started, ct),
                DebugExperimentKind.Benchmark or DebugExperimentKind.LoadTest
                    => await RunBenchmarkAsync(step, context, resolver, configuration, started, ct),
                DebugExperimentKind.RuntimeReplay
                    => await RunReplayAsync(step, context, workspace, configuration, started, ct),
                DebugExperimentKind.VerifyPatch
                    => Result(step, context, AutonomousQaExperimentStatus.Skipped, false,
                        "VerifyPatch is owned by VerifiedFixService and is not independently dispatched.",
                        [], null, null, started),
                _ => Result(step, context, AutonomousQaExperimentStatus.Unavailable, false,
                    $"No bounded runner is registered for experiment kind {step.Kind}.",
                    [], null, null, started)
            };

            var enforced = evidenceValidator.Enforce(step, raw);
            return await PersistResultEnvelopeAsync(step, context, enforced, ct);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            var failed = evidenceValidator.Enforce(
                step,
                Result(
                    step,
                    context,
                    AutonomousQaExperimentStatus.InfrastructureFailure,
                    false,
                    $"Experiment infrastructure failed: {ex.GetType().Name}: {ex.Message}",
                    [],
                    null,
                    null,
                    started));
            try
            {
                return await PersistResultEnvelopeAsync(step, context, failed, ct);
            }
            catch (Exception persistEx) when (persistEx is not OperationCanceledException)
            {
                return failed with
                {
                    Summary = failed.Summary + $" Durable result persistence also failed: {persistEx.GetType().Name}: {persistEx.Message}"
                };
            }
        }
    }

    private async Task<AutonomousQaExperimentResult> RunCapabilityQaAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        string workspace,
        ExactWorkspaceResolver resolver,
        string? configuration,
        DateTimeOffset started,
        CancellationToken ct)
    {
        var config = Deserialize<GenericQaConfiguration>(configuration);
        var profile = await detector.DetectAsync(workspace, ct);
        var requested = context.Language.IsUnknown ? LanguageId.Auto : context.Language;
        var selections = adapters.Resolve(requested, profile, workspace);
        if (selections.Count == 0)
            return Result(step, context, AutonomousQaExperimentStatus.Unavailable, false,
                "No QA adapter supports the detected repository technology.", [], null, null, started);

        var runId = Guid.NewGuid();
        var artifactRoot = resolver.QaArtifactRoot(context.ProjectId, runId);
        var tools = new List<(LanguageId Language, QaToolExecution Tool)>();
        var findings = new List<(LanguageId Language, QaFindingCandidate Finding)>();
        var packsExecuted = new List<string>();

        foreach (var selection in selections)
        {
            var childRoot = Path.Combine(artifactRoot, selection.Language.Value);
            Directory.CreateDirectory(childRoot);
            var result = await selection.Adapter.RunAsync(
                new QaExecutionCommand(
                    context.ProjectId,
                    context.SubjectKey,
                    selection.Language,
                    ".",
                    context.CommitSha,
                    config?.RuntimeExecutable,
                    config?.RuntimeArguments,
                    Math.Clamp(step.TimeoutSeconds, 30, 1800),
                    AutomaticallyReproduce: step.Kind == DebugExperimentKind.Reproduce,
                    RecordScreen: false,
                    RecordSystemAudio: false),
                workspace,
                childRoot,
                ct);
            tools.AddRange(result.Tools.Select(x => (selection.Language, x)));
            findings.AddRange(result.Findings.Select(x => (selection.Language, x)));
        }

        if (step.Kind is DebugExperimentKind.AssetValidation or DebugExperimentKind.ShaderValidation)
        {
            foreach (var pack in crossCuttingPacks.Where(x => x.Applies(profile, workspace)))
            {
                var packRoot = Path.Combine(artifactRoot, "packs", SafeName(pack.PackId));
                Directory.CreateDirectory(packRoot);
                var packResult = await pack.RunAsync(
                    new QaExecutionCommand(
                        context.ProjectId,
                        context.SubjectKey,
                        requested,
                        ".",
                        context.CommitSha,
                        null,
                        null,
                        Math.Clamp(step.TimeoutSeconds, 30, 1800),
                        false,
                        false,
                        false),
                    profile,
                    workspace,
                    packRoot,
                    ct);
                packsExecuted.Add(pack.PackId);
                tools.AddRange(packResult.Tools.Select(x => (requested, x)));
                findings.AddRange(packResult.Findings.Select(x => (requested, x)));
            }
        }

        var available = tools.Where(x => x.Tool.WasAvailable).ToArray();
        if (available.Length == 0)
            return Result(step, context, AutonomousQaExperimentStatus.Unavailable, false,
                "QA adapters were selected, but no underlying verification tool was available.",
                [], artifactRoot, null, started);

        var executedCapabilities = InferExecutedCapabilities(available, profile, workspace, packsExecuted, step.Kind);
        if (!AutonomousQaCapabilityPolicy.IsSatisfied(step.Kind, executedCapabilities))
        {
            return Result(
                step,
                context,
                AutonomousQaExperimentStatus.Unavailable,
                false,
                $"Adapters executed, but none proved the strict capability floor for {step.Kind}. {AutonomousQaCapabilityPolicy.RequirementDescription(step.Kind)}",
                [],
                artifactRoot,
                null,
                started,
                executedCapabilities);
        }

        var critical = findings.Where(x => x.Finding.Severity == Severity.Critical).ToArray();
        var failedTools = available.Where(x => !x.Tool.Succeeded).ToArray();
        var status = failedTools.Length > 0 || critical.Length > 0
            ? AutonomousQaExperimentStatus.Failed
            : AutonomousQaExperimentStatus.Passed;

        var evidenceKind = AutonomousQaCapabilityPolicy.EvidenceKind(step.Kind);
        var refs = new List<string>();
        foreach (var capability in executedCapabilities)
            refs.Add($"{evidenceKind}:capability:{capability}");
        refs.AddRange(available.Select(x => $"{evidenceKind}:tool:{x.Tool.Tool}"));
        refs.AddRange(critical.Select(x => $"{evidenceKind}:finding:{QaFingerprint.Create(x.Language, x.Finding)}"));
        refs.AddRange(await PersistPathAsync(evidenceKind, artifactRoot, context, ct));

        var summary =
            $"Executed {selections.Count} adapter(s), {packsExecuted.Count} cross-cutting pack(s), "
            + $"{available.Length} available tool(s), {failedTools.Length} failed tool(s), "
            + $"{critical.Length} Critical finding(s); capabilities=[{string.Join(",", executedCapabilities)}].";
        return Result(
            step,
            context,
            status,
            status == AutonomousQaExperimentStatus.Passed,
            summary,
            refs,
            DurableArtifactReference(refs),
            EnvironmentFingerprint(available.Select(x => x.Tool.Tool)),
            started,
            executedCapabilities);
    }

    private IReadOnlyList<QaCapability> InferExecutedCapabilities(
        IReadOnlyList<(LanguageId Language, QaToolExecution Tool)> available,
        QaTechnologyProfile profile,
        string workspace,
        IReadOnlyList<string> packsExecuted,
        DebugExperimentKind kind)
    {
        var result = new HashSet<QaCapability>();
        foreach (var row in available)
        {
            var pack = capabilityPlanner.Describe(row.Language, profile, workspace);
            foreach (var descriptor in pack.Capabilities)
                if (ToolMatchesDescriptor(row.Tool, descriptor))
                    result.Add(descriptor.Capability);
            InferByToolName(row.Tool.Tool, result);
        }
        if (kind == DebugExperimentKind.AssetValidation
            && packsExecuted.Any(x => x.Contains("asset", StringComparison.OrdinalIgnoreCase)
                                      || x.Contains("production-capability", StringComparison.OrdinalIgnoreCase)))
            result.Add(QaCapability.AssetValidation);
        // ShaderValidation is intentionally never inferred from a generic game/production
        // pack. A shader compiler/validator (GLSL/HLSL) must actually execute.
        if (packsExecuted.Any(x => x.Equals("gpu-capture", StringComparison.OrdinalIgnoreCase)))
            result.Add(QaCapability.GpuProfiling);
        return result.OrderBy(x => x).ToArray();
    }

    private static bool ToolMatchesDescriptor(QaToolExecution tool, QaCapabilityDescriptor descriptor)
    {
        var actual = NormalizeTool(tool.Tool);
        var expected = descriptor.Tool
            .Split(['/', ' ', '+', ','], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(NormalizeTool)
            .Where(x => x.Length >= 3)
            .ToArray();
        return expected.Any(token => actual.Contains(token, StringComparison.OrdinalIgnoreCase)
                                     || token.Contains(actual, StringComparison.OrdinalIgnoreCase));
    }

    private static void InferByToolName(string toolName, ISet<QaCapability> result)
    {
        var name = NormalizeTool(toolName);
        if (ContainsAny(name, "compile", "build", "clang", "gcc", "glslang", "dxc", "fxc", "typescript", "typecheck", "javac", "kotlinc", "cargo-check"))
            result.Add(QaCapability.Compile);
        if (ContainsAny(name, "typecheck", "typescript", "mypy"))
            result.Add(QaCapability.TypeCheck);
        if (ContainsAny(name, "lint", "tidy", "cppcheck", "semgrep", "rubocop", "phpstan", "psalm", "vet", "clippy", "static", "semantic"))
            result.Add(QaCapability.StaticAnalysis);
        if (ContainsAny(name, "test", "pytest", "ctest", "pester", "rspec", "phpunit", "bats", "busted"))
            result.Add(QaCapability.UnitTest);
        if (ContainsAny(name, "property", "quickcheck", "proptest", "fast-check"))
            result.Add(QaCapability.PropertyTest);
        if (ContainsAny(name, "fuzz"))
            result.Add(QaCapability.Fuzz);
        if (ContainsAny(name, "race", "tsan", "threadsanitizer", "miri"))
            result.Add(QaCapability.RaceDetection);
        if (ContainsAny(name, "deadlock", "lock-check"))
            result.Add(QaCapability.DeadlockDetection);
        if (ContainsAny(name, "asan", "ubsan", "sanitizer", "valgrind", "miri"))
            result.Add(QaCapability.MemorySafety);
        if (ContainsAny(name, "runtime", "replay", "ctest", "game-run"))
            result.Add(QaCapability.Runtime);
        if (ContainsAny(name, "security", "bandit", "trivy", "gosec", "brakeman"))
            result.Add(QaCapability.Security);
        if (ContainsAny(name, "unity", "unreal", "godot", "game"))
            result.Add(QaCapability.GameEngine);
        if (ContainsAny(name, "shader", "glslang", "dxc", "fxc"))
            result.Add(QaCapability.ShaderValidation);
        if (ContainsAny(name, "renderdoc", "nsys", "ncu", "gpu-capture"))
            result.Add(QaCapability.GpuProfiling);
    }

    private async Task<AutonomousQaExperimentResult> RunFuzzAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        ExactWorkspaceResolver resolver,
        string? configuration,
        DateTimeOffset started,
        CancellationToken ct)
    {
        var config = Deserialize<FuzzDispatchConfiguration>(configuration);
        if (config is null || string.IsNullOrWhiteSpace(config.Executable))
            return NotConfigured(step, context, "Fuzz requires executable/arguments configuration.", started);
        var result = await new FuzzExperimentRunner(executor, resolver).RunFuzzAsync(
            new FuzzCommand(
                Target(context, config.Executable, config.Arguments, config.TimeoutSeconds ?? step.TimeoutSeconds),
                config.ArgumentPlaceholder ?? "{input}",
                config.Cases ?? 100,
                config.MaxInputBytes ?? 4096,
                config.Seed ?? 1337,
                config.MinimizeFailures ?? true,
                config.MaxMinimizationRunsPerFailure ?? 24),
            ct);
        var status = MapStatus(result.Status);
        var refs = new List<string>
        {
            $"fuzz:fingerprints:{result.UniqueFingerprints}",
            $"fuzz:cases:{result.Cases}"
        };
        refs.AddRange(await PersistPathAsync("fuzz", result.ArtifactReference, context, ct));
        return Result(step, context, status, status == AutonomousQaExperimentStatus.Passed,
            $"Fuzz: {result.Cases} case(s), {result.Crashes} crash(es), {result.Timeouts} timeout(s), {result.UniqueFingerprints} unique failure fingerprint(s).",
            refs, DurableArtifactReference(refs), EnvironmentFingerprint([config.Executable]), started, [QaCapability.Fuzz]);
    }

    private async Task<AutonomousQaExperimentResult> RunSoakAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        ExactWorkspaceResolver resolver,
        string? configuration,
        DateTimeOffset started,
        CancellationToken ct)
    {
        var config = Deserialize<SoakDispatchConfiguration>(configuration);
        if (config is null || string.IsNullOrWhiteSpace(config.Executable))
            return NotConfigured(step, context, "Soak/Stress requires executable/arguments configuration.", started);
        var result = await new SoakExperimentRunner(executor, resolver).RunSoakAsync(
            new SoakCommand(
                Target(context, config.Executable, config.Arguments, config.TimeoutSeconds ?? step.TimeoutSeconds),
                config.DurationSeconds ?? Math.Clamp(step.TimeoutSeconds, 5, 1800),
                config.SampleIntervalSeconds ?? 5,
                config.MemoryGrowthWarningPercent ?? 20m,
                config.PerformanceDriftWarningPercent ?? 15m),
            ct);
        var status = MapStatus(result.Status);
        var refs = new List<string>
        {
            $"soak:samples:{result.Samples.Count}",
            $"soak:memory-growth:{result.MemoryGrowthPercent:0.###}"
        };
        refs.AddRange(await PersistPathAsync("soak", result.ArtifactReference, context, ct));
        return Result(step, context, status, status == AutonomousQaExperimentStatus.Passed,
            $"Soak: memory {result.StartMemoryMb}->{result.EndMemoryMb} MiB ({result.MemoryGrowthPercent:+0.##;-0.##;0}%), performance drift {(result.PerformanceDriftPercent?.ToString("0.##") ?? "n/a")}%, crashed={result.Crashed}.",
            refs, DurableArtifactReference(refs), EnvironmentFingerprint([config.Executable]), started, [QaCapability.Runtime]);
    }

    private async Task<AutonomousQaExperimentResult> RunMutationAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        ExactWorkspaceResolver resolver,
        string? configuration,
        DateTimeOffset started,
        CancellationToken ct)
    {
        var config = Deserialize<MutationDispatchConfiguration>(configuration);
        if (config is null || string.IsNullOrWhiteSpace(config.Executable))
            return NotConfigured(step, context, "Mutation requires an executable and mutation configuration.", started);
        var result = await new MutationExperimentRunner(executor, resolver).RunMutationAsync(
            new MutationCommand(
                Target(context, config.Executable, config.Arguments, config.TimeoutSeconds ?? step.TimeoutSeconds),
                config.Mutations ?? [],
                config.MaxMutations ?? 50,
                config.GenerateCommonMutations ?? true,
                config.AutoSourceFiles),
            ct);
        var status = MapStatus(result.Status);
        var refs = new List<string> { $"mutation:score:{result.MutationScorePercent:0.##}" };
        refs.AddRange(await PersistPathAsync("mutation", result.ArtifactReference, context, ct));
        return Result(step, context, status, status == AutonomousQaExperimentStatus.Passed,
            $"Mutation: score {result.MutationScorePercent:0.##}% ({result.Killed}/{result.Applied} killed).",
            refs, DurableArtifactReference(refs), EnvironmentFingerprint([config.Executable]), started, [QaCapability.UnitTest]);
    }

    private async Task<AutonomousQaExperimentResult> RunBisectAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        ExactWorkspaceResolver resolver,
        string? configuration,
        DateTimeOffset started,
        CancellationToken ct)
    {
        var config = Deserialize<BisectDispatchConfiguration>(configuration);
        if (config is null
            || string.IsNullOrWhiteSpace(config.Executable)
            || string.IsNullOrWhiteSpace(config.GoodCommit)
            || string.IsNullOrWhiteSpace(config.BadCommit))
            return NotConfigured(step, context, "Bisect requires executable, GoodCommit, and BadCommit configuration.", started);
        var result = await new GitBisectRunner(executor, resolver).FindRegressionCommitAsync(
            new GitBisectCommand(
                Target(context, config.Executable, config.Arguments, config.TimeoutSeconds ?? step.TimeoutSeconds),
                config.GoodCommit,
                config.BadCommit,
                config.MaxEvaluations ?? 20),
            ct);
        var status = result.Status.Equals("Found", StringComparison.OrdinalIgnoreCase)
            ? AutonomousQaExperimentStatus.Passed
            : MapStatus(result.Status);
        var refs = new List<string> { $"bisect:first-bad:{result.FirstBadCommit ?? "unknown"}" };
        refs.AddRange(await PersistPathAsync("bisect", result.ArtifactReference, context, ct));
        return Result(step, context, status, status == AutonomousQaExperimentStatus.Passed,
            $"Bisect: status={result.Status}, firstBad={result.FirstBadCommit ?? "n/a"}, lastGood={result.LastGoodCommit ?? "n/a"}, evaluations={result.Evaluations}.",
            refs, DurableArtifactReference(refs), EnvironmentFingerprint([config.Executable]), started);
    }

    private async Task<AutonomousQaExperimentResult> RunMediaAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        ExactWorkspaceResolver resolver,
        string? configuration,
        MediaRegressionType type,
        string evidenceKind,
        DateTimeOffset started,
        CancellationToken ct)
    {
        var config = Deserialize<MediaDispatchConfiguration>(configuration);
        if (config is null || string.IsNullOrWhiteSpace(config.BaselinePath) || string.IsNullOrWhiteSpace(config.CurrentPath))
            return NotConfigured(step, context, $"{type} regression requires BaselinePath and CurrentPath configuration.", started);
        var result = await new MediaRegressionRunner(executor, resolver).AnalyzeAsync(
            new MediaRegressionCommand(
                context.ProjectId,
                context.SubjectKey,
                ".",
                config.BaselinePath,
                config.CurrentPath,
                type,
                config.WarningThresholdPercent ?? 2m,
                config.FailureThresholdPercent ?? 10m,
                config.VideoSampleFps ?? 2),
            ct);
        var status = MapStatus(result.Status);
        var refs = new List<string> { $"{evidenceKind}:difference:{result.DifferencePercent:0.###}" };
        refs.AddRange(await PersistPathAsync(evidenceKind, result.ArtifactReference, context, ct));
        return Result(step, context, status, status == AutonomousQaExperimentStatus.Passed,
            $"{type} regression: {result.Summary} Difference={result.DifferencePercent:0.###}%, confidence={result.ConfidencePercent:0.##}%.",
            refs, DurableArtifactReference(refs), EnvironmentFingerprint(["ffmpeg", "ffprobe", type.ToString()]), started);
    }

    private async Task<AutonomousQaExperimentResult> RunBenchmarkAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        ExactWorkspaceResolver resolver,
        string? configuration,
        DateTimeOffset started,
        CancellationToken ct)
    {
        var config = Deserialize<BenchmarkExecutionSpec>(configuration);
        if (config is null || string.IsNullOrWhiteSpace(config.Target))
            return NotConfigured(step, context, "Benchmark/LoadTest requires a BenchmarkExecutionSpec configuration.", started);
        if (string.Equals(config.Mode, "http", StringComparison.OrdinalIgnoreCase))
            return NotConfigured(step, context,
                "Autonomous QA rejects direct HTTP benchmark targets to preserve the network/SSRF boundary. Configure a bounded process-mode load runner such as k6.",
                started);
        var spec = config with
        {
            WorkspaceRelativePath = ".",
            TimeoutSeconds = Math.Clamp(config.TimeoutSeconds <= 0 ? step.TimeoutSeconds : config.TimeoutSeconds, 1, 600)
        };
        using var benchmark = new BenchmarkExecutor(executor, resolver);
        var result = await benchmark.ExecuteAsync(spec, ct);
        var passed = result.Samples.Count > 0 && result.Samples.All(x => x.Passed && !x.TimedOut);
        var status = passed ? AutonomousQaExperimentStatus.Passed : AutonomousQaExperimentStatus.Failed;
        var record = await artifacts.StoreJsonAsync(
            context.ProjectId,
            "autonomous-qa-benchmark",
            $"{SafeName(step.Id)}-{context.Phase}.json",
            new { step.Id, step.Kind, context.Phase, context.CommitSha, spec, result },
            new ArtifactLinkContext(context.DurableJobId),
            "qa",
            ct);
        var refs = new[]
        {
            $"benchmark:artifact:{record.Id}",
            $"benchmark:samples:{result.Samples.Count}",
            $"benchmark:environment:{result.EnvironmentFingerprint}"
        };
        return Result(step, context, status, passed,
            $"Benchmark: {result.Samples.Count} measured sample(s), {result.Samples.Count(x => x.Passed)} passed, {result.Samples.Count(x => x.TimedOut)} timed out.",
            refs, $"artifact:{record.Id}", result.EnvironmentFingerprint, started);
    }

    private async Task<AutonomousQaExperimentResult> RunReplayAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        string workspace,
        string? configuration,
        DateTimeOffset started,
        CancellationToken ct)
    {
        var config = Deserialize<ReplayDispatchConfiguration>(configuration);
        if (config is null || string.IsNullOrWhiteSpace(config.Command))
            return NotConfigured(step, context, "RuntimeReplay requires replay command configuration.", started);
        var replay = new GameReplayExecutor(
            executor,
            Options.Create(new GameReplayOptions
            {
                WorkspaceRoot = workspace,
                AllowPathLookup = config.AllowPathLookup ?? false
            }));
        var result = await replay.ExecuteAsync(
            new ReplayExecutionRequest(
                config.Command,
                config.Arguments ?? string.Empty,
                config.WorkingDirectory,
                config.Scene ?? "default",
                config.InputArtifactId ?? string.Empty,
                config.RandomSeed ?? 1337,
                Math.Clamp(config.DurationSeconds ?? Math.Min(step.TimeoutSeconds, 300), 1, 1800),
                context.CommitSha ?? "working-tree-candidate",
                config.BuildId ?? "autonomous-qa",
                config.HardwareFingerprint ?? EnvironmentFingerprint(["runtime-replay"])),
            ct);
        var passed = result.Succeeded && !result.Crashed && !result.TimedOut;
        var status = passed ? AutonomousQaExperimentStatus.Passed : AutonomousQaExperimentStatus.Failed;
        var record = await artifacts.StoreJsonAsync(
            context.ProjectId,
            "autonomous-qa-replay",
            $"{SafeName(step.Id)}-{context.Phase}.json",
            new
            {
                step.Id,
                step.Kind,
                context.Phase,
                context.CommitSha,
                result.Succeeded,
                result.ExitCode,
                result.Crashed,
                result.TimedOut,
                result.Samples,
                result.EnvironmentFingerprint,
                stdout = Clip(result.StandardOutput, 200_000),
                stderr = Clip(result.StandardError, 200_000)
            },
            new ArtifactLinkContext(context.DurableJobId),
            "qa",
            ct);
        var refs = new[]
        {
            $"runtime-replay:artifact:{record.Id}",
            $"runtime-replay:exit:{result.ExitCode}",
            $"runtime-replay:metrics:{result.Samples.Count}"
        };
        return Result(step, context, status, passed,
            $"Replay: succeeded={result.Succeeded}, crashed={result.Crashed}, timedOut={result.TimedOut}, exit={result.ExitCode}, metrics={result.Samples.Count}.",
            refs, $"artifact:{record.Id}", result.EnvironmentFingerprint, started, [QaCapability.Runtime]);
    }

    private async Task<AutonomousQaExperimentResult> PersistResultEnvelopeAsync(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        AutonomousQaExperimentResult result,
        CancellationToken ct)
    {
        // ArtifactService is required by production DI. Null is supported only as a
        // narrow unit-test seam for results that terminate before evidence persistence.
        if (artifacts is null)
            return result;

        var record = await artifacts.StoreJsonAsync(
            context.ProjectId,
            "autonomous-qa-result",
            $"{SafeName(step.Id)}-{context.Phase}.json",
            new
            {
                step,
                result,
                context.ProjectId,
                context.SubjectKey,
                context.CommitSha,
                context.Phase
            },
            new ArtifactLinkContext(context.DurableJobId),
            "qa",
            ct);
        var envelopeRef = $"experiment-result:artifact:{record.Id}";
        var refs = result.EvidenceReferences
            .Concat([envelopeRef])
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();
        return result with
        {
            EvidenceReferences = refs,
            ArtifactReference = result.ArtifactReference ?? $"artifact:{record.Id}"
        };
    }

    private async Task<IReadOnlyList<string>> PersistPathAsync(
        string evidenceKind,
        string path,
        AutonomousQaExperimentExecutionContext context,
        CancellationToken ct)
    {
        if (artifacts is null)
            return Array.Empty<string>();

        var imported = await artifacts.ImportPathAsync(
            context.ProjectId,
            $"autonomous-qa-{evidenceKind}",
            path,
            new ArtifactLinkContext(context.DurableJobId),
            ct);
        return imported.Select(x => $"{evidenceKind}:artifact:{x.Id}").ToArray();
    }

    private static string? DurableArtifactReference(IEnumerable<string> references) =>
        references.FirstOrDefault(x => x.Contains(":artifact:", StringComparison.OrdinalIgnoreCase))
            ?.Split(":artifact:", 2, StringSplitOptions.None)
            .LastOrDefault() is { Length: > 0 } id
            ? $"artifact:{id}"
            : null;

    private ExperimentTarget Target(
        AutonomousQaExperimentExecutionContext context,
        string executable,
        IReadOnlyList<string>? arguments,
        int timeoutSeconds) =>
        new(context.ProjectId, context.SubjectKey, ".", executable, arguments, Math.Clamp(timeoutSeconds, 1, 1800));

    private string ResolveWorkspace(AutonomousQaExperimentExecutionContext context)
    {
        if (!string.IsNullOrWhiteSpace(context.WorkspaceOverridePath))
        {
            var overridePath = Path.GetFullPath(context.WorkspaceOverridePath);
            if (!Directory.Exists(overridePath))
                throw new DirectoryNotFoundException(overridePath);
            return overridePath;
        }
        return configuredWorkspaceResolver.Resolve(context.WorkspaceRelativePath);
    }

    private static string? ResolveConfiguration(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        string workspace)
    {
        if (!string.IsNullOrWhiteSpace(step.ConfigurationJson))
            return step.ConfigurationJson;
        var fromRequest = ExtractExperimentConfiguration(context.ExperimentConfigurationJson, step);
        if (!string.IsNullOrWhiteSpace(fromRequest))
            return fromRequest;
        var manifest = Path.Combine(workspace, ".dip", "autonomous-qa.json");
        if (!File.Exists(manifest))
            return null;
        try { return ExtractExperimentConfiguration(File.ReadAllText(manifest), step); }
        catch (IOException) { return null; }
    }

    private static string? ExtractExperimentConfiguration(string? json, AutonomousQaExperimentStep step)
    {
        if (string.IsNullOrWhiteSpace(json))
            return null;
        try
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                return null;
            var experiments = Find(root, "experiments");
            if (experiments.HasValue && experiments.Value.ValueKind == JsonValueKind.Object)
                root = experiments.Value;
            foreach (var name in new[] { step.Id, step.Kind.ToString(), step.Kind.ToString().ToLowerInvariant() })
            {
                var value = Find(root, name);
                if (value is not null)
                    return value.Value.GetRawText();
            }
            return null;
        }
        catch (JsonException) { return null; }
    }

    private static JsonElement? Find(JsonElement root, string name)
    {
        foreach (var property in root.EnumerateObject())
            if (property.Name.Equals(name, StringComparison.OrdinalIgnoreCase))
                return property.Value;
        return null;
    }

    private static T? Deserialize<T>(string? json) where T : class
    {
        if (string.IsNullOrWhiteSpace(json))
            return null;
        try { return JsonSerializer.Deserialize<T>(json, Json); }
        catch (JsonException) { return null; }
    }

    private static AutonomousQaExperimentResult NotConfigured(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        string summary,
        DateTimeOffset started) =>
        Result(step, context, AutonomousQaExperimentStatus.NotConfigured, false, summary, [], null, null, started);

    private static AutonomousQaExperimentStatus MapStatus(string status) =>
        status.ToLowerInvariant() switch
        {
            "passed" or "succeeded" or "found" => AutonomousQaExperimentStatus.Passed,
            "warning" => AutonomousQaExperimentStatus.Warning,
            "failed" => AutonomousQaExperimentStatus.Failed,
            "infrastructurefailure" => AutonomousQaExperimentStatus.InfrastructureFailure,
            "unavailable" => AutonomousQaExperimentStatus.Unavailable,
            _ => AutonomousQaExperimentStatus.Warning
        };

    private static AutonomousQaExperimentResult Result(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentExecutionContext context,
        AutonomousQaExperimentStatus status,
        bool verified,
        string summary,
        IReadOnlyList<string> evidence,
        string? artifact,
        string? environment,
        DateTimeOffset started,
        IReadOnlyList<QaCapability>? capabilities = null)
    {
        if (status == AutonomousQaExperimentStatus.Passed && evidence.Count == 0)
        {
            status = AutonomousQaExperimentStatus.InfrastructureFailure;
            verified = false;
            summary = "Runner reported PASS without an evidence reference. " + summary;
        }

        var evidenceWithRevision = evidence
            .Concat(["hardening-revision:" + AutonomousQaHardeningRevision.Current])
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();

        return new(
            step.Id,
            step.Kind,
            context.Phase,
            status,
            verified && status == AutonomousQaExperimentStatus.Passed,
            summary,
            evidenceWithRevision,
            artifact,
            environment,
            started,
            DateTimeOffset.UtcNow,
            capabilities,
            null,
            context.CommitSha,
            Conclusion(step.Kind, status));
    }


    private static AutonomousQaExperimentConclusion Conclusion(
        DebugExperimentKind kind,
        AutonomousQaExperimentStatus status)
    {
        if (status is AutonomousQaExperimentStatus.Unavailable
            or AutonomousQaExperimentStatus.NotConfigured
            or AutonomousQaExperimentStatus.InfrastructureFailure
            or AutonomousQaExperimentStatus.Skipped
            or AutonomousQaExperimentStatus.Planned)
            return AutonomousQaExperimentConclusion.Inconclusive;

        // Bisect is an experiment whose successful execution identifies/supports
        // a regression boundary rather than proving the product is healthy.
        if (kind == DebugExperimentKind.Bisect && status == AutonomousQaExperimentStatus.Passed)
            return AutonomousQaExperimentConclusion.SupportsHypothesis;

        return status == AutonomousQaExperimentStatus.Passed
            ? AutonomousQaExperimentConclusion.FalsifiesHypothesis
            : AutonomousQaExperimentConclusion.SupportsHypothesis;
    }

    private static string EnvironmentFingerprint(IEnumerable<string> toolIdentities)
    {
        var text = string.Join("|", new[]
        {
            Environment.MachineName,
            System.Runtime.InteropServices.RuntimeInformation.OSDescription,
            System.Runtime.InteropServices.RuntimeInformation.OSArchitecture.ToString(),
            System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
            System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
            Environment.GetEnvironmentVariable("PROCESSOR_IDENTIFIER") ?? "unknown",
            Environment.ProcessorCount.ToString(),
            string.Join(",", toolIdentities.OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
        });
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text))).ToLowerInvariant();
    }

    private static string NormalizeTool(string value) =>
        new(value.Where(char.IsLetterOrDigit).Select(char.ToLowerInvariant).ToArray());

    private static bool ContainsAny(string value, params string[] tokens) =>
        tokens.Any(token => value.Contains(NormalizeTool(token), StringComparison.OrdinalIgnoreCase));

    private static string SafeName(string value)
    {
        var safe = string.Concat(value.Select(c => char.IsLetterOrDigit(c) || c is '-' or '_' ? c : '_'));
        return safe[..Math.Min(100, safe.Length)];
    }

    private static string Clip(string value, int max) => value.Length <= max ? value : value[..max];

    private sealed class ExactWorkspaceResolver(string workspace) : IQaWorkspaceResolver
    {
        private readonly string _workspace = Path.GetFullPath(workspace);

        public string Resolve(string workspaceRelativePath)
        {
            var relative = string.IsNullOrWhiteSpace(workspaceRelativePath) ? "." : workspaceRelativePath;
            if (Path.IsPathRooted(relative))
                throw new InvalidOperationException("Experiment workspace paths must remain relative to the trusted verification root.");
            var resolved = Path.GetFullPath(Path.Combine(_workspace, relative));
            var comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
            var prefix = _workspace.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
            if (!resolved.Equals(_workspace, comparison) && !resolved.StartsWith(prefix, comparison))
                throw new InvalidOperationException("Experiment workspace escaped the trusted verification root.");
            if (!Directory.Exists(resolved))
                throw new DirectoryNotFoundException(resolved);
            return resolved;
        }

        public string QaArtifactRoot(Guid projectId, Guid qaRunId)
        {
            var root = Path.Combine(_workspace, ".dipqa", "autonomous", projectId.ToString("N"), $"run-{qaRunId:N}");
            Directory.CreateDirectory(root);
            return root;
        }
    }

    private sealed record GenericQaConfiguration(string? RuntimeExecutable = null, IReadOnlyList<string>? RuntimeArguments = null);
    private sealed record FuzzDispatchConfiguration(string Executable, IReadOnlyList<string>? Arguments = null, string? ArgumentPlaceholder = null, int? Cases = null, int? MaxInputBytes = null, int? Seed = null, bool? MinimizeFailures = null, int? MaxMinimizationRunsPerFailure = null, int? TimeoutSeconds = null);
    private sealed record SoakDispatchConfiguration(string Executable, IReadOnlyList<string>? Arguments = null, int? DurationSeconds = null, int? SampleIntervalSeconds = null, decimal? MemoryGrowthWarningPercent = null, decimal? PerformanceDriftWarningPercent = null, int? TimeoutSeconds = null);
    private sealed record MutationDispatchConfiguration(string Executable, IReadOnlyList<string>? Arguments = null, IReadOnlyList<MutationCaseSpec>? Mutations = null, int? MaxMutations = null, bool? GenerateCommonMutations = null, IReadOnlyList<string>? AutoSourceFiles = null, int? TimeoutSeconds = null);
    private sealed record BisectDispatchConfiguration(string Executable, string GoodCommit, string BadCommit, IReadOnlyList<string>? Arguments = null, int? MaxEvaluations = null, int? TimeoutSeconds = null);
    private sealed record MediaDispatchConfiguration(string BaselinePath, string CurrentPath, decimal? WarningThresholdPercent = null, decimal? FailureThresholdPercent = null, int? VideoSampleFps = null);
    private sealed record ReplayDispatchConfiguration(string Command, string? Arguments = null, string? WorkingDirectory = null, string? Scene = null, string? InputArtifactId = null, int? RandomSeed = null, int? DurationSeconds = null, string? BuildId = null, string? HardwareFingerprint = null, bool? AllowPathLookup = null);
}
