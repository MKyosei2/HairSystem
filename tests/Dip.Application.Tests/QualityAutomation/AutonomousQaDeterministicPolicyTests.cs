// Purpose: Locks the non-AI minimum experiment policy used by autonomous QA.
using Dip.Application.Features.QualityAutomation;
using Dip.Domain.ValueObjects;
using FluentAssertions;

namespace Dip.Application.Tests.QualityAutomation;

public sealed class AutonomousQaDeterministicPolicyTests
{
    [Fact]
    public void RenderingChange_RequiresVisualReplayShaderAndPerformanceEvidence()
    {
        var result = AutonomousQaDeterministicPolicy.RequiredFor(
            ["Assets/Shaders/Hero.hlsl"],
            "game",
            LanguageId.Cpp);

        result.Should().Contain(DebugExperimentKind.ShaderValidation);
        result.Should().Contain(DebugExperimentKind.RuntimeReplay);
        result.Should().Contain(DebugExperimentKind.VisualRegression);
        result.Should().Contain(DebugExperimentKind.Benchmark);
        result.Should().Contain(DebugExperimentKind.MemorySafety);
        result.Should().Contain(DebugExperimentKind.RegressionTest);
    }

    [Fact]
    public void MissingChangedFiles_GameDomain_StillHasConservativeNonAiFloor()
    {
        var result = AutonomousQaDeterministicPolicy.RequiredFor(
            [],
            "game",
            LanguageId.Auto);

        result.Should().Contain(DebugExperimentKind.StaticAnalysis);
        result.Should().Contain(DebugExperimentKind.RegressionTest);
        result.Should().Contain(DebugExperimentKind.RuntimeReplay);
        result.Should().Contain(DebugExperimentKind.VisualRegression);
        result.Should().Contain(DebugExperimentKind.Benchmark);
    }

    [Fact]
    public void MissingChangedFiles_BackendDomain_RequiresRobustnessAndLoadEvidence()
    {
        var result = AutonomousQaDeterministicPolicy.RequiredFor(
            null,
            "backend.service",
            LanguageId.CSharp);

        result.Should().Contain(DebugExperimentKind.Fuzz);
        result.Should().Contain(DebugExperimentKind.LoadTest);
        result.Should().Contain(DebugExperimentKind.RegressionTest);
    }

    [Fact]
    public void DocumentationOnlyChange_DoesNotForceRuntimeExperiments()
    {
        var result = AutonomousQaDeterministicPolicy.RequiredFor(
            ["docs/README.md"],
            "software",
            LanguageId.CSharp);

        result.Should().Equal(DebugExperimentKind.StaticAnalysis);
    }
}
