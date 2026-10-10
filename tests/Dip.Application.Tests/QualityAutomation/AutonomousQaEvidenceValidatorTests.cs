using Dip.Application.Features.QualityAutomation;
using FluentAssertions;

namespace Dip.Application.Tests.QualityAutomation;

public sealed class AutonomousQaEvidenceValidatorTests
{
    private readonly AutonomousQaEvidenceValidator _validator = new();

    [Fact]
    public void PassWithoutRequiredEvidence_FailsClosed()
    {
        var step = AutonomousQaExperimentDefaults.Create(
            "visual", DebugExperimentKind.VisualRegression, "rendering change", true, 120);
        var result = new AutonomousQaExperimentResult(
            step.Id, step.Kind, AutonomousQaExperimentPhase.PostPatch,
            AutonomousQaExperimentStatus.Passed, true, "reported pass",
            ["artifact:123"], "artifact:123", null,
            DateTimeOffset.UtcNow, DateTimeOffset.UtcNow);

        var enforced = _validator.Enforce(step, result);

        enforced.Status.Should().Be(AutonomousQaExperimentStatus.InfrastructureFailure);
        enforced.Verified.Should().BeFalse();
        enforced.EvidenceValidation!.MissingKinds.Should().Contain("visual-diff");
    }

    [Fact]
    public void PassWithExactRequiredEvidence_RemainsVerified()
    {
        var step = AutonomousQaExperimentDefaults.Create(
            "fuzz", DebugExperimentKind.Fuzz, "protocol change", true, 120);
        var result = new AutonomousQaExperimentResult(
            step.Id, step.Kind, AutonomousQaExperimentPhase.PostPatch,
            AutonomousQaExperimentStatus.Passed, true, "passed",
            ["fuzz:artifact:4cb31d"], "artifact:4cb31d", null,
            DateTimeOffset.UtcNow, DateTimeOffset.UtcNow);

        var enforced = _validator.Enforce(step, result);

        enforced.Status.Should().Be(AutonomousQaExperimentStatus.Passed);
        enforced.Verified.Should().BeTrue();
        enforced.EvidenceValidation!.Satisfied.Should().BeTrue();
    }
}
