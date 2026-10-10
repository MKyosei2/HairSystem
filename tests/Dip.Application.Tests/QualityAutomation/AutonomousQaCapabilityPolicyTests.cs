using Dip.Application.Features.QualityAutomation;
using FluentAssertions;

namespace Dip.Application.Tests.QualityAutomation;

public sealed class AutonomousQaCapabilityPolicyTests
{
    [Theory]
    [InlineData(DebugExperimentKind.Compile, QaCapability.Compile)]
    [InlineData(DebugExperimentKind.StaticAnalysis, QaCapability.StaticAnalysis)]
    [InlineData(DebugExperimentKind.RegressionTest, QaCapability.UnitTest)]
    [InlineData(DebugExperimentKind.ShaderValidation, QaCapability.ShaderValidation)]
    [InlineData(DebugExperimentKind.MemorySafety, QaCapability.MemorySafety)]
    [InlineData(DebugExperimentKind.RaceDetection, QaCapability.RaceDetection)]
    [InlineData(DebugExperimentKind.DeadlockDetection, QaCapability.DeadlockDetection)]
    [InlineData(DebugExperimentKind.Security, QaCapability.Security)]
    public void FormalExperiment_RequiresConcreteCapability(DebugExperimentKind kind, QaCapability expected)
    {
        AutonomousQaCapabilityPolicy.AcceptedCapabilities(kind).Should().Contain(expected);
    }

    [Fact]
    public void ShaderValidation_CannotBeSatisfiedByGenericCompile()
    {
        AutonomousQaCapabilityPolicy.IsSatisfied(
            DebugExperimentKind.ShaderValidation,
            [QaCapability.Compile, QaCapability.GameEngine]).Should().BeFalse();
        AutonomousQaCapabilityPolicy.IsSatisfied(
            DebugExperimentKind.ShaderValidation,
            [QaCapability.ShaderValidation]).Should().BeTrue();
    }

    [Fact]
    public void MemorySafety_CannotBeSatisfiedByGenericRuntime()
    {
        AutonomousQaCapabilityPolicy.IsSatisfied(
            DebugExperimentKind.MemorySafety,
            [QaCapability.Runtime]).Should().BeFalse();
        AutonomousQaCapabilityPolicy.IsSatisfied(
            DebugExperimentKind.MemorySafety,
            [QaCapability.MemorySafety]).Should().BeTrue();
    }

    [Theory]
    [InlineData(DebugExperimentKind.VisualRegression, "experiment")]
    [InlineData(DebugExperimentKind.Compile, "tool-execution")]
    [InlineData(DebugExperimentKind.ShaderValidation, "shader-qa")]
    [InlineData(DebugExperimentKind.MemorySafety, "memory-safety")]
    public void CapabilityExperiment_HasDeterministicEvidenceNamespace(DebugExperimentKind kind, string expected)
    {
        AutonomousQaCapabilityPolicy.EvidenceKind(kind).Should().Be(expected);
    }
}
