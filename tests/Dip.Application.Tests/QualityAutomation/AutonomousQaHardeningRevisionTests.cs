// Purpose: Locks the observable autonomous-QA hardening revision and its mandatory guarantees.
using Dip.Application.Features.QualityAutomation;
using FluentAssertions;

namespace Dip.Application.Tests.QualityAutomation;

public sealed class AutonomousQaHardeningRevisionTests
{
    [Fact]
    public void CurrentRevision_DeclaresAllMandatoryGuarantees()
    {
        AutonomousQaHardeningRevision.Current.Should().Be("2026.10.05-r3");
        AutonomousQaHardeningRevision.Guarantees.Should().Contain(new[]
        {
            "sandboxed-runner-execution",
            "required-evidence-validation",
            "strict-capability-dispatch",
            "deterministic-required-experiments",
            "durable-artifact-persistence",
            "exact-base-commit-baseline",
            "durable-job-progress",
            "gpu-qa-extension"
        });
    }
}
