using Dip.Application.Features.QualityAutomation;
using Dip.Domain.ValueObjects;
using Dip.Infrastructure.QualityAutomation;
using FluentAssertions;

namespace Dip.Infrastructure.Tests.QualityAutomation;

public sealed class AutonomousQaExperimentDispatcherTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "dip-dispatcher-test-" + Guid.NewGuid().ToString("N"));

    public AutonomousQaExperimentDispatcherTests() => Directory.CreateDirectory(_root);

    [Fact]
    public async Task FuzzWithoutConfiguration_IsNotConfiguredAndNeverVerified()
    {
        var dispatcher = Create(new NoopRegistry(), new NoopPlanner());
        var step = AutonomousQaExperimentDefaults.Create("fuzz", DebugExperimentKind.Fuzz, "network", true, 60);

        var result = await dispatcher.ExecuteAsync(step, Context(), CancellationToken.None);

        result.Status.Should().Be(AutonomousQaExperimentStatus.NotConfigured);
        result.Verified.Should().BeFalse();
        result.EvidenceValidation.Should().NotBeNull();
        result.EvidenceValidation!.Satisfied.Should().BeFalse();
    }

    [Fact]
    public async Task CompileExperiment_CannotPassWhenOnlyTestCapabilityExecuted()
    {
        var adapter = new TestOnlyAdapter();
        var dispatcher = Create(new SingleRegistry(adapter), new TestOnlyPlanner());
        var step = AutonomousQaExperimentDefaults.Create("compile", DebugExperimentKind.Compile, "source", true, 60);

        var result = await dispatcher.ExecuteAsync(step, Context(), CancellationToken.None);

        result.Status.Should().Be(AutonomousQaExperimentStatus.Unavailable);
        result.Verified.Should().BeFalse();
        result.Summary.Should().Contain("strict capability floor");
    }


    [Fact]
    public async Task ShaderValidation_CannotPassFromGenericCompileCapability()
    {
        var adapter = new TestOnlyAdapter("dotnet-build");
        var dispatcher = Create(new SingleRegistry(adapter), new CompileOnlyPlanner());
        var step = AutonomousQaExperimentDefaults.Create("shader", DebugExperimentKind.ShaderValidation, "shader change", true, 60);

        var result = await dispatcher.ExecuteAsync(step, Context(), CancellationToken.None);

        result.Status.Should().Be(AutonomousQaExperimentStatus.Unavailable);
        result.Verified.Should().BeFalse();
        result.Summary.Should().Contain("strict capability floor");
    }

    [Fact]
    public async Task MemorySafety_CannotPassFromGenericRuntimeCapability()
    {
        var adapter = new TestOnlyAdapter("generic-runtime");
        var dispatcher = Create(new SingleRegistry(adapter), new RuntimeOnlyPlanner());
        var step = AutonomousQaExperimentDefaults.Create("memory", DebugExperimentKind.MemorySafety, "memory defect", true, 60);

        var result = await dispatcher.ExecuteAsync(step, Context(), CancellationToken.None);

        result.Status.Should().Be(AutonomousQaExperimentStatus.Unavailable);
        result.Verified.Should().BeFalse();
    }

    private AutonomousQaExperimentDispatcher Create(IQaAdapterRegistry registry, IQaCapabilityPlanner planner) =>
        new(
            new ProcessExecutor(),
            new Workspace(_root),
            new Detector(_root),
            registry,
            planner,
            Array.Empty<ICrossCuttingQaPack>(),
            null!, // Both tests terminate before durable artifact persistence is reached.
            new AutonomousQaEvidenceValidator());

    private AutonomousQaExperimentExecutionContext Context() =>
        new(Guid.NewGuid(), "subject", ".", "deadbeef", LanguageId.CSharp, AutonomousQaExperimentPhase.Baseline, WorkspaceOverridePath: _root);

    public void Dispose()
    {
        try { Directory.Delete(_root, true); } catch { }
    }

    private sealed class Workspace(string root) : IQaWorkspaceResolver
    {
        public string Resolve(string workspaceRelativePath) => root;
        public string QaArtifactRoot(Guid projectId, Guid qaRunId)
        {
            var path = Path.Combine(root, ".dipqa", qaRunId.ToString("N"));
            Directory.CreateDirectory(path);
            return path;
        }
    }

    private sealed class Detector(string root) : IRepositoryTechnologyDetector
    {
        public Task<QaTechnologyProfile> DetectAsync(string workspace, CancellationToken cancellationToken) =>
            Task.FromResult(new QaTechnologyProfile(root, [LanguageId.CSharp], ["dotnet"], [], [], []));
    }

    private sealed class NoopRegistry : IQaAdapterRegistry
    {
        public IReadOnlyList<string> AdapterIds => [];
        public IReadOnlyList<QaAdapterSelection> Resolve(LanguageId requested, QaTechnologyProfile profile, string workspace) => [];
    }

    private sealed class SingleRegistry(IQaAdapter adapter) : IQaAdapterRegistry
    {
        public IReadOnlyList<string> AdapterIds => [adapter.AdapterId];
        public IReadOnlyList<QaAdapterSelection> Resolve(LanguageId requested, QaTechnologyProfile profile, string workspace) =>
            [new(LanguageId.CSharp, adapter)];
    }

    private sealed class NoopPlanner : IQaCapabilityPlanner
    {
        public QaLanguagePack Describe(LanguageId language, QaTechnologyProfile profile, string workspace) => new(language, []);
    }

    private sealed class TestOnlyPlanner : IQaCapabilityPlanner
    {
        public QaLanguagePack Describe(LanguageId language, QaTechnologyProfile profile, string workspace) =>
            new(language, [new(QaCapability.UnitTest, "pytest", "test")]);
    }

    private sealed class TestOnlyAdapter(string toolName = "pytest") : IQaAdapter
    {
        public string AdapterId => "test-only";
        public int Priority => 1;
        public bool Supports(LanguageId language, QaTechnologyProfile profile, string workspace) => true;
        public Task<QaAdapterResult> RunAsync(QaExecutionCommand command, string workspace, string artifactRoot, CancellationToken cancellationToken) =>
            Task.FromResult(new QaAdapterResult(
                command.Language,
                [new QaToolExecution(toolName, true, true, 0, 1, "", "", "tests passed")],
                [],
                "{}"));
    }

    private sealed class CompileOnlyPlanner : IQaCapabilityPlanner
    {
        public QaLanguagePack Describe(LanguageId language, QaTechnologyProfile profile, string workspace) =>
            new(language, [new(QaCapability.Compile, "dotnet-build", "compile")]);
    }

    private sealed class RuntimeOnlyPlanner : IQaCapabilityPlanner
    {
        public QaLanguagePack Describe(LanguageId language, QaTechnologyProfile profile, string workspace) =>
            new(language, [new(QaCapability.Runtime, "generic-runtime", "runtime")]);
    }

}
