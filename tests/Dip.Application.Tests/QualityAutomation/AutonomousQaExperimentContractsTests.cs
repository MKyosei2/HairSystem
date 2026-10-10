// Purpose: Locks autonomous-QA executable-contract defaults and legacy enum compatibility.
using Dip.Application.Features.QualityAutomation;
using FluentAssertions;

namespace Dip.Application.Tests.QualityAutomation;

public sealed class AutonomousQaExperimentContractsTests
{
    [Fact]
    public void DebugExperimentKind_PreservesLegacyNumericValues()
    {
        ((int)DebugExperimentKind.Reproduce).Should().Be(0);
        ((int)DebugExperimentKind.VerifyPatch).Should().Be(1);
        ((int)DebugExperimentKind.RegressionTest).Should().Be(2);
        ((int)DebugExperimentKind.StaticAnalysis).Should().Be(3);
        ((int)DebugExperimentKind.RuntimeReplay).Should().Be(4);
        ((int)DebugExperimentKind.Benchmark).Should().Be(5);
        ((int)DebugExperimentKind.Bisect).Should().Be(6);
        ((int)DebugExperimentKind.Stress).Should().Be(7);
        ((int)DebugExperimentKind.Fuzz).Should().Be(8);
    }

    [Theory]
    [InlineData(DebugExperimentKind.VisualRegression)]
    [InlineData(DebugExperimentKind.Benchmark)]
    [InlineData(DebugExperimentKind.Fuzz)]
    [InlineData(DebugExperimentKind.Soak)]
    [InlineData(DebugExperimentKind.RuntimeReplay)]
    public void RequiredExperiment_DefaultContractAlwaysRequiresEvidence(DebugExperimentKind kind)
    {
        var step = AutonomousQaExperimentDefaults.Create(
            "test-step",
            kind,
            "test",
            required: true,
            timeoutSeconds: 120);

        step.Required.Should().BeTrue();
        step.SuccessCondition.Should().NotBeNullOrWhiteSpace();
        step.FalsificationCondition.Should().NotBeNullOrWhiteSpace();
        step.RequiredEvidence.Should().NotBeEmpty();
        step.RequiredEvidence.Should().OnlyContain(x => x.Required);
    }
}
